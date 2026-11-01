#include "Network.h"
#include <stdio.h>
#include <string.h>

namespace
{
    const unsigned long PacketMagic = 0x504D5352; // RSMP
    const unsigned int MaxDatagramSize = 512;
    const unsigned int MaxRemotePeers = RisenMP::MaxPlayers - 1;

    struct SRemotePeer
    {
        sockaddr_in Address;
        unsigned long PeerId;
        bool Used;
    };

    struct SNetworkState
    {
        SOCKET Socket;
        HANDLE Thread;
        volatile LONG Running;
        bool IsHost;
        bool Connected;
        bool WinsockStarted;
        unsigned short Port;
        unsigned long SessionId;
        unsigned long LocalPeerId;
        unsigned long Sequence;
        unsigned long LastHello;
        unsigned long LastPing;
        sockaddr_in HostAddress;
        SRemotePeer Peers[MaxRemotePeers];
        unsigned int PeerCount;
        CRITICAL_SECTION Lock;
        bool LockReady;
        char Status[160];
    };

    SNetworkState g_State = { INVALID_SOCKET, 0, 0, false, false, false, 0, 0, 0, 0, 0, 0, {0}, {{0}}, 0, {0}, false, "Stopped" };

    void SetStatus(char const * Text)
    {
        if (g_State.LockReady)
            EnterCriticalSection(&g_State.Lock);
        strncpy(g_State.Status, Text, sizeof(g_State.Status) - 1);
        g_State.Status[sizeof(g_State.Status) - 1] = 0;
        if (g_State.LockReady)
            LeaveCriticalSection(&g_State.Lock);
    }

    bool SameAddress(sockaddr_in const & A, sockaddr_in const & B)
    {
        return A.sin_addr.s_addr == B.sin_addr.s_addr && A.sin_port == B.sin_port;
    }

    void SendPacket(sockaddr_in const & To, unsigned short Type, unsigned long SessionId,
        void const * Payload, unsigned short PayloadSize)
    {
        char Buffer[MaxDatagramSize];
        RisenMP::PacketHeader Header;
        int TotalSize;

        if (PayloadSize > MaxDatagramSize - sizeof(Header))
            return;

        Header.Magic = PacketMagic;
        Header.Version = RisenMP::ProtocolVersion;
        Header.Type = Type;
        Header.PayloadSize = PayloadSize;
        Header.Reserved = 0;
        Header.Sequence = ++g_State.Sequence;
        Header.SessionId = SessionId;

        memcpy(Buffer, &Header, sizeof(Header));
        if (PayloadSize && Payload)
            memcpy(Buffer + sizeof(Header), Payload, PayloadSize);

        TotalSize = sizeof(Header) + PayloadSize;
        sendto(g_State.Socket, Buffer, TotalSize, 0,
            reinterpret_cast<sockaddr const *>(&To), sizeof(To));
    }

    void SendHello(void)
    {
        RisenMP::HelloPayload Payload;
        sockaddr_in Target = g_State.HostAddress;
        memset(&Payload, 0, sizeof(Payload));
        strncpy(Payload.Name, "RisenMP Player", sizeof(Payload.Name) - 1);
        SendPacket(Target, RisenMP::PacketHello, 0, &Payload, sizeof(Payload));
    }

    int FindPeer(sockaddr_in const & Address)
    {
        unsigned int i;
        for (i = 0; i < MaxRemotePeers; ++i)
            if (g_State.Peers[i].Used && SameAddress(g_State.Peers[i].Address, Address))
                return static_cast<int>(i);
        return -1;
    }

