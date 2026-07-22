#include "myserver.h"
#include "ui_myserver.h"
#include "filetcpserver.h"
#include "filereceiverworker.h"

#include <QCoreApplication>

MyServer::MyServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MyServer)
{
    ui->setupUi(this);
    initDefaultData();
    m_server = new QTcpServer(this);
    m_fileServer = new FileTcpServer(this);

    connect(m_fileServer, &FileTcpServer::incomingFileConnection,
            this, &MyServer::startFileReceiver);

    connect(ui->sendMsgList,&QTextEdit::textChanged,this,[=](){
        QString content = ui->sendMsgList->toPlainText().trimmed();
        if(content.isEmpty()||!flag)
        {
            ui->sendMsgBtn->setEnabled(false);
            ui->sendMsgClear->setEnabled(false);
        }else
        {
            ui->sendMsgBtn->setEnabled(true);
            ui->sendMsgClear->setEnabled(true);
        }


    });
    connect(m_server,&QTcpServer::newConnection,this,[=](){
        m_socket = m_server->nextPendingConnection();
        revBuffer.clear();
        ui->msgShowList->append("********服务器与客户端连接成功*******");
        flag = true;
        ui->sendMsgList->clear();
        ui->sendMsgList->setEnabled(true);
        ui->msgShowList->append(QString("客户端IP地址:%1，端口号:%2")
                                .arg(m_socket->peerAddress().toString())
                                .arg(m_socket->peerPort()));
        //socket收到消息的处理
        connect(m_socket,&QTcpSocket::readyRead,this,[=](){
            revBuffer.append(m_socket->readAll());
            Protocol::Packet packet;
            while(Protocol::tryTakePacket(revBuffer,packet))
            {
                handlePacket(packet);
            }
        });
        //socket断开的处理
        connect(m_socket,&QTcpSocket::disconnected,this,[=](){
             flag = false;
             ui->sendMsgList->clear();
             ui->sendMsgList->setEnabled(false);
             ui->sendMsgBtn->setEnabled(false);
             ui->sendMsgClear->setEnabled(false);
             ui->msgShowList->append(QString("[客户端断开] IP: %1  Port: %2")
                                     .arg(m_socket->peerAddress().toString())
                                     .arg(m_socket->peerPort()));
            m_socket->deleteLater();
            m_socket = nullptr;
        });
    });
}

void MyServer::initDefaultData()
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
    ui->listenStartBtn->setEnabled(true);
    ui->listenEndBtn->setEnabled(false);
}
QString MyServer::getLocalIPv4()
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
MyServer::~MyServer()
{
    stopFileThreads();
    delete ui;
}


void MyServer::on_listenStartBtn_clicked()
{
    QString ip = ui->serverIP->currentText().trimmed();
    quint16 port = ui->serverPort->value();



    QHostAddress listenAddress;

    if (ip == "0.0.0.0")
    {
        listenAddress = QHostAddress::AnyIPv4;
    }
    else
    {
        listenAddress = QHostAddress(ip);
    }

    if (port == 65535)
    {
        QMessageBox::warning(this, "监听失败", "文件传输端口使用监听端口 + 1，请把监听端口设置为 65534 或更小");
        return;
    }

    bool ret = m_server->listen(listenAddress, port);

    if (!ret)
    {
        QMessageBox::warning(this, "监听失败", m_server->errorString());
        return;
    }

    bool fileRet = m_fileServer->listen(listenAddress, static_cast<quint16>(port + 1));
    if (!fileRet)
    {
        m_server->close();
        QMessageBox::warning(this, "文件监听失败", m_fileServer->errorString());
        return;
    }

    ui->listenStartBtn->setEnabled(false);
    ui->listenEndBtn->setEnabled(true);

    ui->serverIP->setEnabled(false);
    ui->serverPort->setEnabled(false);

    ui->msgShowList->append(
        QString("[服务器启动监听] 消息端口: %1:%2  文件端口: %1:%3")
        .arg(ip)
        .arg(port)
        .arg(port + 1)
    );
}

