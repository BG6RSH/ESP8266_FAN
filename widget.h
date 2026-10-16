#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QMqttClient>
#include <QLabel>
#include <QTimer>
#include <QMediaPlayer>
#include <QAudioOutput>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget {
    Q_OBJECT

  public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
  protected:
    void resizeEvent(QResizeEvent *event) override;
  private slots:
    void on_btn_connect_clicked();
    void on_dial_valueChanged(int value);
    void on_radioButton_0_clicked();
    void on_radioButton_1_clicked();
    void on_radioButton_2_clicked();
    void on_radioButton_3_clicked();
    void on_comboBox_timing_currentIndexChanged(int index);
    void on_btn_play_clicked();

  private:
    Ui::Widget *ui;
    QMqttClient *m_client;
    QLabel *l;
    QTimer *timing, *timer_play;
    QMediaPlayer* player;
    QAudioOutput* audioOutput;
    QStringList file_mp3;

    bool isStop;
    int mp3_index = 0;

    void mqtt_pub();
};
#endif // WIDGET_H
