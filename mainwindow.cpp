#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QDateTime>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QSpinBox>
#include <QLabel>
#include <QCloseEvent>
#include <QTextBlock>
#include <QTextCursor>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_totalCRC32(0)
    , m_currentPacketIndex(0)
    , m_serialPort(nullptr)
    , m_serialConnected(false)
    , m_interPacketDelayMs(100)
    , m_subPacketDelayMs(100)
    , m_queryTimeoutMs(500)
    , m_stopRequested(false)
{
    ui->setupUi(this);

    connect(ui->btnSelectFile,     &QPushButton::clicked, this, &MainWindow::onSelectFile);
    connect(ui->btnUnpack,         &QPushButton::clicked, this, &MainWindow::onUnpack);
    connect(ui->btnSendSingle,     &QPushButton::clicked, this, &MainWindow::onSendSingle);
    connect(ui->btnSendBatch,      &QPushButton::clicked, this, &MainWindow::onSendBatch);
    connect(ui->btnSendCurrent,    &QPushButton::clicked, this, &MainWindow::onSendCurrent);
    connect(ui->btnRefreshSerial,  &QPushButton::clicked, this, &MainWindow::onRefreshSerial);
    connect(ui->btnConnectSerial,  &QPushButton::clicked, this, &MainWindow::onConnectSerial);

    // 文件路径可手动输入
    ui->editFilePath->setReadOnly(false);

    // 初始化串口对象
    m_serialPort = new QSerialPort(this);
    connect(m_serialPort, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);

    // 刷新串口列表
    refreshSerialPorts();

    // 添加包间延时 SpinBox
    QLabel *labelInterPktDelay = new QLabel(QString::fromUtf8("包间延时(ms):"), this);
    m_spinInterPacketDelay = new QSpinBox(this);
    m_spinInterPacketDelay->setRange(0, 10000);
    m_spinInterPacketDelay->setValue(100);
    m_spinInterPacketDelay->setSuffix(" ms");
    connect(m_spinInterPacketDelay, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        m_interPacketDelayMs = val;
    });

    // 添加子包间延时 SpinBox
    QLabel *labelSubPktDelay = new QLabel(QString::fromUtf8("子包间延时(ms):"), this);
    m_spinSubPacketDelay = new QSpinBox(this);
    m_spinSubPacketDelay->setRange(0, 10000);
    m_spinSubPacketDelay->setValue(100);
    m_spinSubPacketDelay->setSuffix(" ms");
    connect(m_spinSubPacketDelay, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        m_subPacketDelayMs = val;
    });

    // 添加查询超时 SpinBox
    QLabel *labelQueryTimeout = new QLabel(QString::fromUtf8("查询超时(ms):"), this);
    m_spinQueryTimeout = new QSpinBox(this);
    m_spinQueryTimeout->setRange(0, 10000);
    m_spinQueryTimeout->setValue(500);
    m_spinQueryTimeout->setSuffix(" ms");
    connect(m_spinQueryTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        m_queryTimeoutMs = val;
    });

    // 添加停止发送按钮
    m_btnStopSend = new QPushButton(QString::fromUtf8("停止发送"), this);
    connect(m_btnStopSend, &QPushButton::clicked, this, &MainWindow::onStopSend);
    m_btnStopSend->setEnabled(false);

    // 添加重置按钮
    m_btnReset = new QPushButton(QString::fromUtf8("重置"), this);
    connect(m_btnReset, &QPushButton::clicked, this, &MainWindow::onResetSend);

    ui->actionLayout->addWidget(labelInterPktDelay);
    ui->actionLayout->addWidget(m_spinInterPacketDelay);
    ui->actionLayout->addWidget(labelSubPktDelay);
    ui->actionLayout->addWidget(m_spinSubPacketDelay);
    ui->actionLayout->addWidget(labelQueryTimeout);
    ui->actionLayout->addWidget(m_spinQueryTimeout);
    ui->actionLayout->addWidget(m_btnStopSend);
    ui->actionLayout->addWidget(m_btnReset);

    // 恢复上次配置
    loadSettings();
}

MainWindow::~MainWindow()
{
    if (m_serialPort->isOpen())
        m_serialPort->close();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}

