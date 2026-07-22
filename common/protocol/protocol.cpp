#include "protocol.h"

#include <QDataStream>

QByteArray Protocol::pack(PacketType type, const QByteArray &body)
{
    QByteArray data;



    QDataStream out(&data, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_12);

    out << Magic;
    out << Version;
    out << static_cast<quint16>(type);
    out << static_cast<quint32>(body.size());

    data.append(body);
    return data;
}

bool Protocol::tryTakePacket(QByteArray &buffer, Packet &packet)
{
    if (buffer.size() < HeaderSize)
        return false;

    QDataStream in(buffer);
    in.setByteOrder(QDataStream::BigEndian);
    in.setVersion(QDataStream::Qt_5_12);

    quint32 magic = 0;
    quint16 version = 0;
    quint16 type = 0;
    quint32 bodySize = 0;

    in >> magic >> version >> type >> bodySize;

    if (magic != Magic || version != Version)
    {
        buffer.clear();
        return false;
    }

    const int packetSize = HeaderSize + static_cast<int>(bodySize);
    if (buffer.size() < packetSize)
        return false;

    packet.type = static_cast<PacketType>(type);
    packet.body = buffer.mid(HeaderSize, bodySize);

    buffer.remove(0, packetSize);
    return true;
}
