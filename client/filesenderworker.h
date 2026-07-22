#ifndef FILESENDERWORKER_H
#define FILESENDERWORKER_H

#include <QObject>
#include <QFile>
#include <QTcpSocket>

#include "../../common/protocol/protocol.h"

class FileSenderWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileSenderWorker(QObject *parent = nullptr);

public slots:
    void start(const QString &host, quint16 port, const QString &filePath);
    void cancel();

signals:
    void progressChanged(int value);
    void message(const QString &text);
    void finished();

private:
    void sendFileInfo();
    void sendNextFileChunk();
    void handlePacket(const Protocol::Packet &packet);
    void finish();

private:
    QTcpSocket *socket = nullptr;
    QFile sendFile;
    QByteArray recvBuffer;
    QString selectedFilePath;
    qint64 sendFileSize = 0;
    qint64 sentBytes = 0;
    bool sendingFile = false;
    bool finishedEmitted = false;
};

#endif // FILESENDERWORKER_H