void MainWindow::loadSettings()
{
    QSettings settings("BinParser", "BinParserApp");
    // 窗口位置和大小
    if (settings.contains("window/geometry"))
        restoreGeometry(settings.value("window/geometry").toByteArray());
    // 串口配置
    if (settings.contains("serial/port"))
        ui->comboSerialPort->setCurrentText(settings.value("serial/port").toString());
    if (settings.contains("serial/baud"))
        ui->comboBaudRate->setCurrentText(settings.value("serial/baud").toString());
    // 分块大小
    if (settings.contains("config/chunkSize"))
        ui->spinChunkSize->setValue(settings.value("config/chunkSize").toInt());
    // 版本号
    if (settings.contains("config/version"))
        ui->editVersion->setText(settings.value("config/version").toString());
    // 延时值
    if (settings.contains("config/interPacketDelay"))
        m_spinInterPacketDelay->setValue(settings.value("config/interPacketDelay").toInt());
    if (settings.contains("config/subPacketDelay"))
        m_spinSubPacketDelay->setValue(settings.value("config/subPacketDelay").toInt());
    if (settings.contains("config/queryTimeout"))
        m_spinQueryTimeout->setValue(settings.value("config/queryTimeout").toInt());
}

void MainWindow::saveSettings()
{
    QSettings settings("BinParser", "BinParserApp");
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("serial/port", ui->comboSerialPort->currentText());
    settings.setValue("serial/baud", ui->comboBaudRate->currentText());
    settings.setValue("config/chunkSize", ui->spinChunkSize->value());
    settings.setValue("config/version", ui->editVersion->text());
    settings.setValue("config/interPacketDelay", m_spinInterPacketDelay->value());
    settings.setValue("config/subPacketDelay", m_spinSubPacketDelay->value());
    settings.setValue("config/queryTimeout", m_spinQueryTimeout->value());
}

void MainWindow::onStopSend()
{
    m_stopRequested = true;
    log(QString::fromUtf8("用户请求停止发送"));
    m_btnStopSend->setEnabled(false);
}

void MainWindow::onResetSend()
{
    m_stopRequested = true;
    m_sendingInProgress = false;
    m_taskID = 0;
    m_currentPacketIndex = 0;
    m_btnStopSend->setEnabled(false);
    ui->btnSendSingle->setEnabled(true);
    ui->btnSendBatch->setEnabled(true);
    ui->btnSendCurrent->setEnabled(true);
    log(QString::fromUtf8("已重置，回到协商前的状态"));
}

void MainWindow::log(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui->textLog->append(QString("[%1] %2").arg(timestamp, msg));

    // 日志超过 3000 行自动覆盖
    QTextDocument *doc = ui->textLog->document();
    if (doc->blockCount() > 3000) {
        QTextCursor cursor(doc->begin());
        // 删除最早的 ~500 行
        for (int i = 0; i < 500; ++i) {
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();
            cursor.deleteChar(); // remove newline
        }
    }
}

// ---------- CRC32 ----------
quint32 MainWindow::calculateCRC32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFF;
    const unsigned char *buf = reinterpret_cast<const unsigned char *>(data.constData());
    int len = data.size();

    for (int i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc = (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ---------- CRC16 Modbus ----------
quint16 MainWindow::calculateCRC16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    const unsigned char *buf = reinterpret_cast<const unsigned char *>(data.constData());
    int len = data.size();

    for (int i = 0; i < len; ++i) {
        crc ^= static_cast<quint16>(buf[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc = (crc >> 1);
        }
    }
    return crc;
}

quint32 MainWindow::generateTaskID()
{
    return QRandomGenerator::global()->generate();
}

// ---------- 辅助 ----------
static void appendU16LE(QByteArray &arr, quint16 val)
{
    arr.append(static_cast<char>(val & 0xFF));
    arr.append(static_cast<char>((val >> 8) & 0xFF));
}

static void appendU16BE(QByteArray &arr, quint16 val)
{
    arr.append(static_cast<char>((val >> 8) & 0xFF));
    arr.append(static_cast<char>(val & 0xFF));
}

static void appendU32LE(QByteArray &arr, quint32 val)
{
    for (int i = 0; i < 4; ++i)
        arr.append(static_cast<char>((val >> (i * 8)) & 0xFF));
}

static void appendU32BE(QByteArray &arr, quint32 val)
{
    for (int i = 3; i >= 0; --i)
        arr.append(static_cast<char>((val >> (i * 8)) & 0xFF));
}

// ---------- 1. 选择文件 ----------
void MainWindow::onSelectFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("选择Bin文件"),
        QString(),
        QString::fromUtf8("Bin文件 (*.bin);;所有文件 (*.*)")
    );

    if (filePath.isEmpty())
        return;

    m_selectedFile = filePath;
    ui->editFilePath->setText(filePath);

    m_fileData.clear();
    m_packets.clear();
    m_totalCRC32 = 0;
    m_taskID = 0;
    m_currentPacketIndex = 0;

    QFileInfo fi(filePath);
    log(QString::fromUtf8("已选择文件: %1  (%2 字节)")
            .arg(fi.fileName())
            .arg(fi.size()));
}

