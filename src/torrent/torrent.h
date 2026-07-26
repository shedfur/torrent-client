#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include "../tracker/tracker.h"
#include <optional>

struct ActivePiece
{
    uint32_t index;
    uint32_t nextBlock;
    std::vector<uint8_t> data;
};

struct Torrent
{
    std::vector<Tracker> trackers;

    std::string name;
    int64_t length;
    int32_t piecelength;

    std::vector<bool> bitfield;

    std::vector<std::array<uint8_t, 20>> pieceHashes;
    std::array<uint8_t, 20> infoHash;

    std::optional<ActivePiece> activePiece;
};

Torrent LoadTorrent(const std::string &filename);