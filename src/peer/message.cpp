#include "message.h"
#include "../tracker/tracker.h"
#include <netinet/in.h>
#include <cstring>
#include "peer.h"
#include <sys/socket.h>
#include <netdb.h>
#include <iostream>
#include <unistd.h>
#include "../torrent/torrent.h"
#include <openssl/sha.h>
#include <fstream>

bool recvAll(int socketfd, uint8_t *buffer, int len);
bool message(PeerConnection &connectedPeer, Torrent &torrent);

bool peerHasNeededPiece(PeerConnection &connectedPeer, Torrent &torrent)
{
    for (int i = 0; i < connectedPeer.bitfield.size(); i++)
    {
        if (!torrent.bitfield[i] && connectedPeer.bitfield[i])
        {
            return true;
        }
    }
    return false;
}

void sendInterested(PeerConnection &connectedPeer)
{
    std::cout << "sending interested" << std::endl;
    uint32_t length = htonl(1);
    uint8_t id = 2;
    send(connectedPeer.socket, &length, 4, 0);
    send(connectedPeer.socket, &id, 1, 0);
}

void requestPiece(PeerConnection &connectedPeer, Torrent &torrent)
{
    std::cout << "Requesting Piece: ";
    uint8_t buffer[17]{};
    uint32_t length = htonl(13);
    memcpy(buffer, &length, 4);
    uint8_t id = 6;
    memcpy(buffer + 4, &id, 1);

    uint32_t pieceIndex;
    for (int i = 0; i < torrent.bitfield.size(); i++)
    {
        if (!torrent.bitfield[i] && connectedPeer.bitfield[i])
        {
            std::cout << i << std::endl;
            pieceIndex = htonl(i);
            memcpy(buffer + 5, &pieceIndex, 4);
            uint32_t begin = 0;

            uint64_t pieceStart =
                (uint64_t)i * torrent.piecelength;

            uint32_t currentPieceLength =
                std::min(
                    (uint64_t)torrent.piecelength,
                    torrent.length - pieceStart);

            if (torrent.activePiece)
            {
                begin = torrent.activePiece->nextBlock;
            }
            uint32_t requestLength = htonl(std::min(16384, (int)(currentPieceLength - begin)));
            begin = htonl(begin);
            memcpy(buffer + 9, &begin, 4);
            memcpy(buffer + 13, &requestLength, 4);
            send(connectedPeer.socket, buffer, 17, 0);
            return;
        }
    }
}

void messageLoop(PeerConnection &connectedPeer, Torrent &torrent)
{
    while (message(connectedPeer, torrent))
    {
        if (!connectedPeer.choked)
        {
            requestPiece(connectedPeer, torrent);
        }
    }
}