// ---------- 2. 拆包 ----------
void MainWindow::onUnpack()
{
    // 支持手动输入路径
    QString filePath = ui->editFilePath->text().trimmed();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先选择或输入Bin文件路径！"));
        return;
    }
    m_selectedFile = filePath;

    QFile inFile(m_selectedFile);
    if (!inFile.open(QIODevice::ReadOnly)) {
        log(QString::fromUtf8("错误: 无法打开文件 %1").arg(m_selectedFile));
        return;
    }

    int chunkSize = ui->spinChunkSize->value();
    if (chunkSize <= 0) {
        log(QString::fromUtf8("错误: 分块大小必须大于0"));
        inFile.close();
        return;
    }

    m_fileData = inFile.readAll();
    inFile.close();

    m_totalCRC32 = calculateCRC32(m_fileData);

    QFileInfo fi(m_selectedFile);
    log(QString::fromUtf8("=========================================="));
    log(QString::fromUtf8("开始拆包: %1").arg(fi.fileName()));
    log(QString::fromUtf8("文件大小: %1 字节").arg(m_fileData.size()));
    log(QString::fromUtf8("分块大小: %1 字节").arg(chunkSize));
    log(QString::fromUtf8("CRC32 校验: 0x%1").arg(m_totalCRC32, 8, 16, QLatin1Char('0')));

    m_packets.clear();
    qint64 offset = 0;
    int packetIndex = 0;

    while (offset < m_fileData.size()) {
        qint64 bytesToRead = qMin((qint64)chunkSize, m_fileData.size() - offset);
        QByteArray packet = m_fileData.mid(offset, bytesToRead);
        m_packets.append(packet);

        QByteArray preview = packet.left(16);
        log(QString::fromUtf8("  包序号=%1 | 偏移=0x%2 | 大小=%3 字节 | 数据: %4%5")
                .arg(packetIndex, 4, 10, QLatin1Char('0'))
                .arg(offset, 8, 16, QLatin1Char('0'))
                .arg(packet.size(), 4)
                .arg(QString(preview.toHex(' ').toUpper()))
                .arg(packet.size() > 16 ? QString::fromUtf8(" ...") : QString()));

        offset += bytesToRead;
        ++packetIndex;
    }

    log(QString::fromUtf8("拆包完成！共生成 %1 个包，已存入缓存").arg(m_packets.size()));
    log(QString::fromUtf8("=========================================="));
}

