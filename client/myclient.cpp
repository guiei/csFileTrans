#include "myclient.h"
#include "ui_myclient.h"
#include "filesenderworker.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

MyClient::MyClient(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MyClient)
{
    ui->setupUi(this);

    socket=new QTcpSocket(this);
    initDefaultData();
    connect(ui->sendMsgList,&QTextEdit::textChanged,this,[=](){
        QString content = ui->sendMsgList->toPlainText().trimmed();
        if(content.isEmpty())
        {
            ui->sendMsgBtn->setEnabled(false);
            ui->clearMsgBtn->setEnabled(false);
        }else
        {
            ui->sendMsgBtn->setEnabled(true);
            ui->clearMsgBtn->setEnabled(true);
        }


    });
    connect(socket, &QTcpSocket::connected, this, [=]()
       {
           ui->msgShowList->append("[客户端] 连接服务器成功");
           recvBuffer.clear();
           ui->sendMsgList->setEnabled(true);
           ui->fileSelectBtn->setEnabled(true);
           ui->fileSendBtn->setEnabled(!selectedFilePath.isEmpty());

           ui->serverConnectBtn->setEnabled(false);
           ui->serverDisconnectBtn->setEnabled(true);
       });

    connect(socket, &QTcpSocket::disconnected, this, [=]()
       {
           ui->msgShowList->append("[客户端] 已断开服务器连接");
            recvBuffer.clear();
            cancelFileTransfer();
            ui->sendMsgList->clear();
            ui->sendMsgList->setEnabled(false);
           ui->fileSelectBtn->setEnabled(false);
           ui->fileSendBtn->setEnabled(false);
           ui->serverConnectBtn->setEnabled(true);
           ui->serverDisconnectBtn->setEnabled(false);
       });

    connect(socket, &QTcpSocket::readyRead, this, [=]()
       {
           recvBuffer.append(socket->readAll());

           Protocol::Packet packet;
           while (Protocol::tryTakePacket(recvBuffer, packet))
           {
               handlePacket(packet);
           }
       });

//       connect(socket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError)
//       {
//           ui->msgShowList->append("[连接错误] " + socket->errorString());

//           ui->serverConnectBtn->setEnabled(true);
//           ui->serverDisconnectBtn->setEnabled(false);
//       });

}

void MyClient::initDefaultData()
{
    QString localIp = getLocalIPv4();
//    qDebug()<<localIp;
    // 在 localIP 这个 QLabel 中显示服务器本机 IP
    ui->localIP->setText(localIp);

    // serverIP 默认数据
    ui->serverIP->clear();
    ui->serverIP->addItem("0.0.0.0");      // 监听所有网卡，最常用
    ui->serverIP->addItem("127.0.0.1");    // 只允许本机连接
    ui->serverIP->addItem(localIp);         // 局域网 IP

    ui->serverIP->setCurrentText("0.0.0.0");


    // 初始按钮状态
    ui->serverConnectBtn->setEnabled(true);
    ui->serverDisconnectBtn->setEnabled(false);
    ui->fileSelectBtn->setEnabled(false);
    ui->fileSendBtn->setEnabled(false);
    ui->fileProgressBar->setValue(0);
}
QString MyClient::getLocalIPv4()
{
    QList<QHostAddress> addressList = QNetworkInterface::allAddresses();

    for (const QHostAddress &address : addressList)
    {
        if (address.protocol() == QAbstractSocket::IPv4Protocol &&
            address != QHostAddress::LocalHost)
        {
            return address.toString();
        }
    }

    return "127.0.0.1";
}

MyClient::~MyClient()
{
    cancelFileTransfer();
    if (fileThread)
    {
        fileThread->quit();
        fileThread->wait(3000);
    }
    delete ui;
}


void MyClient::on_serverConnectBtn_clicked()
{
    QString ip =ui->serverIP->currentText().trimmed();
    quint16 port = ui->serverPort->value();
    QHostAddress serverAddress;
    if(ip=="0.0.0.0")
    {
        serverAddress = QHostAddress::AnyIPv4;
    }else
    {
        serverAddress = QHostAddress(ip);
    }
    socket->connectToHost(serverAddress,port);
    ui->msgShowList->append(
            QString("[客户端] 正在连接服务器 %1:%2 ...")
                .arg(ip)
                .arg(port)
        );
    ui->serverConnectBtn->setEnabled(false);
    ui->serverDisconnectBtn->setEnabled(true);
}