bool message(PeerConnection &connectedPeer, Torrent &torrent)
{
    std::cout << "message called" << std::endl;
    Peer peer = connectedPeer.peer;
    uint8_t buffer[4];

    bool success = recvAll(connectedPeer.socket, buffer, 4);
    if (!success)
        return false;
    uint32_t length;
    memcpy(&length, buffer, 4);
    length = ntohl(length);
    if (length == 0)
        return true;
    std::vector<uint8_t> message(length);

    bool n = recvAll(connectedPeer.socket, message.data(), length);
    if (!n)
        return false;

    uint8_t id = message[0];
    std::cout << (int)id << std::endl;

    if ((int)id == 0)
    {
        connectedPeer.choked = true;
    }
    else if ((int)id == 1)
    {
        std::cout << "unchoked called" << std::endl;
        connectedPeer.choked = false;
    }
    else if ((int)id == 2)
    {
        connectedPeer.interested = true;
    }
    else if ((int)id == 3)
    {
        connectedPeer.interested = false;
    }
    else if ((int)id == 5)
    {
        std::cout << "Received bitfield" << std::endl;
        connectedPeer.bitfield.clear();
        for (size_t i = 1; i < length; i++)
        {
            uint8_t b = message[i];
            for (int bit = 7; bit >= 0; bit--)
            {
                connectedPeer.bitfield.push_back((b >> bit) & 1);
            }
        }
        if (peerHasNeededPiece(connectedPeer, torrent))
        {
            sendInterested(connectedPeer);
        }
    }
    else if ((int)id == 7)
    {
        // implement piece
        std::cout << "message size " << message.size() << std::endl;
        uint32_t index;
        memcpy(&index, message.data() + 1, 4);
        index = ntohl(index);

        std::cout << "first" << std::endl;
        std::cout << torrent.bitfield.size() << " " << index << std::endl;
        if (torrent.bitfield[index])
        {
            // Already downloaded this piece.
            // Ignore duplicate block.
            std::cout << "ik";
            return true;
        }
        std::cout << "second" << std::endl;


        uint32_t begin;
        memcpy(&begin, message.data() + 5, 4);
        begin = ntohl(begin);

        std::cout << "Received Piece No: " << index << " starting from offset " << begin << std::endl;
        int blocklength = length - 9;
        std::vector<uint8_t> block(blocklength);
        memcpy(block.data(), message.data() + 9, blocklength);
        std::cout << "1" << std::endl;

        uint64_t pieceStart =
            (uint64_t)index * torrent.piecelength;

        uint32_t currentPieceLength =
            std::min(
                (uint64_t)torrent.piecelength,
                torrent.length - pieceStart);

        if (!torrent.activePiece)
        {

            torrent.activePiece.emplace();
            std::cout << "piecel" << torrent.piecelength << '\n';
            torrent.activePiece->index = index;
            torrent.activePiece->nextBlock = begin + blocklength;

            torrent.activePiece->data.resize(currentPieceLength);
            memcpy(torrent.activePiece->data.data(), block.data(), blocklength);
        }
        else
        {
            if (torrent.activePiece->index != index)
            {
                std::cout << "SOMETHING WENT WRONG INDEX ARENT EQUAL" << std::endl;
            }
            torrent.activePiece->nextBlock = begin + blocklength,
            memcpy(torrent.activePiece->data.data() + begin, block.data(), blocklength);

            if (torrent.activePiece->nextBlock >= currentPieceLength)
            {
                std::cout << "Received the full piece";
                unsigned char hash[SHA_DIGEST_LENGTH];

                // check hash
                SHA1(
                    reinterpret_cast<const unsigned char *>(torrent.activePiece->data.data()),
                    torrent.activePiece->data.size(),
                    hash);

                int cmp = memcmp(hash, torrent.pieceHashes[torrent.activePiece->index].data(), SHA_DIGEST_LENGTH);
                if (cmp != 0)
                {
                    std::cout << "THE INFO HASH DOES NOT MATCH" << std::endl;
                    torrent.activePiece.reset();
                    return true;
                }

                std::cout << "THE INFO HASH MATCHES, PIECE FULLY DOWNLOADED" << std::endl;

                // add piece to memory
                std::fstream file(torrent.name, std::ios::binary | std::ios::in | std::ios::out);
                uint64_t offset = torrent.activePiece->index * torrent.piecelength;
                file.seekp(offset);
                file.write(
                    reinterpret_cast<char *>(torrent.activePiece->data.data()),
                    currentPieceLength);

                // set the bifield to true;
                torrent.bitfield[torrent.activePiece->index] = true;
                torrent.activePiece.reset();
            }
        }
    }
    std::cout << "state: " << (int)id << std::endl;
    return true;
}

bool recvAll(int socketfd, uint8_t *buffer, int len)
{
    int bytesReceived = 0;
    while (bytesReceived < len)
    {
        ssize_t n = recv(socketfd, buffer + bytesReceived, len - bytesReceived, 0);
        if (n == 0)
        {
            std::cout << "Peer closed the connection\n";
            return false;
        }
        else if (n < 0)
        {
            perror("recv error");
            return false;
        }
        bytesReceived += n;
    }
    return true;
}