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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_totalCRC32(0)
    , m_currentPacketIndex(0)
    , m_serialPort(nullptr)
    , m_serialConnected(false)
    , m_interPacketDelayMs(100)
    , m_subPacketDelayMs(100)
{
    ui->setupUi(this);

    connect(ui->btnSelectFile,     &QPushButton::clicked, this, &MainWindow::onSelectFile);
    connect(ui->btnUnpack,         &QPushButton::clicked, this, &MainWindow::onUnpack);
    connect(ui->btnSendSingle,     &QPushButton::clicked, this, &MainWindow::onSendSingle);
    connect(ui->btnSendBatch,      &QPushButton::clicked, this, &MainWindow::onSendBatch);
    connect(ui->btnSendCurrent,    &QPushButton::clicked, this, &MainWindow::onSendCurrent);
    connect(ui->btnRefreshSerial,  &QPushButton::clicked, this, &MainWindow::onRefreshSerial);
    connect(ui->btnConnectSerial,  &QPushButton::clicked, this, &MainWindow::onConnectSerial);

    // 初始化串口对象
    m_serialPort = new QSerialPort(this);
    connect(m_serialPort, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);

    // 启动时刷新串口列表
    refreshSerialPorts();

    // 默认波特率选 115200
    ui->comboBaudRate->setCurrentText("115200");

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

    ui->actionLayout->addWidget(labelInterPktDelay);
    ui->actionLayout->addWidget(m_spinInterPacketDelay);
    ui->actionLayout->addWidget(labelSubPktDelay);
    ui->actionLayout->addWidget(m_spinSubPacketDelay);
}

MainWindow::~MainWindow()
{
    if (m_serialPort->isOpen())
        m_serialPort->close();
    delete ui;
}

void MainWindow::log(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui->textLog->append(QString("[%1] %2").arg(timestamp, msg));
}

