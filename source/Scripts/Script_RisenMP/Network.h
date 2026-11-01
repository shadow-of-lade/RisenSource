#ifndef RISENMP_NETWORK_H_INCLUDED
#define RISENMP_NETWORK_H_INCLUDED

#include <winsock2.h>
#include <windows.h>

namespace RisenMP
{
    enum
    {
        MaxPlayers = 4,
        DefaultPort = 28000,
        ProtocolVersion = 1
    };

    enum PacketType
    {
        PacketHello = 1,
        PacketWelcome = 2,
        PacketReject = 3,
        PacketDisconnect = 4,
        PacketPing = 5,
        PacketPong = 6,
        PacketPlayerSnapshot = 7
    };

#pragma pack(push, 1)
    struct PacketHeader
    {
        unsigned long Magic;
        unsigned short Version;
        unsigned short Type;
        unsigned short PayloadSize;
        unsigned short Reserved;
        unsigned long Sequence;
        unsigned long SessionId;
    };

    struct HelloPayload
    {
        char Name[32];
    };

    struct WelcomePayload
    {
        unsigned long PeerId;
        unsigned char PlayerLimit;
        unsigned char Reserved[3];
    };

    struct RejectPayload
    {
        unsigned long Reason;
    };

    struct PingPayload
    {
        unsigned long Timestamp;
    };

    struct PlayerSnapshotPayload
    {
        unsigned long PlayerId;
        float X;
        float Y;
        float Z;
        float Yaw;
        unsigned long MovementFlags;
    };
#pragma pack(pop)

    bool StartHost(unsigned short Port);
    bool StartJoin(char const * Address, unsigned short Port);
    void Stop(void);
    bool IsRunning(void);
    void GetStatus(char * Buffer, unsigned int BufferSize);
}

#endif