void MyServer::on_listenEndBtn_clicked()
{
    if(m_server->isListening())
    {
        m_server->close();
    }
    if(m_fileServer->isListening())
    {
        m_fileServer->close();
    }
    stopFileThreads();
    if(m_socket!=nullptr)
    {
        m_socket->disconnectFromHost();
    }
    ui->listenStartBtn->setEnabled(true);
    ui->listenEndBtn->setEnabled(false);

    ui->serverIP->setEnabled(true);
    ui->serverPort->setEnabled(true);

    ui->msgShowList->append(QString("[服务器关闭监听]"));
}

void MyServer::on_sendMsgBtn_clicked()
{
    QString msg = ui->sendMsgList->toPlainText();
    qint64 ret =  m_socket->write(Protocol::pack(Protocol::PacketType::Text, msg.toUtf8()));
    if(ret==-1)
    {
        ui->msgShowList->append("[错误] 消息发送失败：" + m_socket->errorString());
                return;
    }
    QString date = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    ui->msgShowList->append(QString("[服务器消息] %1\n %2")
                            .arg(date)
                            .arg(msg));
    ui->sendMsgList->clear();
}

void MyServer::handlePacket(const Protocol::Packet &packet)
{
    if(packet.type==Protocol::PacketType::Text)
    {
        QString revmsg = QString::fromUtf8(packet.body);
        QString time = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
        ui->msgShowList->append(QString("[客户端消息%1 %2]\n %3")
                                .arg(m_socket->peerAddress().toString())
                                .arg(time)
                                .arg(revmsg));
        return;
    }

    if (packet.type == Protocol::PacketType::FileInfo)
    {
        ui->msgShowList->append("[文件] 文件传输已改为独立线程端口，请使用文件传输连接发送");
        return;
    }

    if (packet.type == Protocol::PacketType::FileChunk)
    {
        ui->msgShowList->append("[文件] 收到主消息连接上的文件块，已忽略");
        return;
    }

    if (packet.type == Protocol::PacketType::FileEnd)
    {
        ui->msgShowList->append("[文件] 收到主消息连接上的文件结束包，已忽略");
    }
}

QString MyServer::receiveDirPath() const
{
    return QCoreApplication::applicationDirPath() + "/received";
}

void MyServer::startFileReceiver(qintptr socketDescriptor)
{
    QThread *thread = new QThread(this);
    FileReceiverWorker *worker = new FileReceiverWorker(socketDescriptor, receiveDirPath());
    worker->moveToThread(thread);
    fileThreads.append(thread);
    fileWorkers.append(worker);

    connect(thread, &QThread::started, worker, &FileReceiverWorker::start);
    connect(worker, &FileReceiverWorker::progressChanged, this, [=](int value) {
        ui->fileProgressBar->setValue(value);
    });
    connect(worker, &FileReceiverWorker::message, this, [=](const QString &text) {
        ui->msgShowList->append(text);
    });
    connect(worker, &FileReceiverWorker::finished, thread, &QThread::quit);
    connect(worker, &FileReceiverWorker::finished, worker, &FileReceiverWorker::deleteLater);
    connect(thread, &QThread::finished, this, [=]() {
        fileThreads.removeAll(thread);
        fileWorkers.removeAll(worker);
        thread->deleteLater();
    });

    thread->start();
}

void MyServer::stopFileThreads()
{
    const QList<FileReceiverWorker*> workers = fileWorkers;
    for (FileReceiverWorker *worker : workers)
        QMetaObject::invokeMethod(worker, "cancel", Qt::QueuedConnection);

    const QList<QThread*> threads = fileThreads;
    for (QThread *thread : threads)
    {
        thread->quit();
        thread->wait(3000);
    }
    fileThreads.clear();
}