// ---------- 3. 协商命令 ----------
bool MainWindow::sendNegotiationCommand()
{
    QString version = ui->editVersion->text().trimmed();
    if (version.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请输入固件版本号（4字节ASCII）！"));
        return false;
    }

    QByteArray versionBytes = version.toUtf8().left(4);
    while (versionBytes.size() < 4)
        versionBytes.append('\0');

    m_taskID = generateTaskID();

    QByteArray cmd;
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x06));
    cmd.append(static_cast<char>(0x51));
    cmd.append(static_cast<char>(0x00));
    cmd.append(static_cast<char>(0xA0));
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x01));
    appendU32BE(cmd, m_taskID);
    cmd.append(versionBytes);
    quint32 fileSize = static_cast<quint32>(m_fileData.size());
    appendU32BE(cmd, fileSize);
    appendU32LE(cmd, m_totalCRC32);
    quint8 dataLen = static_cast<quint8>(4 + 4 + 4 + 4 + 4);
    cmd.insert(4, static_cast<char>(dataLen));
    quint16 cmdCRC16 = calculateCRC16(cmd);
    appendU16LE(cmd, cmdCRC16);

    log(QString::fromUtf8("---------- 发送协商命令 ----------"));
    log(QString::fromUtf8("协商命令 HEX: %1").arg(QString(cmd.toHex(' ').toUpper())));
    log(QString::fromUtf8("  头部: 01 06 51 00 | 数据长度: 0x%1 | A0 01 01 01(升级标志)").arg(dataLen, 2, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  任务ID: 0x%1").arg(m_taskID, 8, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  版本号: %1 (ASCII: '%2')").arg(QString(versionBytes.toHex(' ').toUpper()), QString::fromUtf8(versionBytes)));
    log(QString::fromUtf8("  固件大小: %1 字节 (0x%2)").arg(fileSize).arg(fileSize, 8, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  固件CRC32: 0x%1").arg(m_totalCRC32, 8, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  CRC16校验: 0x%1").arg(cmdCRC16, 4, 16, QLatin1Char('0')));
    log(QString::fromUtf8("------------------------------------"));

    qint64 written = m_serialPort->write(cmd);
    if (written < 0 || written != cmd.size()) {
        log(QString::fromUtf8("错误: 协商命令发送失败!"));
        return false;
    }

    log(QString::fromUtf8("协商命令已发送，等待固件ACK应答..."));
    QByteArray response = waitForResponse(5000);
    if (response.isEmpty()) {
        log(QString::fromUtf8("错误: 等待固件协商ACK超时 (5秒)"));
        return false;
    }
    log(QString::fromUtf8("收到固件协商应答: %1").arg(QString(response.toHex(' ').toUpper())));

    if (!validateNegotiationACK(response)) {
        log(QString::fromUtf8("错误: 协商ACK验证失败！"));
        return false;
    }

    log(QString::fromUtf8("协商ACK验证通过，可进行下一步发包"));
    log(QString::fromUtf8("------------------------------------"));
    return true;
}

bool MainWindow::validateNegotiationACK(const QByteArray &response)
{
    // 从合并应答中查找 ACK 帧 (01 06 51 00 开头)
    const char ackHeader[] = { 0x01, 0x06, 0x51, 0x00 };
    int ackPos = response.indexOf(QByteArray(ackHeader, 4));
    if (ackPos < 0) {
        log(QString::fromUtf8("  ACK验证: 未找到ACK帧头 01 06 51 00"));
        return false;
    }

    // ACK 帧结构: 01 06 51 00 + 1B数据长度 + 1B数据 + 2B CRC = 8 字节
    if (ackPos + 8 > response.size()) {
        log(QString::fromUtf8("  ACK帧不完整 (需要8字节，剩余%1)").arg(response.size() - ackPos));
        return false;
    }

    // ACK 数据字节在帧中的位置: header(4) + dataLen(1) = 第5字节 (0-indexed: ackPos+4)
    quint8 checkByte = static_cast<quint8>(response.at(ackPos + 5));
    log(QString::fromUtf8("  ACK验证: 帧位置=%1, 数据字节=0x%2 (%3)")
            .arg(ackPos)
            .arg(checkByte, 2, 16, QLatin1Char('0'))
            .arg(checkByte == 0x01 ? QString::fromUtf8("通过") : QString::fromUtf8("失败,期望0x01")));
    return checkByte == 0x01;
}

