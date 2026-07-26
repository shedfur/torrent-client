#include "parser/parser.h"
#include <iostream>
#include <fstream>
#include <string>
#include <bencodevalue.h>
#include <printbencode.h>
#include <openssl/sha.h>
#include "torrent/torrent.h"
#include "tracker/udptracker.h"
#include "peer/connect.h"
#include "tracker/tracker.h"
#include "tracker/httptracker.h"
#include <unordered_set>
#include "peer/peer.h"
#include "peer/handshake.h"
#include "peer/message.h"

int main()
{
    InfoPos infopos;
    std::cout << "Hello NotitTorrent" << std::endl;
    std::cout << "Reading Torrent file...................." << std::endl;

    Torrent torrent = LoadTorrent("../ubuntu-26.04-desktop-amd64.iso.torrent");

    std::cout << torrent.name << std::endl;

    std::unordered_set<Peer, PeerHash> peers = getTracker(torrent);

    std::cout << "No. of Peers found: " << peers.size() << '\n';

    int connected = 0;

    std::vector<PeerConnection> connectedPeers;
    for (const auto &peer : peers)
    {
        std::cout << "Trying to connect to a peer\n";
        std::cout << "IP: "
                  << +peer.ip[0] << "."
                  << +peer.ip[1] << "."
                  << +peer.ip[2] << "."
                  << +peer.ip[3] << '\n';

        if (connectPeer(peer, connectedPeers) == 0){
            connected++;
        }
    }

    // handShake(peers);
    std::cout << "Successfully connected to "
              << connected
              << " out of "
              << peers.size()
              << " peers\n";
    for (auto &connectedPeer : connectedPeers)
    {
        if (handShake(connectedPeer, torrent.infoHash)){
            messageLoop(connectedPeer, torrent);
        }
        else
            close(connectedPeer.socket);
    }

    return 0;
}