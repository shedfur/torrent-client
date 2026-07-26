#include "../tracker/tracker.h"
#include <netinet/in.h>
#include <cstring>
#include "peer.h"
#include <sys/socket.h>
#include <netdb.h>
#include <iostream>
#include <unistd.h>

bool handShake(PeerConnection &peerconnection, std::array<uint8_t, 20> &infoHash)
{
    char peer_id[] = "-AM0001-123456789012";

    uint8_t packet[68] = {};
    packet[0] = 19;
    memcpy(packet + 1, "BitTorrent protocol", 19);
    memcpy(packet + 28, infoHash.data(), 20);
    memcpy(packet + 48, peer_id, 20);

    ssize_t bytesSent = send(peerconnection.socket, packet, 68, 0);

    if (bytesSent != 68)
    {
        perror("send");
        close(peerconnection.socket);
        return false;
    }

    std::cout << "Bytes sent: " << bytesSent << std::endl;

    uint8_t response[68];
    size_t totalReceived = 0;

    timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    setsockopt(peerconnection.socket,
               SOL_SOCKET,
               SO_RCVTIMEO,
               &tv,
               sizeof(tv));

    while (totalReceived < 68)
    {
        ssize_t n = recv(
            peerconnection.socket,
            response + totalReceived,
            68 - totalReceived,
            0);

        if (n == 0)
        {
            std::cout << "Peer closed connection\n";
            return false;
        }

        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::cout << "Handshake timed out\n";
            }
            else
            {
                perror("recv");
            }

            return false;
        }

        totalReceived += n;
    }

    if (response[0] != 19)
    {
        return false;
    }

    if (memcmp(response + 1, "BitTorrent protocol", 19) != 0)
    {
        return false;
    }

    if (memcmp(response + 28, infoHash.data(), 20) != 0)
    {
        return false;
    }

    return true;
}