#ifndef MYSERVER_H
#define MYSERVER_H

#include <QWidget>
#include<QTcpServer>
#include<QTcpSocket>
#include <QNetworkInterface>
#include<QMessageBox>
#include<QDateTime>
#include <QThread>
#include"../../common/protocol/protocol.h"

class FileTcpServer;
class FileReceiverWorker;

QT_BEGIN_NAMESPACE
namespace Ui { class MyServer; }
QT_END_NAMESPACE

class MyServer : public QWidget
{
    Q_OBJECT

public:
    MyServer(QWidget *parent = nullptr);
    ~MyServer();
private slots:
    void on_listenStartBtn_clicked();

    void on_listenEndBtn_clicked();

    void on_sendMsgBtn_clicked();

private:
    void initDefaultData();      // 初始化默认 IP 和端口
    QString getLocalIPv4();      // 获取本机 IPv4 地址
    void handlePacket(const Protocol::Packet &packet);
    QString receiveDirPath() const;
    void startFileReceiver(qintptr socketDescriptor);
    void stopFileThreads();
private:
    bool flag = false;
    QTcpServer* m_server;
    FileTcpServer* m_fileServer;
    QTcpSocket* m_socket=nullptr;
    Ui::MyServer *ui;
    QByteArray revBuffer;
    QList<QThread*> fileThreads;
    QList<FileReceiverWorker*> fileWorkers;
};
#endif // MYSERVER_H
