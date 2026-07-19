#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QFile>
#include <QByteArray>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSpinBox>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSelectFile();
    void onUnpack();
    void onSendSingle();
    void onSendBatch();
    void onSendCurrent();
    void onRefreshSerial();
    void onConnectSerial();
    void onSerialReadyRead();

private:
    void log(const QString &msg);
    quint32 calculateCRC32(const QByteArray &data);
    quint16 calculateCRC16(const QByteArray &data);
    void refreshSerialPorts();
    quint32 generateTaskID();
    bool sendNegotiationCommand();
    bool validateNegotiationACK(const QByteArray &response);
    bool sendDataPacket(bool isFirstSubPacket, int subIndex, quint32 packetSeq, const QByteArray &data, quint32 taskID, quint16 totalPacketSize);
    bool sendBothSubPackets(int packetIndex, const QByteArray &bigPacket);
    QByteArray waitForResponse(int timeoutMs);

    Ui::MainWindow *ui;
    QByteArray m_rxBuffer;
    int m_currentPacketIndex;
    QString m_selectedFile;
    QByteArray m_fileData;
    QVector<QByteArray> m_packets;
    quint32 m_totalCRC32;
    quint32 m_taskID;
    QSerialPort *m_serialPort;
    bool m_serialConnected;

    // 两种延时控件
    QSpinBox *m_spinInterPacketDelay;    // 包与包之间的延时
    int m_interPacketDelayMs;
    QSpinBox *m_spinSubPacketDelay;      // 子包与子包之间的延时
    int m_subPacketDelayMs;
};

#endif // MAINWINDOW_H