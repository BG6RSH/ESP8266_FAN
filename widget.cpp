#include "widget.h"
#include "ui_widget.h"
#include <QNetworkProxy>
#include <QtMqtt/QMqttClient>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>

using namespace Qt::StringLiterals;

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget) {
    ui->setupUi(this);

    QString exePath = QCoreApplication::applicationDirPath() + "/";

    l = new QLabel(this);
    l->setText("255");
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet("font: 700 12pt \"Microsoft YaHei UI\";");
    l->show();

    // QNetworkProxy proxy;
    // proxy.setType(QNetworkProxy::HttpProxy);
    // proxy.setHostName("172.16.144.232");
    // proxy.setPort(9128);
    // proxy.setUser("D45623");
    // proxy.setPassword("zaq1XSW2");
    // QNetworkProxy::setApplicationProxy(proxy);

    m_client = new QMqttClient(this);
    m_client->setHostname("172.16.145.232");
    m_client->setPort(1883);
    m_client->setUsername("user");
    m_client->setPassword("0001");
    // m_client->connectToHost();

    timing = new QTimer(this);
    timing->stop();
    timing->setTimerType(Qt::VeryCoarseTimer);

    timer_play = new QTimer(this);
    connect(timer_play, &QTimer::timeout, this, [ = ]() {
        QDateTime t = QDateTime::currentDateTime();
        QString t_str = t.toString("hh:mm");//格式化时间
        if(t_str >= "18:00" or t_str <= "07:30") {
            qDebug() << "Playing:" << file_mp3.at(mp3_index);
            player->setSource(QUrl::fromLocalFile(file_mp3.at(mp3_index++)));
            if(mp3_index >= file_mp3.count()) mp3_index = 0;
            player->play();
        }
    });

    player = new QMediaPlayer;
    audioOutput = new QAudioOutput;
    player->setAudioOutput(audioOutput);
    // player->setSource(QUrl::fromLocalFile(exePath + "/猫叫.mp3"));
    connect(player, &QMediaPlayer::playbackStateChanged, this, [ = ](QMediaPlayer::PlaybackState newState) {
        switch (newState) {
        case QMediaPlayer::StoppedState:
            ui->speak->setPixmap(QPixmap(""));
            break;
        case QMediaPlayer::PlayingState:
            ui->speak->setPixmap(QPixmap(":/svg/volume-2.svg"));
            break;
        case QMediaPlayer::PausedState:

            break;
        }
    });

    connect(m_client, &QMqttClient::stateChanged, this, [this](QMqttClient::ClientState state) {
        QString m = "";
        switch (state) {
        case QMqttClient::Disconnected:
            m = "MQTT未连接。";
            ui->btn_connect->setText(tr("连接"));
            ui->groupBox->setEnabled(false);
            ui->comboBox_timing->setEnabled(false);
            break;
        case QMqttClient::Connecting:
            m = "MQTT连接中...";
            ui->groupBox->setEnabled(false);
            break;
        case QMqttClient::Connected:
            m = "MQTT已连接：172.16.144.232:1883";
            ui->btn_connect->setText(tr("断开"));
            ui->groupBox->setEnabled(true);
            ui->comboBox_timing->setEnabled(true);
            mqtt_pub();
            break;
        }
        ui->messager->setText(m);
        qDebug() << m;
    });

    connect(m_client, &QMqttClient::messageReceived, this, [this](const QByteArray & message, const QMqttTopicName & topic) {
        Q_UNUSED(topic);
        qDebug() << message;
        QJsonParseError jsonError;
        QJsonDocument doucment = QJsonDocument::fromJson(message, &jsonError);  // 转化为 JSON 文档
        if (!doucment.isNull() && (jsonError.error == QJsonParseError::NoError)) { // 解析未发生错误
            if (doucment.isObject()) {
                // JSON 文档为对象
                QJsonObject object = doucment.object();  // 转化为对象
                int mode = 0, speed = 0;
                if (object.contains("mode")) {
                    // 包含指定的 key
                    QJsonValue value = object.value("mode");  // 获取指定 key 对应的 value
                    mode = value.toInt();  // 将 value 转化为整数
                }
                if (object.contains("speed")) {
                    // 包含指定的 key
                    QJsonValue value = object.value("speed");  // 获取指定 key 对应的 value
                    speed = value.toInt();  // 将 value 转化为整数
                }
                qDebug() << mode << speed;
                QString str_tmp =  message;
                switch (mode) {
                case 0:
                    ui->radioButton_0->setChecked(true);
                    str_tmp = "工作模式：" + ui->radioButton_0->text();
                    break;
                case 1:
                    ui->radioButton_1->setChecked(true);
                    str_tmp = "工作模式：" + ui->radioButton_1->text();
                    break;
                case 2:
                    ui->radioButton_2->setChecked(true);
                    str_tmp = "工作模式：" + ui->radioButton_2->text();
                    break;
                case 3:
                    ui->radioButton_3->setChecked(true);
                    str_tmp = "工作模式：" + ui->radioButton_3->text();
                    break;
                }
                ui->dial->setValue(speed);
                ui->messager->setText(str_tmp);
            }
        }
    });

    connect(m_client, &QMqttClient::pingResponseReceived, this, [this]() {
        const QString content = QDateTime::currentDateTime().toString()
                                + "PingResponse\n"_L1;
        ui->messager->setText(content);
    });

    QDir dir(exePath, "*.mp3");
    dir.setFilter(QDir::Files);
    dir.setSorting(QDir::Size | QDir::Reversed);

    QFileInfoList list = dir.entryInfoList();
    qDebug() << "     Bytes Filename" ;
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo fileInfo = list.at(i);
        file_mp3 << exePath + fileInfo.fileName();
        qDebug() << qPrintable(QString("%1 %2").arg(fileInfo.size(), 10)
                               .arg(fileInfo.fileName()));
    }
}

