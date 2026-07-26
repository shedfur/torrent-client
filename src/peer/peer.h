#pragma once
#include "../tracker/tracker.h"
#include <vector>

struct PeerConnection
{
    Peer peer;

    int socket;

    bool handshakeDone;

    bool choked = true;      // they are choking us
    bool interested = false; // they are interested in us

    std::vector<bool> bitfield;
};