// ---------- 发送子数据包 ----------
bool MainWindow::sendDataPacket(bool isFirstSubPacket, int subIndex, quint32 packetSeq, const QByteArray &data, quint32 taskID, quint16 totalPacketSize)
{
    QByteArray pkt;

    pkt.append(static_cast<char>(0x01));
    pkt.append(static_cast<char>(0x06));
    pkt.append(static_cast<char>(0x51));
    pkt.append(static_cast<char>(isFirstSubPacket ? 0x00 : 0x01));

    quint8 dataLen = static_cast<quint8>(3 + 4 + 2 + 1 + 4 + data.size());
    pkt.append(static_cast<char>(dataLen));

    pkt.append(static_cast<char>(0xA0));
    pkt.append(static_cast<char>(0x01));
    pkt.append(static_cast<char>(0x02));

    appendU32BE(pkt, taskID);
    quint16 dataSize = totalPacketSize;
    appendU16BE(pkt, dataSize);
    pkt.append(static_cast<char>(0x01));
    appendU32BE(pkt, packetSeq);
    pkt.append(data);

    quint16 pktCRC16 = calculateCRC16(pkt);
    appendU16LE(pkt, pktCRC16);

    QString regName = isFirstSubPacket ? QString::fromUtf8("51 00") : QString::fromUtf8("51 01");
    log(QString::fromUtf8("  >>> 发包: 寄存器=%1 | 子包=%2/2 | 序号=0x%3 | 数据大小=%4 | CRC16=0x%5 | 总长=%6")
            .arg(regName).arg(subIndex + 1).arg(packetSeq, 8, 16, QLatin1Char('0'))
            .arg(dataSize).arg(pktCRC16, 4, 16, QLatin1Char('0')).arg(pkt.size()));
    log(QString::fromUtf8("  [串口发送] %1").arg(QString(pkt.toHex(' ').toUpper())));

    qint64 written = m_serialPort->write(pkt);
    if (written < 0 || written != pkt.size()) {
        log(QString::fromUtf8("  错误: 发送失败 (已写入 %1 / %2)").arg(written).arg(pkt.size()));
        return false;
    }
    return true;
}

// ---------- 发送两个子包 ----------
bool MainWindow::sendBothSubPackets(int packetIndex, const QByteArray &bigPacket)
{
    if (m_stopRequested) return false;

    int maxSubSize = (ui->spinChunkSize->value() + 1) / 2;
    int subSize1 = qMin(bigPacket.size(), maxSubSize);
    int subSize2 = bigPacket.size() - subSize1;
    QByteArray subData1 = bigPacket.left(subSize1);
    QByteArray subData2 = bigPacket.mid(subSize1, subSize2);
    quint32 packetSeq = static_cast<quint32>(packetIndex);
    quint16 totalSize = static_cast<quint16>(bigPacket.size());

    log(QString::fromUtf8("--- 发送固件包 %1/%2 (序号=%3) ---")
            .arg(packetIndex + 1).arg(m_packets.size()).arg(packetIndex));

    // 子包1
    log(QString::fromUtf8("  子包1: 寄存器 51 00, 数据=%1字节").arg(subData1.size()));
    m_rxBuffer.clear();
    if (!sendDataPacket(true, 0, packetSeq, subData1, m_taskID, totalSize)) {
        log(QString::fromUtf8("子包1发送失败，终止"));
        return false;
    }
    {
        QByteArray resp = waitForResponse(500);
        if (!resp.isEmpty())
            log(QString::fromUtf8("  返回: %1").arg(QString(resp.toHex(' ').toUpper())));
    }

    if (m_stopRequested) return false;

    if (subData2.size() > 0 && m_subPacketDelayMs > 0)
        QThread::msleep(m_subPacketDelayMs);

    // 子包2
    if (subData2.size() > 0) {
        log(QString::fromUtf8("  子包2: 寄存器 51 01, 数据=%1字节").arg(subData2.size()));
        m_rxBuffer.clear();
        if (!sendDataPacket(false, 1, packetSeq, subData2, m_taskID, totalSize)) {
            log(QString::fromUtf8("子包2发送失败，终止"));
            return false;
        }
        {
            QByteArray resp = waitForResponse(500);
            if (!resp.isEmpty())
                log(QString::fromUtf8("  返回: %1").arg(QString(resp.toHex(' ').toUpper())));
        }
    } else {
        log(QString::fromUtf8("  子包2: 数据为空，跳过"));
    }

    if (m_stopRequested) return false;

    // 查询命令
    {
        QByteArray queryCmd;
        queryCmd.append(static_cast<char>(0x01));
        queryCmd.append(static_cast<char>(0x03));
        queryCmd.append(static_cast<char>(0x51));
        queryCmd.append(static_cast<char>(0x01));
        queryCmd.append(static_cast<char>(0x00));
        quint16 queryCRC = calculateCRC16(queryCmd);
        appendU16LE(queryCmd, queryCRC);
        log(QString::fromUtf8("  [查询命令] %1").arg(QString(queryCmd.toHex(' ').toUpper())));
        m_serialPort->write(queryCmd);
    }
    {
        QByteArray resp = waitForResponse(m_queryTimeoutMs);
        if (resp.isEmpty()) {
            log(QString::fromUtf8("  [查询返回] 无应答 (超时%1ms)，停止发送").arg(m_queryTimeoutMs));
            return false;
        }
        log(QString::fromUtf8("  [查询返回] %1").arg(QString(resp.toHex(' ').toUpper())));

        // 验证查询应答: 期望收到 03 51 01 01 A5 (不含可变地址头)
        const char expected[] = { 0x03, 0x51, 0x01, 0x01, (char)0xA5 };
        if (!resp.contains(QByteArray(expected, 5))) {
            log(QString::fromUtf8("  查询应答验证失败: 未匹配 03 51 01 01 A5，停止发送"));
            return false;
        }

        log(QString::fromUtf8("  查询应答验证通过 (03 51 01 01 A5)"));
    }

    return true;
}