// ---------- CRC32 计算 ----------
quint32 MainWindow::calculateCRC32(const QByteArray &data)
{
    // static const quint32 crcTable[256] = {
    //     0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    //     0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    //     0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    //     0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    //     0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    //     0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    //     0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    //     0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    //     0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    //     0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    //     0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    //     0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    //     0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    //     0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    //     0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    //     0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    //     0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    //     0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    //     0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    //     0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    //     0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    //     0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    //     0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    //     0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB30A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    //     0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    //     0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    //     0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    //     0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    //     0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    //     0xAED16A4A, 0xD9D65ADC, 0x40BF0B66, 0x37B83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    //     0xBDBDF21C, 0xCABAC28A, 0x53839330, 0x2484B3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    //     0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    // };

    // quint32 crc = 0xFFFFFFFF;
    // const unsigned char *buf = reinterpret_cast<const unsigned char *>(data.constData());
    // int len = data.size();

    // for (int i = 0; i < len; ++i) {
    //     crc = (crc >> 8) ^ crcTable[(crc ^ buf[i]) & 0xFF];
    // }

    // return crc ^ 0xFFFFFFFF;

    quint32 crc = 0xFFFFFFFF;
    const unsigned char *buf = reinterpret_cast<const unsigned char *>(data.constData());
    int len = data.size();

    for (int i = 0; i < len; ++i) {
        crc ^= buf[i];  // 当前字节与 CRC 低8位异或

        // 逐位处理 8 个 bit
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {  // 检查最低位
                crc = (crc >> 1) ^ 0xEDB88320;  // 如果为1，右移后异或多项式
            } else {
                crc = (crc >> 1);  // 如果为0，只右移
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

// ---------- CRC16 Modbus 计算 (逐位算法，确保正确) ----------
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

// ---------- 生成唯一任务ID (4字节) ----------
quint32 MainWindow::generateTaskID()
{
    quint32 id = QRandomGenerator::global()->generate();
    return id;
}

// ---------- 辅助: 写LE uint16 (CRC16专用) ----------
static void appendU16LE(QByteArray &arr, quint16 val)
{
    arr.append(static_cast<char>(val & 0xFF));
    arr.append(static_cast<char>((val >> 8) & 0xFF));
}

// ---------- 辅助: 写BE uint16 ----------
static void appendU16BE(QByteArray &arr, quint16 val)
{
    arr.append(static_cast<char>((val >> 8) & 0xFF));
    arr.append(static_cast<char>(val & 0xFF));
}

// ---------- 辅助: 写LE uint32 (CRC32专用) ----------
static void appendU32LE(QByteArray &arr, quint32 val)
{
    for (int i = 0; i < 4; ++i)
        arr.append(static_cast<char>((val >> (i * 8)) & 0xFF));
}

// ---------- 辅助: 写BE uint32 ----------
static void appendU32BE(QByteArray &arr, quint32 val)
{
    for (int i = 3; i >= 0; --i)
        arr.append(static_cast<char>((val >> (i * 8)) & 0xFF));
}

// ---------- 1. 选择 .bin 文件 ----------
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

// ---------- 2. 拆包 -> 存入缓存 ----------
void MainWindow::onUnpack()
{
    if (m_selectedFile.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先选择一个Bin文件！"));
        return;
    }

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

// ---------- 3. 发送协商命令 + 验证ACK ----------
bool MainWindow::sendNegotiationCommand()
{
    // 版本号校验
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
    // 头部: 01 06 51 00
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x06));
    cmd.append(static_cast<char>(0x51));
    cmd.append(static_cast<char>(0x00));
    // A0(主标识段) + 01(数据块数量) + 01(子标识段:协商) + 01(是否有升级固件:默认01)
    cmd.append(static_cast<char>(0xA0));
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x01));
    cmd.append(static_cast<char>(0x01));
    // TaskID (4B BE)
    appendU32BE(cmd, m_taskID);
    // 版本号 (4B ASCII)
    cmd.append(versionBytes);
    // 固件大小 (4B BE)
    quint32 fileSize = static_cast<quint32>(m_fileData.size());
    appendU32BE(cmd, fileSize);
    // CRC32 (4B LE)
    appendU32LE(cmd, m_totalCRC32);
    // 数据长度 (1B): 从本字节之后到CRC16前
    // = 4(A0+01+01+01) + 4(TaskID) + 4(Version) + 4(FileSize) + 4(CRC32) = 20 = 0x14
    quint8 dataLen = static_cast<quint8>(4 + 4 + 4 + 4 + 4);
    cmd.insert(4, static_cast<char>(dataLen));
    // CRC16 (2B LE)
    quint16 cmdCRC16 = calculateCRC16(cmd);
    appendU16LE(cmd, cmdCRC16);

    // 打印
    log(QString::fromUtf8("---------- 发送协商命令 ----------"));
    log(QString::fromUtf8("协商命令 HEX: %1").arg(QString(cmd.toHex(' ').toUpper())));
    log(QString::fromUtf8("  头部: 01 06 51 00 | 数据长度: 0x%1 | A0 01 01 01(升级标志)")
            .arg(dataLen, 2, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  任务ID: 0x%1").arg(m_taskID, 8, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  版本号: %1 (ASCII: '%2')")
            .arg(QString(versionBytes.toHex(' ').toUpper()), QString::fromUtf8(versionBytes)));
    log(QString::fromUtf8("  固件大小: %1 字节 (0x%2)").arg(fileSize).arg(fileSize, 8, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  固件CRC32: 0x%1").arg(m_totalCRC32, 8, 16, QLatin1Char('0')));
    log(QString::fromUtf8("  CRC16校验: 0x%1").arg(cmdCRC16, 4, 16, QLatin1Char('0')));
    log(QString::fromUtf8("------------------------------------"));

    // 发送
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

    // 验证ACK
    if (!validateNegotiationACK(response)) {
        log(QString::fromUtf8("错误: 协商ACK验证失败！"));
        return false;
    }

    log(QString::fromUtf8("协商ACK验证通过，可进行下一步发包"));
    log(QString::fromUtf8("------------------------------------"));
    return true;
}

// ---------- 验证协商ACK (倒数第3字节 == 0x01) ----------
bool MainWindow::validateNegotiationACK(const QByteArray &response)
{
    if (response.size() < 3) {
        log(QString::fromUtf8("  ACK数据过短 (%1 字节)，无法验证").arg(response.size()));
        return false;
    }

    quint8 checkByte = static_cast<quint8>(response.at(response.size() - 3));
    log(QString::fromUtf8("  ACK验证: 倒数第3字节 = 0x%1 (%2)")
            .arg(checkByte, 2, 16, QLatin1Char('0'))
            .arg(checkByte == 0x01 ? QString::fromUtf8("通过") : QString::fromUtf8("失败,期望0x01")));

    return checkByte == 0x01;
}

// ---------- 发送一个子数据包 (寄存器 51 00 或 51 01) ----------
bool MainWindow::sendDataPacket(bool isFirstSubPacket, int subIndex, quint32 packetSeq, const QByteArray &data, quint32 taskID, quint16 totalPacketSize)
{
    QByteArray pkt;

    // 01 06
    pkt.append(static_cast<char>(0x01));
    pkt.append(static_cast<char>(0x06));
    // 寄存器 (2B, 大端): 51 00 或 51 01
    pkt.append(static_cast<char>(0x51));
    pkt.append(static_cast<char>(isFirstSubPacket ? 0x00 : 0x01));

    // 数据长度域: 从本字节之后到 CRC16 之前的长度
    // = 3(A0+01+02) + 4(TaskID) + 2(dataSize) + 1(status) + 4(packetSeq) + data
    quint8 dataLen = static_cast<quint8>(3 + 4 + 2 + 1 + 4 + data.size());
    pkt.append(static_cast<char>(dataLen));

    // A0(主标识段) + 01(数据块数量) + 02(子标识段:数据包子包)
    pkt.append(static_cast<char>(0xA0));
    pkt.append(static_cast<char>(0x01));
    pkt.append(static_cast<char>(0x02));

    // TaskID (4B BE)
    appendU32BE(pkt, taskID);
    // 本包固件大小: 两个子包的总大小 (2B BE)
    quint16 dataSize = totalPacketSize;
    appendU16BE(pkt, dataSize);
    // 请求固件状态 (1B) = 0x01
    pkt.append(static_cast<char>(0x01));
    // 固件包序号 (4B BE)
    appendU32BE(pkt, packetSeq);

    // 包数据
    pkt.append(data);

    // CRC16 (2B LE)
    quint16 pktCRC16 = calculateCRC16(pkt);
    appendU16LE(pkt, pktCRC16);

    QString regName = isFirstSubPacket ? QString::fromUtf8("51 00") : QString::fromUtf8("51 01");
    log(QString::fromUtf8("  >>> 发包: 寄存器=%1 | 子包=%2/2 | 序号=0x%3 | 数据大小=%4 | CRC16=0x%5 | 总长=%6")
            .arg(regName)
            .arg(subIndex + 1)
            .arg(packetSeq, 8, 16, QLatin1Char('0'))
            .arg(dataSize)
            .arg(pktCRC16, 4, 16, QLatin1Char('0'))
            .arg(pkt.size()));
    log(QString::fromUtf8("  [串口发送] %1").arg(QString(pkt.toHex(' ').toUpper())));

    qint64 written = m_serialPort->write(pkt);
    if (written < 0 || written != pkt.size()) {
        log(QString::fromUtf8("  错误: 发送失败 (已写入 %1 / %2)").arg(written).arg(pkt.size()));
        return false;
    }

    return true;
}

// ---------- 发送一个大包的两个子包 (带分包之间延时) ----------
bool MainWindow::sendBothSubPackets(int packetIndex, const QByteArray &bigPacket)
{
    int maxSubSize = (ui->spinChunkSize->value() + 1) / 2;
    int subSize1 = qMin(bigPacket.size(), maxSubSize);
    int subSize2 = bigPacket.size() - subSize1;
    QByteArray subData1 = bigPacket.left(subSize1);
    QByteArray subData2 = bigPacket.mid(subSize1, subSize2);
    quint32 packetSeq = static_cast<quint32>(packetIndex);
    quint16 totalSize = static_cast<quint16>(bigPacket.size());

    log(QString::fromUtf8("--- 发送固件包 %1/%2 (序号=%3) ---")
            .arg(packetIndex + 1).arg(m_packets.size()).arg(packetIndex));

    // 子包1 → 51 00
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

    // 子包与子包之间的延时（用 m_subPacketDelayMs）
    if (subData2.size() > 0 && m_subPacketDelayMs > 0) {
        QThread::msleep(m_subPacketDelayMs);
    }

    // 子包2 → 51 01
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

    return true;
}

// ---------- 4. 单次发送（点一下发一个包） ----------
void MainWindow::onSendSingle()
{
    if (m_packets.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先执行拆包操作！"));
        return;
    }
    if (!m_serialConnected) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先连接串口！"));
        return;
    }

    // 第一个包需要先协商
    if (m_currentPacketIndex == 0) {
        log(QString::fromUtf8("========== 单次发送流程开始 (共%1包) ==========").arg(m_packets.size()));
        if (!sendNegotiationCommand()) {
            log(QString::fromUtf8("协商命令失败，单次发送终止"));
            m_currentPacketIndex = 0;
            return;
        }
        log(QString::fromUtf8("等待1秒后开始发包..."));
        QThread::msleep(1000);
    }

    if (m_currentPacketIndex >= m_packets.size()) {
        log(QString::fromUtf8("所有 %1 个包已发送完毕").arg(m_packets.size()));
        m_currentPacketIndex = 0;
        return;
    }

    int i = m_currentPacketIndex;
    const QByteArray &bigPacket = m_packets[i];

    if (!sendBothSubPackets(i, bigPacket)) {
        m_currentPacketIndex = 0;
        return;
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

// ---------- 4b. 当前包发送（点一下发当前包，不递增序号） ----------
void MainWindow::onSendCurrent()
{
    if (m_packets.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先执行拆包操作！"));
        return;
    }
    if (!m_serialConnected) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先连接串口！"));
        return;
    }

    // 发送上一个单次发送已发过的包
    int i = qMax(0, m_currentPacketIndex - 1);
    if (i >= m_packets.size()) {
        log(QString::fromUtf8("所有 %1 个包已发送完毕，无法重复发送").arg(m_packets.size()));
        return;
    }

    // 需要协商（首次使用时任务ID为空）
    if (m_taskID == 0) {
        log(QString::fromUtf8("========== 重复发送当前包模式 (共%1包) ==========").arg(m_packets.size()));
        if (!sendNegotiationCommand()) {
            log(QString::fromUtf8("协商命令失败，终止"));
            return;
        }
        log(QString::fromUtf8("等待1秒后开始发送当前包..."));
        QThread::msleep(1000);
    }

    const QByteArray &bigPacket = m_packets[i];
    sendBothSubPackets(i, bigPacket);

    log(QString::fromUtf8("固件包 %1/%2 已重复发送").arg(i + 1).arg(m_packets.size()));
}

// ---------- 5. 一次性发送（协商 → 所有包连续发送 → 等整体ACK） ----------
void MainWindow::onSendBatch()
{
    if (m_packets.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先执行拆包操作！"));
        return;
    }
    if (!m_serialConnected) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先连接串口！"));
        return;
    }

    log(QString::fromUtf8("========== 一次性发送流程开始 (共%1包) ==========").arg(m_packets.size()));
    if (!sendNegotiationCommand()) {
        log(QString::fromUtf8("协商命令失败，一次性发送终止"));
        return;
    }

    log(QString::fromUtf8("等待1秒后开始一次性发送..."));
    QThread::msleep(1000);
    log(QString::fromUtf8("开始一次性发送，总固件包数: %1").arg(m_packets.size()));

    for (int i = 0; i < m_packets.size(); ++i) {
        if (!sendBothSubPackets(i, m_packets[i])) {
            return;
        }

        // 包与包之间的延时（用 m_interPacketDelayMs，最后一个包不加）
        if (i < m_packets.size() - 1 && m_interPacketDelayMs > 0) {
            log(QString::fromUtf8("  包间延时 %1 ms...").arg(m_interPacketDelayMs));
            QThread::msleep(m_interPacketDelayMs);
        }
    }

    log(QString::fromUtf8("所有 %1 个固件包已一次性发送完毕").arg(m_packets.size()));
    log(QString::fromUtf8("========== 一次性发送流程结束 =========="));
}

// ---------- 6. 刷新串口列表 ----------
void MainWindow::refreshSerialPorts()
{
    ui->comboSerialPort->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString display = info.portName();
        if (!info.description().isEmpty()) {
            display += QString(" - %1").arg(info.description());
        }
        ui->comboSerialPort->addItem(display, info.portName());
    }

    if (ports.isEmpty()) {
        log(QString::fromUtf8("串口检测: 未发现可用串口"));
    } else {
        log(QString::fromUtf8("串口检测: 发现 %1 个串口").arg(ports.size()));
    }
}

void MainWindow::onRefreshSerial()
{
    refreshSerialPorts();
}

// ---------- 7. 串口连接/断开 ----------
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

// ---------- 8. 串口接收数据（异步通知 → 存入缓存） ----------
void MainWindow::onSerialReadyRead()
{
    QByteArray data = m_serialPort->readAll();
    m_rxBuffer.append(data);
    log(QString::fromUtf8("[串口接收] %1").arg(QString(data.toHex(' ').toUpper())));
}

// ---------- 阻塞等待应答 ----------
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