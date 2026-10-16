/*
  ESP8266 风扇控制器 + WiFiManager网页配网
  功能：MQTT+按键控制风扇；WiFiManager网页配网，WiFi/MQTT参数网页填写保存
*/
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <FS.h>

// 硬件引脚定义
const uint8_t FAN_SPEED_PIN = D7;  // 风扇PWM控制引脚
const uint8_t BEEP_PIN = D5;       // 蜂鸣器引脚
const uint8_t KEY_PIN = D1;        // 触摸按键引脚

// ========= MQTT参数，将由WiFiManager网页配置，从SPIFFS加载 =========
char mqtt_server[40] = "172.16.145.232";  // MQTT 服务器地址
char mqtt_port[6]  = "1883";  // MQTT 服务器端口
char mqtt_user[20] = "user";  // MQTT 用户名
char mqtt_pass[20] = "0001";  // MQTT 密码

// MQTT主题
const char *TOPIC_SUB = "fan/pc";   // 订阅主题："模式,速度"
const char *TOPIC_PUB = "fan/esp";  // 发布主题
const char *CLIENT_ID_PREFIX = "esp8266-fan-";  // MQTT客户端ID前缀

// WiFiManager自定义网页参数
WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT服务器", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT端口", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT用户名", mqtt_user, 20);
WiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT密码", mqtt_pass, 20);

// 标志位
bool shouldSaveConfig = false;

// 风扇模式枚举
typedef enum {
  FAN_OFF = 0,          // 风扇关闭
  FAN_MANUAL,           // 手动调速模式
  FAN_COS_WAVE,         // 余弦调速模式
  FAN_NATURAL,          // 自然风模式
  FAN_MODE_COUNT        // 模式数量
} FanMode;

FanMode current_mode = FAN_OFF;    // 当前风扇模式
uint8_t target_speed = 0;          // 目标速度
uint16_t natural_wind_counter = 0; // 自然风模式计数器
float angle = 0.0f;                // 角度用于余弦调速模式

const unsigned int BEEP_FREQ = 500;         // 蜂鸣器频率 500Hz
const unsigned long DEBOUNCE_DELAY = 200;   // 按键消抖延迟 200ms
const unsigned long LONG_PRESS_TIME = 10000;

WiFiClient espClient;           // Wi-Fi客户端
PubSubClient mqtt(espClient);   // MQTT客户端
Ticker speedTicker;             // 定时器
StaticJsonDocument<200> pubDoc; // 发布消息的JSON文档
volatile bool fanUpdateFlag = false;

// ========= WiFiManager 全局实例（重点修复！！） =========
WiFiManager wm;

// 保存配置回调
void saveConfigCallback() {
  Serial.println("收到保存配置指令");
  shouldSaveConfig = true;
}

// 从SPIFFS加载MQTT配置
bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("无保存配置文件，使用默认MQTT参数");
    return false;
  }
  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("配置文件过大");
    configFile.close();
    return false;
  }
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, buf.get());
  if (err) {
    Serial.println("JSON配置解析失败");
    configFile.close();
    return false;
  }
  strlcpy(mqtt_server, doc["mqtt_server"] | "172.16.145.232", sizeof(mqtt_server));
  strlcpy(mqtt_port, doc["mqtt_port"] | "1883", sizeof(mqtt_port));
  strlcpy(mqtt_user, doc["mqtt_user"] | "user", sizeof(mqtt_user));
  strlcpy(mqtt_pass, doc["mqtt_pass"] | "0001", sizeof(mqtt_pass));
  configFile.close();
  return true;
}

// 保存MQTT配置到SPIFFS
bool saveConfig() {
  StaticJsonDocument<512> doc;
  doc["mqtt_server"] = mqtt_server;
  doc["mqtt_port"] = mqtt_port;
  doc["mqtt_user"] = mqtt_user;
  doc["mqtt_pass"] = mqtt_pass;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("打开配置文件失败");
    return false;
  }
  serializeJson(doc, configFile);
  configFile.close();
  Serial.println("MQTT配置保存成功");
  return true;
}

// 删除所有保存配置（WiFi+MQTT）
void clearAllConfig() {
  Serial.println("清除所有WiFiManager设置");
  wm.resetSettings();
  SPIFFS.remove("/config.json");
  ESP.restart();
}

// MQTT重连函数
void reconnectMQTT() {
  static unsigned long lastRetry = 0;
  const unsigned long RETRY_INTERVAL = 5000;  // 5秒重试间隔
  if (millis() - lastRetry < RETRY_INTERVAL || WiFi.status() != WL_CONNECTED)
    return;
  Serial.println("尝试连接MQTT broker...");
  lastRetry = millis();
  String clientId = CLIENT_ID_PREFIX + String(random(0xFFFF), HEX);
  uint16_t mqttPort = atoi(mqtt_port);
  if (mqtt.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    Serial.println("MQTT连接成功");
    mqtt.subscribe(TOPIC_SUB);
  } else {
    Serial.printf("MQTT连接失败 rc=%d\n", mqtt.state());
  }
}

// MQTT消息回调
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 1. 检查消息是否来自我们的主题
  if (strcmp(topic, TOPIC_SUB) != 0) return;
  // 2. 声明并初始化JSON文档
  StaticJsonDocument<100> subDoc;
  
  // 3. 解析JSON消息
  DeserializationError error = deserializeJson(subDoc, payload, length);
  if (error) {
    Serial.printf("JSON解析错误: %s\n", error.c_str());
    return;
  }
  // 4. 安全类型转换并更新模式和速度
  if (subDoc.containsKey("mode") && subDoc.containsKey("speed")) {
    int modeVal = subDoc["mode"].as<int>();
    int spdVal = subDoc["speed"].as<int>();
    if (modeVal >= FAN_OFF && modeVal < FAN_MODE_COUNT) {
      current_mode = static_cast<FanMode>(modeVal);
      target_speed = constrain(spdVal, 0, 255);
      angle = 0.0f; // 重置角度
      natural_wind_counter = 0;
    }
  }
}

