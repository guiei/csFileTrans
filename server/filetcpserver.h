#ifndef FILETCPSERVER_H
#define FILETCPSERVER_H

#include <QTcpServer>

class FileTcpServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit FileTcpServer(QObject *parent = nullptr);

signals:
    void incomingFileConnection(qintptr socketDescriptor);

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};

#endif // FILETCPSERVER_H
