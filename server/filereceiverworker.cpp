#include "filereceiverworker.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>

FileReceiverWorker::FileReceiverWorker(qintptr socketDescriptor, const QString &receiveDirPath, QObject *parent)
    : QObject(parent)
    , socketDescriptor(socketDescriptor)
    , dirPath(receiveDirPath)
{
}

void FileReceiverWorker::start()
{
    socket = new QTcpSocket(this);

    if (!socket->setSocketDescriptor(socketDescriptor))
    {
        emit message("[文件线程] 接管文件连接失败：" + socket->errorString());
        finish();
        return;
    }

    connect(socket, &QTcpSocket::readyRead, this, [=]() {
        recvBuffer.append(socket->readAll());

        Protocol::Packet packet;
        while (Protocol::tryTakePacket(recvBuffer, packet))
            handlePacket(packet);
    });

    connect(socket, &QTcpSocket::disconnected, this, [=]() {
        closeReceiveFile();
        emit message("[文件线程] 文件连接已断开");
        finish();
    });

    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, [=](QAbstractSocket::SocketError) {
        closeReceiveFile();
        emit message("[文件线程] 连接错误：" + socket->errorString());
        finish();
    });

    emit message(QString("[文件线程] 文件客户端已连接：%1:%2")
                 .arg(socket->peerAddress().toString())
                 .arg(socket->peerPort()));
}

void FileReceiverWorker::cancel()
{
    closeReceiveFile();

    if (socket)
        socket->disconnectFromHost();
    else
        finish();
}

void FileReceiverWorker::handlePacket(const Protocol::Packet &packet)
{
    if (packet.type == Protocol::PacketType::FileInfo)
    {
        handleFileInfo(packet.body);
        return;
    }

    if (packet.type == Protocol::PacketType::FileChunk)
    {
        handleFileChunk(packet.body);
        return;
    }

    if (packet.type == Protocol::PacketType::FileEnd)
        handleFileEnd(packet.body);
}

void FileReceiverWorker::handleFileInfo(const QByteArray &body)
{
    QDataStream in(body);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_12);

    QString fileName;
    qint64 fileSize = 0;
    qint64 lastModifiedMsecs = 0;
    in >> fileName >> fileSize >> lastModifiedMsecs;

    fileName = QFileInfo(fileName).fileName();
    if (fileName.isEmpty() || fileSize < 0 || lastModifiedMsecs <= 0)
    {
        emit message("[文件线程] 文件信息无效");
        return;
    }

    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    closeReceiveFile();

    receivingFileName = fileName;
    receivingFileSize = fileSize;
    receivingLastModifiedMsecs = lastModifiedMsecs;
    receivingFinalPath = dir.filePath(fileName);
    receivingPartPath = receivingFinalPath + ".part";

    qint64 oldFileSize = 0;
    qint64 oldLastModifiedMsecs = 0;
    if (readReceiveMeta(&oldFileSize, &oldLastModifiedMsecs) &&
        (oldLastModifiedMsecs != receivingLastModifiedMsecs || oldFileSize != receivingFileSize))
    {
        QFile::remove(receivingFinalPath);
        QFile::remove(receivingPartPath);
        QFile::remove(receiveMetaPath());
        emit message(QString("[文件线程] 检测到源文件已更新，重新从 0 开始接收：%1").arg(fileName));
    }

    QFileInfo finalInfo(receivingFinalPath);
    QFileInfo partInfo(receivingPartPath);

    if (finalInfo.exists() && finalInfo.size() == fileSize)
    {
        receivedBytes = fileSize;
    }
    else
    {
        receivedBytes = partInfo.exists() ? partInfo.size() : 0;
        if (receivedBytes > fileSize)
        {
            QFile::remove(receivingPartPath);
            QFile::remove(receiveMetaPath());
            receivedBytes = 0;
        }

        receiveFile.setFileName(receivingPartPath);
        if (!receiveFile.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            emit message("[文件线程] 打开接收文件失败：" + receiveFile.errorString());
            return;
        }
    }

    writeReceiveMeta();

    emit progressChanged(fileSize == 0 ? 0 : static_cast<int>(receivedBytes * 100 / fileSize));
    emit message(QString("[文件线程] 准备接收：%1，大小：%2 字节，已接收：%3 字节")
                 .arg(fileName)
                 .arg(fileSize)
                 .arg(receivedBytes));

    QByteArray replyBody;
    QDataStream out(&replyBody, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_12);
    out << fileName << receivedBytes;

    socket->write(Protocol::pack(Protocol::PacketType::FileResume, replyBody));
}

