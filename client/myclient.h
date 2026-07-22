#ifndef MYCLIENT_H
#define MYCLIENT_H

#include <QWidget>
#include<QTcpSocket>
#include <QNetworkInterface>
#include<QDateTime>
#include <QThread>
#include"../../common/protocol/protocol.h"

class FileSenderWorker;

QT_BEGIN_NAMESPACE
namespace Ui { class MyClient; }
QT_END_NAMESPACE

class MyClient : public QWidget
{
    Q_OBJECT

public:
    MyClient(QWidget *parent = nullptr);
    ~MyClient();
private slots:
    void on_serverConnectBtn_clicked();

    void on_serverDisconnectBtn_clicked();

    void on_sendMsgBtn_clicked();

    void on_fileSelectBtn_clicked();

    void on_fileSendBtn_clicked();

private:
    void initDefaultData();      // 初始化默认 IP 和端口
    QString getLocalIPv4();      // 获取本机 IPv4 地址
    void handlePacket(const Protocol::Packet &packet);
    void startFileTransfer();
    void cancelFileTransfer();
private:
    QTcpSocket* socket;
    Ui::MyClient *ui;
    QByteArray recvBuffer;
    QString selectedFilePath;
    qint64 sendFileSize = 0;
    QThread *fileThread = nullptr;
    FileSenderWorker *fileWorker = nullptr;
    bool fileTransferRunning = false;
};
#endif // MYCLIENT_H