Widget::~Widget() {
    delete ui;
}

void Widget::on_btn_connect_clicked() {
    if (m_client->state() == QMqttClient::Disconnected) {
        m_client->connectToHost();
    } else {
        m_client->disconnectFromHost();
    }
}

void Widget::mqtt_pub() {
    auto subscription = m_client->subscribe(QMqttTopicFilter("fan/esp"));
    if (!subscription) {
        ui->messager->setText("MQTT订阅失败！");
    }
}

void Widget::resizeEvent(QResizeEvent * event) {
    QWidget::resizeEvent(event);
    l->setGeometry((ui->dial->width() - 25) / 2 - 2, (ui->dial->height() - 8) / 2, 50, 23);
}

void Widget::on_dial_valueChanged(int value) {
    if(ui->radioButton_1->isChecked()) {
        QJsonObject json;
        QJsonDocument doc;
        json.insert("mode", 1);
        json.insert("speed", value);
        doc.setObject(json);
        if (m_client->publish(QMqttTopicName("fan/pc"), doc.toJson(QJsonDocument::Compact)) == -1)
            ui->messager->setText("MQTT发布失败！");
    }
    l->setText(QString::number(value));
}

void Widget::on_radioButton_0_clicked() {
    if(ui->radioButton_0->isChecked()) {
        QJsonObject json;
        QJsonDocument doc;
        json.insert("mode", 0);
        json.insert("speed", 255);
        doc.setObject(json);
        if (m_client->publish(QMqttTopicName("fan/pc"), doc.toJson(QJsonDocument::Compact)) == -1) {
            ui->messager->setText("MQTT发布失败！");
            return;
        }
        isStop = true;
    }
}

void Widget::on_radioButton_1_clicked() {
    if(ui->radioButton_1->isChecked()) {
        QJsonObject json;
        QJsonDocument doc;
        json.insert("mode", 1);
        if(isStop)
            json.insert("speed", 255);
        else
            json.insert("speed", ui->dial->value());
        doc.setObject(json);
        if (m_client->publish(QMqttTopicName("fan/pc"), doc.toJson(QJsonDocument::Compact)) == -1) {
            ui->messager->setText("MQTT发布失败！");
            return;
        }
        isStop = false;
    }
}

void Widget::on_radioButton_2_clicked() {
    if(ui->radioButton_2->isChecked()) {
        QJsonObject json;
        QJsonDocument doc;
        json.insert("mode", 2);
        json.insert("speed", 255);
        doc.setObject(json);
        if (m_client->publish(QMqttTopicName("fan/pc"), doc.toJson(QJsonDocument::Compact)) == -1) {
            ui->messager->setText("MQTT发布失败！");
            return;
        }
        isStop = false;
    }
}

void Widget::on_radioButton_3_clicked() {
    if(ui->radioButton_3->isChecked()) {
        QJsonObject json;
        QJsonDocument doc;
        json.insert("mode", 3);
        json.insert("speed", 255);
        doc.setObject(json);
        if (m_client->publish(QMqttTopicName("fan/pc"), doc.toJson(QJsonDocument::Compact)) == -1) {
            ui->messager->setText("MQTT发布失败！");
            return;
        }
        isStop = false;
    }
}

void Widget::on_comboBox_timing_currentIndexChanged(int index) {
    int ms;
    if(index == 0) {
        timing->stop();
        ui->label_timing->setText("定时器关闭");
        return;
    }
    if(index > 0) {
        ms = index * 60 * 60 * 1000; // 将小时转换为毫秒
        QTimer::singleShot(ms, this, [ = ]() {
            ui->radioButton_0->setChecked(true);
            on_radioButton_0_clicked();
            QDateTime tt = QDateTime::currentDateTime();
            ui->label_timing->setText("风扇已于" + tt.toString(" yyyy-MM-dd hh:mm:ss ") + "关闭。");
            ui->comboBox_timing->setCurrentIndex(0);
        });
        QDateTime t = QDateTime::currentDateTime();
        t = t.addSecs(index * 60 * 60);
        ui->label_timing->setText("风扇将在" + t.toString(" yyyy-MM-dd hh:mm:ss ") + "关闭。");
    }
}

void Widget::on_btn_play_clicked() {
    if(timer_play->isActive()) {
        timer_play->stop();
        ui->btn_play->setText("播放");
    } else {
        timer_play->start(30 * 1000);
        ui->btn_play->setText("停止");
        ui->messager->setText("开始播放“猫叫.mp3”.");
    }
}

