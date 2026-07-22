#include "filetcpserver.h"

FileTcpServer::FileTcpServer(QObject *parent)
    : QTcpServer(parent)
{
}

void FileTcpServer::incomingConnection(qintptr socketDescriptor)
{
    emit incomingFileConnection(socketDescriptor);
}