    int AddPeer(sockaddr_in const & Address)
    {
        unsigned int i;
        int Existing = FindPeer(Address);
        if (Existing >= 0)
            return Existing;

        for (i = 0; i < MaxRemotePeers; ++i)
        {
            if (!g_State.Peers[i].Used)
            {
                g_State.Peers[i].Address = Address;
                g_State.Peers[i].PeerId = i + 2;
                g_State.Peers[i].Used = true;
                ++g_State.PeerCount;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void RemovePeer(sockaddr_in const & Address)
    {
        int Index = FindPeer(Address);
        if (Index >= 0)
        {
            g_State.Peers[Index].Used = false;
            if (g_State.PeerCount)
                --g_State.PeerCount;
        }
    }

    void SendReject(sockaddr_in const & Address, unsigned long SessionId, unsigned long Reason)
    {
        RisenMP::RejectPayload Payload;
        Payload.Reason = Reason;
        SendPacket(Address, RisenMP::PacketReject, SessionId, &Payload, sizeof(Payload));
    }

    void HandlePacket(char * Buffer, int Bytes, sockaddr_in const & From)
    {
        RisenMP::PacketHeader Header;
        char const * Payload;
        unsigned int ExpectedSize;

        if (Bytes < static_cast<int>(sizeof(Header)))
            return;

        memcpy(&Header, Buffer, sizeof(Header));
        if (Header.Magic != PacketMagic || Header.Version != RisenMP::ProtocolVersion)
            return;

        ExpectedSize = sizeof(Header) + Header.PayloadSize;
        if (ExpectedSize != static_cast<unsigned int>(Bytes))
            return;

        Payload = Buffer + sizeof(Header);

        if (Header.Type == RisenMP::PacketHello && g_State.IsHost)
        {
            RisenMP::WelcomePayload Welcome;
            int Index;

            if (Header.PayloadSize != sizeof(RisenMP::HelloPayload))
                return;

            Index = AddPeer(From);
            if (Index < 0)
            {
                SendReject(From, g_State.SessionId, 1);
                return;
            }

            Welcome.PeerId = g_State.Peers[Index].PeerId;
            Welcome.PlayerLimit = RisenMP::MaxPlayers;
            memset(Welcome.Reserved, 0, sizeof(Welcome.Reserved));
            SendPacket(From, RisenMP::PacketWelcome, g_State.SessionId, &Welcome, sizeof(Welcome));
            SetStatus("Host: remote player accepted");
            return;
        }

        if (Header.Type == RisenMP::PacketWelcome && !g_State.IsHost)
        {
            RisenMP::WelcomePayload Welcome;
            if (Header.PayloadSize != sizeof(Welcome))
                return;
            memcpy(&Welcome, Payload, sizeof(Welcome));
            if (Welcome.PlayerLimit != RisenMP::MaxPlayers || Welcome.PeerId < 2)
                return;
            g_State.SessionId = Header.SessionId;
            g_State.LocalPeerId = Welcome.PeerId;
            g_State.Connected = true;
            SetStatus("Connected to host (LAN)");
            return;
        }

        if (Header.Type == RisenMP::PacketReject && !g_State.IsHost)
        {
            SetStatus("Connection rejected: session is full");
            return;
        }

        if (Header.Type == RisenMP::PacketPing)
        {
            RisenMP::PingPayload Ping;
            if (Header.PayloadSize != sizeof(Ping))
                return;
            memcpy(&Ping, Payload, sizeof(Ping));
            if (!g_State.IsHost && Header.SessionId != g_State.SessionId)
                return;
            SendPacket(From, RisenMP::PacketPong, g_State.SessionId, &Ping, sizeof(Ping));
            return;
        }

        if (Header.Type == RisenMP::PacketPong)
        {
            if (Header.PayloadSize != sizeof(RisenMP::PingPayload))
                return;
            return;
        }

        if (Header.Type == RisenMP::PacketDisconnect)
        {
            if (g_State.IsHost)
                RemovePeer(From);
            else
            {
                g_State.Connected = false;
                SetStatus("Host disconnected");
            }
        }
    }

    DWORD WINAPI NetworkThread(LPVOID)
    {
        char Buffer[MaxDatagramSize];

        while (InterlockedCompareExchange(&g_State.Running, 1, 1) == 1)
        {
            unsigned long Now = GetTickCount();
            fd_set ReadSet;
            timeval Timeout;
            int SelectResult;

            if (!g_State.IsHost && !g_State.Connected && Now - g_State.LastHello >= 1000)
            {
                SendHello();
                g_State.LastHello = Now;
            }

            if (g_State.IsHost)
            {
                unsigned int i;
                if (Now - g_State.LastPing >= 1000)
                {
                    RisenMP::PingPayload Ping;
                    Ping.Timestamp = Now;
                    for (i = 0; i < MaxRemotePeers; ++i)
                        if (g_State.Peers[i].Used)
                            SendPacket(g_State.Peers[i].Address, RisenMP::PacketPing,
                                g_State.SessionId, &Ping, sizeof(Ping));
                    g_State.LastPing = Now;
                }
            }
            else if (g_State.Connected && Now - g_State.LastPing >= 1000)
            {
                RisenMP::PingPayload Ping;
                Ping.Timestamp = Now;
                SendPacket(g_State.HostAddress, RisenMP::PacketPing,
                    g_State.SessionId, &Ping, sizeof(Ping));
                g_State.LastPing = Now;
            }

            FD_ZERO(&ReadSet);
            FD_SET(g_State.Socket, &ReadSet);
            Timeout.tv_sec = 0;
            Timeout.tv_usec = 100000;
            SelectResult = select(0, &ReadSet, 0, 0, &Timeout);

            if (SelectResult > 0 && FD_ISSET(g_State.Socket, &ReadSet))
            {
                sockaddr_in From;
                int FromLength = sizeof(From);
                int Received = recvfrom(g_State.Socket, Buffer, sizeof(Buffer), 0,
                    reinterpret_cast<sockaddr *>(&From), &FromLength);
                if (Received > 0)
                    HandlePacket(Buffer, Received, From);
            }
        }
        return 0;
    }

    bool StartSocket(unsigned short Port)
    {
        sockaddr_in Local;
        g_State.Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_State.Socket == INVALID_SOCKET)
            return false;

        memset(&Local, 0, sizeof(Local));
        Local.sin_family = AF_INET;
        Local.sin_addr.s_addr = htonl(INADDR_ANY);
        Local.sin_port = htons(Port);

        if (bind(g_State.Socket, reinterpret_cast<sockaddr *>(&Local), sizeof(Local)) == SOCKET_ERROR)
        {
            closesocket(g_State.Socket);
            g_State.Socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    bool StartThread(void)
    {
        InterlockedExchange(&g_State.Running, 1);
        g_State.Thread = CreateThread(0, 0, NetworkThread, 0, 0, 0);
        if (!g_State.Thread)
        {
            InterlockedExchange(&g_State.Running, 0);
            return false;
        }
        return true;
    }
}

namespace RisenMP
{
    bool StartHost(unsigned short Port)
    {
        WSADATA Data;
        Stop();

        if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
        {
            SetStatus("WSAStartup failed");
            return false;
        }
        g_State.WinsockStarted = true;
        InitializeCriticalSection(&g_State.Lock);
        g_State.LockReady = true;

        if (!StartSocket(Port))
        {
            SetStatus("Cannot bind UDP port");
            WSACleanup();
            g_State.WinsockStarted = false;
            DeleteCriticalSection(&g_State.Lock);
            g_State.LockReady = false;
            return false;
        }

        g_State.IsHost = true;
        g_State.Connected = true;
        g_State.Port = Port;
        g_State.SessionId = GetTickCount() ^ GetCurrentProcessId();
        if (!g_State.SessionId)
            g_State.SessionId = 1;
        g_State.LocalPeerId = 1;
        g_State.Sequence = 0;
        g_State.PeerCount = 0;
        memset(g_State.Peers, 0, sizeof(g_State.Peers));

        if (!StartThread())
        {
            closesocket(g_State.Socket);
            g_State.Socket = INVALID_SOCKET;
            WSACleanup();
            g_State.WinsockStarted = false;
            DeleteCriticalSection(&g_State.Lock);
            g_State.LockReady = false;
            SetStatus("Cannot start network thread");
            return false;
        }

        SetStatus("Hosting LAN session");
        return true;
    }

    bool StartJoin(char const * Address, unsigned short Port)
    {
        WSADATA Data;
        unsigned long Ip;
        Stop();

        if (!Address || !Address[0])
            return false;

        Ip = inet_addr(Address);
        if (Ip == INADDR_NONE)
        {
            SetStatus("Use IPv4 address, e.g. 192.168.1.10");
            return false;
        }

        if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
        {
            SetStatus("WSAStartup failed");
            return false;
        }
        g_State.WinsockStarted = true;
        InitializeCriticalSection(&g_State.Lock);
        g_State.LockReady = true;

        if (!StartSocket(0))
        {
            SetStatus("Cannot open UDP socket");
            WSACleanup();
            g_State.WinsockStarted = false;
            DeleteCriticalSection(&g_State.Lock);
            g_State.LockReady = false;
            return false;
        }

        memset(&g_State.HostAddress, 0, sizeof(g_State.HostAddress));
        g_State.HostAddress.sin_family = AF_INET;
        g_State.HostAddress.sin_addr.s_addr = Ip;
        g_State.HostAddress.sin_port = htons(Port);

        g_State.IsHost = false;
        g_State.Connected = false;
        g_State.Port = Port;
        g_State.SessionId = 0;
        g_State.LocalPeerId = 0;
        g_State.Sequence = 0;
        g_State.LastHello = 0;
        g_State.LastPing = 0;
        g_State.PeerCount = 0;
        memset(g_State.Peers, 0, sizeof(g_State.Peers));

        if (!StartThread())
        {
            closesocket(g_State.Socket);
            g_State.Socket = INVALID_SOCKET;
            WSACleanup();
            g_State.WinsockStarted = false;
            DeleteCriticalSection(&g_State.Lock);
            g_State.LockReady = false;
            SetStatus("Cannot start network thread");
            return false;
        }

        SetStatus("Searching for host...");
        return true;
    }

    void Stop(void)
    {
        if (g_State.Thread)
        {
            InterlockedExchange(&g_State.Running, 0);
            WaitForSingleObject(g_State.Thread, 1500);
            CloseHandle(g_State.Thread);
            g_State.Thread = 0;
        }

        if (g_State.Socket != INVALID_SOCKET)
        {
            closesocket(g_State.Socket);
            g_State.Socket = INVALID_SOCKET;
        }

        if (g_State.WinsockStarted)
        {
            WSACleanup();
            g_State.WinsockStarted = false;
        }

        if (g_State.LockReady)
        {
            DeleteCriticalSection(&g_State.Lock);
            g_State.LockReady = false;
        }

        g_State.Connected = false;
        g_State.IsHost = false;
        g_State.PeerCount = 0;
        SetStatus("Stopped");
    }

    bool IsRunning(void)
    {
        return InterlockedCompareExchange(&g_State.Running, 1, 1) == 1;
    }

    void GetStatus(char * Buffer, unsigned int BufferSize)
    {
        unsigned int Players;
        if (!Buffer || BufferSize == 0)
            return;

        if (g_State.LockReady)
            EnterCriticalSection(&g_State.Lock);
        Players = g_State.IsHost ? 1 + g_State.PeerCount : (g_State.Connected ? 2 : 1);
        _snprintf(Buffer, BufferSize - 1, "%s | players: %u/%u | udp: %u",
            g_State.Status, Players, MaxPlayers, g_State.Port);
        Buffer[BufferSize - 1] = 0;
        if (g_State.LockReady)
            LeaveCriticalSection(&g_State.Lock);
    }
}
