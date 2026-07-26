#include "torrent.h"
#include "../parser/parser.h"
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include "../parser/bencodevalue.h"
#include <openssl/sha.h>
#include "../tracker/tracker.h"
#include "../parser/trackerparser/trackerparser.h"

Torrent LoadTorrent(const std::string &filename)
{
    std::cout << "Loading torrent" << '\n';
    Torrent torrent;
    InfoPos infoPos;

    std::ifstream inputFile(filename);

    if (!inputFile.is_open())
    {
        std::cout << "Failed to open the file";
    }
    BencodeValue bencodevalue = benCodeParser(inputFile, infoPos);
    Dictionary &result = std::get<Dictionary>(bencodevalue.value);

    // extract announce
    std::string announce = std::get<std::string>((*result.at("announce")).value);
    torrent.trackers.push_back(trackerParser(announce));

    // extract announcelist
    List &announcelist = std::get<List>((*result.at("announce-list")).value);
    size_t listlen = announcelist.size();
    for (size_t i = 0; i < listlen; i++)
    {
        List &furtherlist = std::get<List>((*announcelist[i]).value);
        size_t flistlen = furtherlist.size();
        for (size_t j = 0; j < flistlen; j++)
        {
            std::string announce = std::get<std::string>((*furtherlist[j]).value);
            torrent.trackers.push_back(trackerParser(announce));
        }
    }

    // extract name
    Dictionary &info = std::get<Dictionary>((*result.at("info")).value);
    std::string name = std::get<std::string>((*info.at("name")).value);
    torrent.name = name;

    // extract length
    int64_t length = std::get<int64_t>((*info.at("length")).value);
    torrent.length = length;

    int32_t piecelength = std::get<int64_t>((*info.at("piece length")).value);
    torrent.piecelength = piecelength;

    // compute info hash
    inputFile.clear();
    inputFile.seekg(infoPos.start);

    std::string hashInfo(static_cast<size_t>(infoPos.end - infoPos.start), '\0');
    inputFile.read(hashInfo.data(), infoPos.end - infoPos.start);
    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(
        reinterpret_cast<const unsigned char *>(hashInfo.data()),
        hashInfo.size(),
        hash);

    std::memcpy(torrent.infoHash.data(), hash, SHA_DIGEST_LENGTH);

    // split pieces into hashes
    std::string pieces = std::get<std::string>((*info.at("pieces")).value);
    for (size_t i = 0; i < pieces.size(); i += 20)
    {
        std::array<uint8_t, 20> pieceHash;

        std::memcpy(pieceHash.data(), pieces.data() + i, 20);

        torrent.pieceHashes.push_back(pieceHash);
        torrent.bitfield.push_back(false);
    }

    for (size_t i = 0; i < torrent.trackers.size(); i++)
    {
        std::cout << "protocol: " << torrent.trackers[i].protocol << "    " << "host: " << torrent.trackers[i].host << "    " << "port: " << torrent.trackers[i].port << "    " << "path: " << torrent.trackers[i].path << std::endl;
    }
    
    std::ofstream create(torrent.name, std::ios::binary);
    create.close();

    return torrent;
}