// ---------- 4. 单次发送 ----------
void MainWindow::onSendSingle()
{
    if (m_packets.isEmpty()) { QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先执行拆包操作！")); return; }
    if (!m_serialConnected) { QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先连接串口！")); return; }

    m_stopRequested = false;

    if (m_currentPacketIndex == 0) {
        log(QString::fromUtf8("========== 单次发送流程开始 (共%1包) ==========").arg(m_packets.size()));
        if (!sendNegotiationCommand()) { m_currentPacketIndex = 0; return; }
        log(QString::fromUtf8("等待1秒后开始发包..."));
        QThread::msleep(1000);
    }

    if (m_currentPacketIndex >= m_packets.size()) {
        log(QString::fromUtf8("所有 %1 个包已发送完毕").arg(m_packets.size()));
        m_currentPacketIndex = 0;
        return;
    }

    if (!sendBothSubPackets(m_currentPacketIndex, m_packets[m_currentPacketIndex])) {
        m_currentPacketIndex = 0; return;
    }

    ++m_currentPacketIndex;

    if (m_currentPacketIndex >= m_packets.size()) {
        log(QString::fromUtf8("所有 %1 个包发送完毕！").arg(m_packets.size()));
        log(QString::fromUtf8("========== 单次发送流程结束 =========="));
        m_currentPacketIndex = 0;
    } else {
        log(QString::fromUtf8("固件包 %1/%2 已发送，请再次点击单次发送发送下一个包")
                .arg(m_currentPacketIndex).arg(m_packets.size()));
    }
}

// ---------- 4b. 当前包发送 ----------
void MainWindow::onSendCurrent()
{
    if (m_packets.isEmpty()) { QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先执行拆包操作！")); return; }
    if (!m_serialConnected) { QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先连接串口！")); return; }

    m_stopRequested = false;

    int i = qMax(0, m_currentPacketIndex - 1);
    if (i >= m_packets.size()) {
        log(QString::fromUtf8("所有 %1 个包已发送完毕，无法重复发送").arg(m_packets.size()));
        return;
    }

    if (m_taskID == 0) {
        log(QString::fromUtf8("========== 重复发送当前包模式 (共%1包) ==========").arg(m_packets.size()));
        if (!sendNegotiationCommand()) return;
        log(QString::fromUtf8("等待1秒后开始发送当前包..."));
        QThread::msleep(1000);
    }

    sendBothSubPackets(i, m_packets[i]);
    log(QString::fromUtf8("固件包 %1/%2 已重复发送").arg(i + 1).arg(m_packets.size()));
}

// ---------- 5. 一次性发送 ----------
void MainWindow::onSendBatch()
{
    if (m_packets.isEmpty()) { QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先执行拆包操作！")); return; }
    if (!m_serialConnected) { QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先连接串口！")); return; }

    m_stopRequested = false;
    m_btnStopSend->setEnabled(true);

    log(QString::fromUtf8("========== 一次性发送流程开始 (共%1包) ==========").arg(m_packets.size()));
    if (!sendNegotiationCommand()) { m_btnStopSend->setEnabled(false); return; }

    log(QString::fromUtf8("等待1秒后开始一次性发送..."));
    QThread::msleep(1000);
    log(QString::fromUtf8("开始一次性发送，总固件包数: %1").arg(m_packets.size()));

    int i;
    for (i = 0; i < m_packets.size(); ++i) {
        if (m_stopRequested) {
            log(QString::fromUtf8("发送已被用户中断"));
            break;
        }
        if (!sendBothSubPackets(i, m_packets[i])) {
            log(QString::fromUtf8("固件包 %1/%2 发送失败，批量发送中断").arg(i + 1).arg(m_packets.size()));
            break;
        }

        if (i < m_packets.size() - 1 && m_interPacketDelayMs > 0) {
            log(QString::fromUtf8("  包间延时 %1 ms...").arg(m_interPacketDelayMs));
            QThread::msleep(m_interPacketDelayMs);
        }
    }

    if (!m_stopRequested && i >= m_packets.size())
        log(QString::fromUtf8("所有 %1 个固件包已一次性发送完毕").arg(m_packets.size()));
    log(QString::fromUtf8("========== 一次性发送流程结束 =========="));
    m_btnStopSend->setEnabled(false);
}

// ---------- 6. 刷新串口 ----------
void MainWindow::refreshSerialPorts()
{
    ui->comboSerialPort->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString display = info.portName();
        if (!info.description().isEmpty())
            display += QString(" - %1").arg(info.description());
        ui->comboSerialPort->addItem(display, info.portName());
    }
    if (ports.isEmpty())
        log(QString::fromUtf8("串口检测: 未发现可用串口"));
    else
        log(QString::fromUtf8("串口检测: 发现 %1 个串口").arg(ports.size()));
}

void MainWindow::onRefreshSerial()
{
    refreshSerialPorts();
}

// ---------- 7. 串口连接 ----------
void MainWindow::onConnectSerial()
{
    if (m_serialConnected) {
        m_serialPort->close();
        m_serialConnected = false;
        ui->btnConnectSerial->setText(QString::fromUtf8("连接"));
        ui->labelSerialStatus->setText(QString::fromUtf8("未连接"));
        ui->labelSerialStatus->setStyleSheet("color: red; font-weight: bold;");
        log(QString::fromUtf8("串口已断开: %1").arg(m_serialPort->portName()));
        return;
    }

    if (ui->comboSerialPort->currentIndex() < 0) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("没有可用的串口，请先刷新！"));
        return;
    }

    QString portName = ui->comboSerialPort->currentData().toString();
    int baudRate = ui->comboBaudRate->currentText().toInt();

    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        log(QString::fromUtf8("错误: 无法打开串口 %1, 波特率: %2").arg(portName).arg(baudRate));
        QMessageBox::critical(this, QString::fromUtf8("错误"),
            QString::fromUtf8("无法打开串口 %1: %2").arg(portName, m_serialPort->errorString()));
        return;
    }

    m_serialConnected = true;
    ui->btnConnectSerial->setText(QString::fromUtf8("断开"));
    ui->labelSerialStatus->setText(QString::fromUtf8("已连接"));
    ui->labelSerialStatus->setStyleSheet("color: green; font-weight: bold;");
    log(QString::fromUtf8("串口已连接: %1, 波特率: %2, 8N1").arg(portName).arg(baudRate));
}

// ---------- 8. 接收 ----------
void MainWindow::onSerialReadyRead()
{
    QByteArray data = m_serialPort->readAll();
    m_rxBuffer.append(data);
    log(QString::fromUtf8("[串口接收] %1").arg(QString(data.toHex(' ').toUpper())));
}

QByteArray MainWindow::waitForResponse(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (!m_rxBuffer.isEmpty()) {
            QByteArray result = m_rxBuffer;
            m_rxBuffer.clear();
            return result;
        }
    }
    return QByteArray();
}