// 更新风扇速度
void updateFanSpeed() {
  uint8_t calculated_speed = 0;    // 计算出的速度
  const char* mode_name = "";      // 当前模式名称
  switch (current_mode) {
    case FAN_OFF:
      digitalWrite(FAN_SPEED_PIN, LOW); // 强制拉低，关闭PWM
      calculated_speed = 0;
      mode_name = "停止";
      break;
    case FAN_MANUAL:
      calculated_speed = target_speed;
      mode_name = "手动调速";
      analogWrite(FAN_SPEED_PIN, calculated_speed);
      break;
    case FAN_COS_WAVE: {
      float rad = angle * PI / 180.0f;  // 角度转换为弧度
      float tmp = cos(rad) * 100.0f;    // 计算余弦值
      calculated_speed = map(tmp, -100, 100, 180, 255); // 映射到PWM范围
      angle = fmod(angle + 3.0f, 360.0f); // 每次增加1度
      mode_name = "余弦调速";
      analogWrite(FAN_SPEED_PIN, calculated_speed);
      break;
    }
    case FAN_NATURAL:
      natural_wind_counter = (natural_wind_counter + 1) % 40;
      calculated_speed = (natural_wind_counter <= 20) ? 255 : 0;
      mode_name = "自然风";
      if (calculated_speed == 0)
        digitalWrite(FAN_SPEED_PIN, LOW);
      else
        analogWrite(FAN_SPEED_PIN, 255);
      break;
    default:
      digitalWrite(FAN_SPEED_PIN, LOW);
      calculated_speed = 0;
      mode_name = "未知";
      break;
  }
  pubDoc.clear();
  pubDoc["mode"] = static_cast<int>(current_mode);
  pubDoc["speed"] = calculated_speed;
  char jsonBuffer[30];
  serializeJson(pubDoc, jsonBuffer);
  if(mqtt.connected()){
    mqtt.publish(TOPIC_PUB, jsonBuffer);
  }
  Serial.printf("模式: %s, 速度: %d\n", mode_name, calculated_speed);
}

// 按键处理
void handleButtonPress() {
  static unsigned long pressStart = 0;
  static bool lastKeyState = HIGH;
  bool keyState = digitalRead(KEY_PIN);
  if (keyState != lastKeyState) {
    if (keyState == HIGH) { // TTP223触摸时输出高电平
      pressStart = millis();
    } else {
      unsigned long pressDur = millis() - pressStart;
      if(pressDur > DEBOUNCE_DELAY){
        if(pressDur >= LONG_PRESS_TIME){
          Serial.println("长按10秒：清除全部配置并重启");
          tone(BEEP_PIN, BEEP_FREQ, 300);
          clearAllConfig();
        }else{
          current_mode = static_cast<FanMode>((current_mode + 1) % FAN_MODE_COUNT);
          tone(BEEP_PIN, BEEP_FREQ, 200);
          angle = 0.0f;
          natural_wind_counter = 0;
          Serial.printf("切换模式至: %d\n", current_mode);
        }
      }
    }
    lastKeyState = keyState;
  }
}

// ticker回调函数（不用lambda，兼容老核心）
void tickerCallback()
{
  fanUpdateFlag = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP8266风扇控制器 WiFiManager版 ===");
  randomSeed(analogRead(A0));
  // 挂载SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS挂载失败");
  }
  loadConfig(); // 加载保存的MQTT参数
  pinMode(FAN_SPEED_PIN, OUTPUT);
  pinMode(BEEP_PIN, OUTPUT);
  pinMode(KEY_PIN, INPUT);
  analogWriteResolution(8); // 设置PWM分辨率为8位：占空比范围为 0 到 255
  analogWriteFreq(20000);  // 20kHz PWM频率
  analogWrite(FAN_SPEED_PIN, 0);

  // WiFiManager初始化，使用全局wm
  wm.setDebugOutput(true);
  wm.setConfigPortalTimeout(180); // 配网页面超时180秒自动退出
  wm.setSaveConfigCallback(saveConfigCallback);
  // 添加自定义MQTT参数到网页
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);

  // 自动连接，失败则开启AP配网门户
  bool connected = wm.autoConnect("ESP-Fan-Config", "fan123456");
  if (!connected) {
    Serial.println("配网门户超时，重启设备");
    ESP.restart();
  } else {
    Serial.println("WiFi连接成功");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    // 网页保存配置后，更新变量并写入SPIFFS
    if (shouldSaveConfig) {
      strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
      strlcpy(mqtt_port, custom_mqtt_port.getValue(), sizeof(mqtt_port));
      strlcpy(mqtt_user, custom_mqtt_user.getValue(), sizeof(mqtt_user));
      strlcpy(mqtt_pass, custom_mqtt_pass.getValue(), sizeof(mqtt_pass));
      saveConfig();
      Serial.println("配置保存完成，准备重启...");
      delay(500);
      ESP.restart();   // 新增：保存完立刻重启
    }
  }
  // MQTT初始化
  mqtt.setServer(mqtt_server, atoi(mqtt_port));
  mqtt.setCallback(mqttCallback);
  speedTicker.attach_ms(500, tickerCallback);
}

void loop() {
  if (!mqtt.connected() && WiFi.status() == WL_CONNECTED) {
    reconnectMQTT();
  }
  if (mqtt.connected()) {
    mqtt.loop();
  }
  handleButtonPress();

  if(fanUpdateFlag){
    fanUpdateFlag = false;
    updateFanSpeed();
  }
}
