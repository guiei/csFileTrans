#ifndef FILERECEIVERWORKER_H
#define FILERECEIVERWORKER_H

#include <QObject>
#include <QFile>
#include <QTcpSocket>

#include "../../common/protocol/protocol.h"

class FileReceiverWorker : public QObject
{
    Q_OBJECT

public:
    FileReceiverWorker(qintptr socketDescriptor, const QString &receiveDirPath, QObject *parent = nullptr);

public slots:
    void start();
    void cancel();

signals:
    void progressChanged(int value);
    void message(const QString &text);
    void finished();

private:
    void handlePacket(const Protocol::Packet &packet);
    void handleFileInfo(const QByteArray &body);
    void handleFileChunk(const QByteArray &body);
    void handleFileEnd(const QByteArray &body);
    QString receiveMetaPath() const;
    void closeReceiveFile();
    bool readReceiveMeta(qint64 *fileSize, qint64 *lastModifiedMsecs) const;
    void writeReceiveMeta() const;
    void finish();

private:
    qintptr socketDescriptor;
    QString dirPath;
    QTcpSocket *socket = nullptr;
    QByteArray recvBuffer;
    QFile receiveFile;
    QString receivingFileName;
    QString receivingPartPath;
    QString receivingFinalPath;
    qint64 receivingLastModifiedMsecs = 0;
    qint64 receivingFileSize = 0;
    qint64 receivedBytes = 0;
    bool finishedEmitted = false;
};

#endif // FILERECEIVERWORKER_H