void MyClient::on_serverDisconnectBtn_clicked()
{
    socket->disconnectFromHost();
    ui->msgShowList->append("[客户端] 正在断开服务器连接...");
}

void MyClient::on_sendMsgBtn_clicked()
{
    QString data = ui->sendMsgList->toPlainText();
    QByteArray packet = Protocol::pack(Protocol::PacketType::Text,data.toUtf8());
    socket->write(packet);
    ui->sendMsgList->clear();
}

void MyClient::on_fileSelectBtn_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择要发送的文件");
    if (filePath.isEmpty())
        return;

    QFileInfo info(filePath);
    selectedFilePath = filePath;
    sendFileSize = info.size();
    ui->fileProgressBar->setValue(0);
    ui->fileSendBtn->setEnabled(socket->state() == QAbstractSocket::ConnectedState);
    ui->msgShowList->append(QString("[文件] 已选择：%1，大小：%2 字节")
                            .arg(info.fileName())
                            .arg(sendFileSize));
}

void MyClient::on_fileSendBtn_clicked()
{
    if (socket->state() != QAbstractSocket::ConnectedState)
    {
        QMessageBox::warning(this, "发送失败", "请先连接服务器");
        return;
    }

    if (selectedFilePath.isEmpty())
    {
        QMessageBox::warning(this, "发送失败", "请先选择文件");
        return;
    }

    startFileTransfer();
}

void MyClient::handlePacket(const Protocol::Packet &packet)
{
    if (packet.type == Protocol::PacketType::Text)
    {
        QString msg = QString::fromUtf8(packet.body);
        QString time = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
        ui->msgShowList->append(QString("[服务器消息] %1\n%2")
                                .arg(time)
                                .arg(msg));
        return;
    }
}

void MyClient::startFileTransfer()
{
    QFileInfo info(selectedFilePath);
    if (!info.exists() || !info.isFile())
    {
        QMessageBox::warning(this, "发送失败", "文件不存在");
        return;
    }

    if (fileTransferRunning)
    {
        ui->msgShowList->append("[文件] 当前已有文件正在传输");
        return;
    }

    quint16 msgPort = ui->serverPort->value();
    if (msgPort == 65535)
    {
        QMessageBox::warning(this, "发送失败", "文件传输端口使用服务器端口 + 1，请把服务器端口设置为 65534 或更小");
        return;
    }

    QString host = ui->serverIP->currentText().trimmed();
    if (host == "0.0.0.0")
        host = "127.0.0.1";
    quint16 filePort = static_cast<quint16>(msgPort + 1);

    fileThread = new QThread(this);
    fileWorker = new FileSenderWorker;
    fileWorker->moveToThread(fileThread);
    fileTransferRunning = true;
    ui->fileSendBtn->setEnabled(false);
    ui->fileSelectBtn->setEnabled(false);
    ui->fileProgressBar->setValue(0);

    connect(fileThread, &QThread::started, fileWorker, [=]() {
        fileWorker->start(host, filePort, selectedFilePath);
    });
    connect(fileWorker, &FileSenderWorker::progressChanged, this, [=](int value) {
        ui->fileProgressBar->setValue(value);
    });
    connect(fileWorker, &FileSenderWorker::message, this, [=](const QString &text) {
        ui->msgShowList->append(text);
    });
    connect(fileWorker, &FileSenderWorker::finished, fileThread, &QThread::quit);
    connect(fileWorker, &FileSenderWorker::finished, fileWorker, &FileSenderWorker::deleteLater);
    connect(fileThread, &QThread::finished, this, [=]() {
        fileThread->deleteLater();
        fileThread = nullptr;
        fileWorker = nullptr;
        fileTransferRunning = false;
        ui->fileSelectBtn->setEnabled(socket->state() == QAbstractSocket::ConnectedState);
        ui->fileSendBtn->setEnabled(socket->state() == QAbstractSocket::ConnectedState && !selectedFilePath.isEmpty());
    });

    ui->msgShowList->append(QString("[文件] 启动文件传输线程，连接端口：%1").arg(filePort));
    fileThread->start();
}

void MyClient::cancelFileTransfer()
{
    if (fileWorker)
        QMetaObject::invokeMethod(fileWorker, "cancel", Qt::QueuedConnection);
}
