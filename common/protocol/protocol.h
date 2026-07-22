#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QByteArray>
#include <QtGlobal>

namespace Protocol
{
    constexpr quint32 Magic = 0x43534654; // CSFT
    constexpr quint16 Version = 1;
    constexpr int HeaderSize = 12;

    enum class PacketType : quint16
    {
        Text = 1,
        FileInfo = 2,
        FileChunk = 3,
        FileEnd = 4,
        FileResume = 5
    };

    struct Packet
    {
        PacketType type;
        QByteArray body;
    };

    QByteArray pack(PacketType type, const QByteArray &body);
    bool tryTakePacket(QByteArray &buffer, Packet &packet);
}

#endif
