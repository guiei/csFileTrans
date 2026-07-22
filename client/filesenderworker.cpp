#include "filesenderworker.h"

#include <QDataStream>
#include <QDateTime>
#include <QFileInfo>

namespace
{
const qint64 ChunkSize = 64 * 1024;
const qint64 MaxPendingBytes = ChunkSize * 8;
}

FileSenderWorker::FileSenderWorker(QObject *parent)
    : QObject(parent)
{
}

void FileSenderWorker::start(const QString &host, quint16 port, const QString &filePath)
{
    selectedFilePath = filePath;
    finishedEmitted = false;

    socket = new QTcpSocket(this);

    connect(socket, &QTcpSocket::connected, this, [=]() {
        emit message(QString("[文件线程] 已连接文件传输端口 %1:%2").arg(host).arg(port));
        sendFileInfo();
    });

    connect(socket, &QTcpSocket::readyRead, this, [=]() {
        recvBuffer.append(socket->readAll());

        Protocol::Packet packet;
        while (Protocol::tryTakePacket(recvBuffer, packet))
            handlePacket(packet);
    });

    connect(socket, &QTcpSocket::bytesWritten, this, [=](qint64) {
        sendNextFileChunk();
    });

    connect(socket, &QTcpSocket::disconnected, this, [=]() {
        if (sendingFile)
            emit message("[文件线程] 文件连接已断开，未完成部分可下次续传");
        finish();
    });

    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, [=](QAbstractSocket::SocketError) {
        emit message("[文件线程] 连接错误：" + socket->errorString());
        finish();
    });

    socket->connectToHost(host, port);
}

void FileSenderWorker::cancel()
{
    sendingFile = false;
    if (sendFile.isOpen())
        sendFile.close();

    if (socket)
        socket->disconnectFromHost();
    else
        finish();
}

void FileSenderWorker::sendFileInfo()
{
    QFileInfo info(selectedFilePath);
    if (!info.exists() || !info.isFile())
    {
        emit message("[文件线程] 文件不存在，无法发送");
        finish();
        return;
    }

    sendFileSize = info.size();
    sentBytes = 0;
    qint64 lastModifiedMsecs = info.lastModified().toMSecsSinceEpoch();

    QByteArray body;
    QDataStream out(&body, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_12);
    out << info.fileName() << sendFileSize << lastModifiedMsecs;

    socket->write(Protocol::pack(Protocol::PacketType::FileInfo, body));
    emit message(QString("[文件线程] 已发送文件信息，等待续传位置：%1").arg(info.fileName()));
}

void FileSenderWorker::handlePacket(const Protocol::Packet &packet)
{
    if (packet.type != Protocol::PacketType::FileResume)
        return;

    QDataStream in(packet.body);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_12);

    QString fileName;
    qint64 offset = 0;
    in >> fileName >> offset;

    QFileInfo info(selectedFilePath);
    if (info.fileName() != fileName)
    {
        emit message("[文件线程] 服务器返回的文件名不匹配，停止发送");
        finish();
        return;
    }

    sendFile.setFileName(selectedFilePath);
    if (!sendFile.open(QIODevice::ReadOnly))
    {
        emit message("[文件线程] 打开文件失败：" + sendFile.errorString());
        finish();
        return;
    }

    if (offset < 0 || offset > sendFile.size())
    {
        emit message("[文件线程] 服务器返回的续传位置无效，停止发送");
        finish();
        return;
    }

    sendFile.seek(offset);
    sendFileSize = sendFile.size();
    sentBytes = offset;
    sendingFile = true;
    emit progressChanged(sendFileSize == 0 ? 100 : static_cast<int>(sentBytes * 100 / sendFileSize));
    emit message(QString("[文件线程] 从 %1 字节处继续发送：%2").arg(sentBytes).arg(fileName));

    sendNextFileChunk();
}

void FileSenderWorker::sendNextFileChunk()
{
    if (!sendingFile || !sendFile.isOpen() || !socket)
        return;

    while (socket->state() == QAbstractSocket::ConnectedState &&
           socket->bytesToWrite() < MaxPendingBytes &&
           !sendFile.atEnd())
    {
        qint64 offset = sendFile.pos();
        QByteArray chunk = sendFile.read(ChunkSize);
        if (chunk.isEmpty())
            break;

        QByteArray body;
        QDataStream out(&body, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out.setVersion(QDataStream::Qt_5_12);
        out << QFileInfo(selectedFilePath).fileName() << offset << chunk;

        socket->write(Protocol::pack(Protocol::PacketType::FileChunk, body));
        sentBytes += chunk.size();
        emit progressChanged(sendFileSize == 0 ? 100 : static_cast<int>(sentBytes * 100 / sendFileSize));
    }

    if (!sendFile.atEnd())
        return;

    QByteArray body;
    QDataStream out(&body, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_12);
    out << QFileInfo(selectedFilePath).fileName() << sendFileSize;

    socket->write(Protocol::pack(Protocol::PacketType::FileEnd, body));
    socket->flush();
    sendingFile = false;
    emit progressChanged(100);
    emit message(QString("[文件线程] 发送完成：%1").arg(QFileInfo(selectedFilePath).fileName()));

    if (sendFile.isOpen())
        sendFile.close();
    socket->disconnectFromHost();
}

void FileSenderWorker::finish()
{
    sendingFile = false;
    if (sendFile.isOpen())
        sendFile.close();

    if (finishedEmitted)
        return;

    finishedEmitted = true;
    emit finished();
}