void FileReceiverWorker::handleFileChunk(const QByteArray &body)
{
    QDataStream in(body);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_12);

    QString fileName;
    qint64 offset = 0;
    QByteArray chunk;
    in >> fileName >> offset >> chunk;
    fileName = QFileInfo(fileName).fileName();

    if (fileName != receivingFileName)
    {
        emit message("[文件线程] 收到的文件块名称不匹配，已忽略");
        return;
    }

    if (!receiveFile.isOpen())
    {
        receiveFile.setFileName(receivingPartPath);
        if (!receiveFile.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            emit message("[文件线程] 继续写入文件失败：" + receiveFile.errorString());
            return;
        }
    }

    if (offset != receivedBytes)
    {
        emit message(QString("[文件线程] 文件块偏移不匹配，期望 %1，收到 %2")
                     .arg(receivedBytes)
                     .arg(offset));
        return;
    }

    if (receivedBytes + chunk.size() > receivingFileSize)
    {
        emit message("[文件线程] 文件块超过声明大小，已忽略");
        return;
    }

    qint64 written = receiveFile.write(chunk);
    if (written != chunk.size())
    {
        emit message("[文件线程] 写入文件失败：" + receiveFile.errorString());
        return;
    }

    receivedBytes += written;
    emit progressChanged(receivingFileSize == 0 ? 100 : static_cast<int>(receivedBytes * 100 / receivingFileSize));
}

void FileReceiverWorker::handleFileEnd(const QByteArray &body)
{
    QDataStream in(body);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_12);

    QString fileName;
    qint64 fileSize = 0;
    in >> fileName >> fileSize;
    fileName = QFileInfo(fileName).fileName();

    if (fileName != receivingFileName || fileSize != receivingFileSize)
    {
        emit message("[文件线程] 文件结束信息不匹配");
        return;
    }

    closeReceiveFile();

    if (receivedBytes != receivingFileSize)
    {
        emit message(QString("[文件线程] 传输未完成，已保留临时文件：%1/%2 字节")
                     .arg(receivedBytes)
                     .arg(receivingFileSize));
        return;
    }

    if (!QFileInfo(receivingPartPath).exists())
    {
        if (QFileInfo(receivingFinalPath).exists() && QFileInfo(receivingFinalPath).size() == receivingFileSize)
        {
            writeReceiveMeta();
            emit progressChanged(100);
            emit message(QString("[文件线程] 文件已存在且完整：%1").arg(receivingFinalPath));
        }
        else
        {
            emit message("[文件线程] 临时文件不存在，无法完成保存");
        }
        return;
    }

    QFile::remove(receivingFinalPath);
    if (QFile::rename(receivingPartPath, receivingFinalPath))
    {
        writeReceiveMeta();
        emit progressChanged(100);
        emit message(QString("[文件线程] 接收完成：%1").arg(receivingFinalPath));
    }
    else if (QFileInfo(receivingFinalPath).exists() && QFileInfo(receivingFinalPath).size() == receivingFileSize)
    {
        writeReceiveMeta();
        emit progressChanged(100);
        emit message(QString("[文件线程] 文件已存在且完整：%1").arg(receivingFinalPath));
    }
    else
    {
        emit message("[文件线程] 临时文件改名失败");
    }

    if (socket)
        socket->disconnectFromHost();
}

QString FileReceiverWorker::receiveMetaPath() const
{
    return receivingFinalPath + ".meta";
}

void FileReceiverWorker::closeReceiveFile()
{
    if (receiveFile.isOpen())
    {
        receiveFile.flush();
        receiveFile.close();
    }
}

bool FileReceiverWorker::readReceiveMeta(qint64 *fileSize, qint64 *lastModifiedMsecs) const
{
    QFile metaFile(receiveMetaPath());
    if (!metaFile.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&metaFile);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_12);
    in >> *fileSize >> *lastModifiedMsecs;

    return in.status() == QDataStream::Ok;
}

void FileReceiverWorker::writeReceiveMeta() const
{
    if (receivingFinalPath.isEmpty())
        return;

    QFile metaFile(receiveMetaPath());
    if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    QDataStream out(&metaFile);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_12);
    out << receivingFileSize << receivingLastModifiedMsecs;
}

void FileReceiverWorker::finish()
{
    closeReceiveFile();

    if (finishedEmitted)
        return;

    finishedEmitted = true;
    emit finished();
}
