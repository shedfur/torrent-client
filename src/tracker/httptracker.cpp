#include "tracker.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <iostream>
#include <vector>
#include "httptracker.h"
#include <iomanip>
#include "../parser/parser.h"
#include "../peer/peer.h"
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <mutex>

char peer_id[] = "-AM0001-123456789012";

void announceHTTPS(const Torrent &torrent, const Tracker &tracker, std::unordered_set<Peer, PeerHash> &peers, std::mutex &peersMutex)
{
    std::cout << "Getting peers from HTTPS tracker" << '\n';

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx)
    {
        std::cerr << "Failer to create SSL context" << std::endl;
        return;
    }

    SSL_CTX_set_default_verify_paths(ctx);

    BIO *bio = BIO_new_ssl_connect(ctx);
    if (!bio)
    {
        std::cerr << "Failed to create BIO." << std::endl;
        SSL_CTX_free(ctx);
        return;
    }

    SSL *ssl = nullptr;
    BIO_get_ssl(bio, &ssl);

    if (!ssl)
    {
        std::cerr << "Failed to locate SSL pointer." << std::endl;
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return;
    }
    std::string port = std::to_string(tracker.port);

    void *ptr = (void *)tracker.host.c_str(); // Cast to void*

    std::string connection_str = tracker.host + ":" + std::to_string(tracker.port);
    std::cout << tracker.host + ":" + port << std::endl;

    SSL_set_tlsext_host_name(ssl, tracker.host.c_str());
    BIO_set_conn_hostname(bio, connection_str.c_str());

    if (BIO_do_connect(bio) <= 0)
    {
        std::cerr << "Connection or TLS handshake failed." << std::endl;
        ERR_print_errors_fp(stderr);
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return;
    }

    uint16_t clientPort = 6881;
    int num = 0;

    std::string encodedInfoHash = urlEncode(torrent.infoHash.data(), 20);
    std::string encodedPeerId = urlEncode(reinterpret_cast<const uint8_t *>(peer_id), 20);

    std::string http_request =
        "GET " + tracker.path + "?"
                                "info_hash=" +
        encodedInfoHash +
        "&peer_id=" + encodedPeerId +
        "&port=6881" +
        "&uploaded=0" +
        "&downloaded=0" +
        "&left=" +
        std::to_string(torrent.piecelength) +
        "&compact=1" +
        "&event=started" +
        " HTTP/1.1\r\n" +
        "Host: " +
        tracker.host + "\r\n" +
        "Connection: close\r\n" +
        "\r\n";

    if (BIO_write(bio, http_request.c_str(), http_request.length()) <= 0)
    {
        std::cerr << "Failed to write HTTP request headers." << std::endl;
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return;
    }

    char buffer[8192];
    std::string response;
    int bytes_read = 0;

    while ((bytes_read = BIO_read(bio, buffer, 8192 - 1)) > 0)
        response.append(buffer, bytes_read);

    BIO_free_all(bio);
    SSL_CTX_free(ctx);
    EVP_cleanup();

    getPeersfromResponse(response, peers, peersMutex);
}

void announceHTTP(const Torrent &torrent, const Tracker &tracker, std::unordered_set<Peer, PeerHash> &peers, std::mutex &peersMutex)
{
    std::cout << "Getting peers from HTTP tracker" << '\n';

    uint16_t clientPort = 6881;
    int num = 0;

    struct addrinfo hints;
    struct addrinfo *servinfo;

    // we basically remove any garbage values that there might be in hints
    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port = std::to_string(tracker.port);

    int status = getaddrinfo(
        tracker.host.c_str(),
        port.c_str(),
        &hints,
        &servinfo);

    if (status != 0)
    {
        std::cerr << "getaddrinfo: " << gai_strerror(status) << '\n';
        return;
    }

    int sockfd = socket(servinfo->ai_family,
                        servinfo->ai_socktype,
                        servinfo->ai_protocol);

    if (sockfd == -1)
    {
        perror("socket");
        freeaddrinfo(servinfo);
        return;
    }

    if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        perror("connect");
        close(sockfd);
        freeaddrinfo(servinfo);
        return;
    }
    std::string encodedInfoHash = urlEncode(torrent.infoHash.data(), 20);
    std::string encodedPeerId = urlEncode(reinterpret_cast<const uint8_t *>(peer_id), 20);
    std::string http_request =
        "GET " + tracker.path + "?"
                                "info_hash=" +
        encodedInfoHash +
        "&peer_id=" + encodedPeerId +
        "&port=6881" +
        "&uploaded=0" +
        "&downloaded=0" +
        "&left=" +
        std::to_string(torrent.piecelength) +
        "&compact=1" +
        "&event=started" +
        " HTTP/1.1\r\n" +
        "Host: " +
        tracker.host + "\r\n" +
        "Connection: close\r\n" +
        "\r\n";

    ssize_t bytesSent = send(sockfd, http_request.data(), http_request.size(), 0);
    if (bytesSent == -1)
        perror("sendto");

    std::cout << "Bytes sent:" << bytesSent << std::endl;

    char buffer[8192];
    std::string response;

    while (true)
    {
        ssize_t bytesReceived = recv(sockfd, buffer, sizeof buffer, 0);
        if (bytesReceived == 0)
            break;
        if (bytesReceived < 0)
        {
            perror("recv");
            break;
        }
        response.append(buffer, bytesReceived);
    }

    freeaddrinfo(servinfo);
    close(sockfd);

    getPeersfromResponse(response, peers, peersMutex);
}

void getPeersfromResponse(std::string response, std::unordered_set<Peer, PeerHash> &peers, std::mutex &peersMutex)
{
    std::cout << response << std::endl;
    // 1. Check that you got something
    if (response.empty())
        return;

    // 2. Check HTTP status
    size_t lineEnd = response.find("\r\n");
    if (lineEnd == std::string::npos)
        return;

    std::string statusLine = response.substr(0, lineEnd);

    if (statusLine.find("200") == std::string::npos)
    {
        std::cerr << statusLine << '\n';
        return;
    }

    // 3. Find the body
    size_t bodyStart = response.find("\r\n\r\n");

    if (bodyStart == std::string::npos)
    {
        std::cerr << "Malformed HTTP response\n";
        return;
    }

    std::string body = response.substr(bodyStart + 4);

    std::istringstream stream(body);

    InfoPos infopos;

    BencodeValue bencodevalue = benCodeParser(stream, infopos);
    Dictionary &result = std::get<Dictionary>(bencodevalue.value);

    if (result.find("failure reason") != result.end())
    {
        std::string failure = std::get<std::string>((*result.at("failure reason")).value);
        if (failure.size())
        {
            std::cout << "failure" << failure << '\n';
            return;
        }
    }
    std::unordered_set<Peer, PeerHash> localpeers;
    if (std::holds_alternative<List>((*result.at("peers")).value))
    {
        List &peerlist = std::get<List>((*result.at("peers")).value);

        for (size_t i = 0; i < peerlist.size(); i++)
        {
            Dictionary &peer = std::get<Dictionary>((*peerlist[i]).value);
            std::string ip = std::get<std::string>((*peer.at("ip")).value);
            int64_t port = std::get<int64_t>((*peer.at("port")).value);
            Peer p;
            if (isIPv6(ip))
            {
                p.isIPv6 = true;

                if (inet_pton(AF_INET6, ip.c_str(), p.ip.data()) != 1)
                    throw std::runtime_error("Invalid IPv6");
            }
            else
            {
                p.isIPv6 = false;

                if (inet_pton(AF_INET, ip.c_str(), p.ip.data()) != 1)
                    throw std::runtime_error("Invalid IPv4");
            }
            p.port = static_cast<uint16_t>(port);
            localpeers.insert(p);
        }
    }

    else
    {
        auto it = result.find("peers");
        if (it != result.end())
        {
            std::string &peerList = std::get<std::string>((*result.at("peers")).value);

            for (size_t i = 0; i < peerList.size(); i += 6)
            {
                uint8_t a = static_cast<uint8_t>(peerList[i]);
                uint8_t b = static_cast<uint8_t>(peerList[i + 1]);
                uint8_t c = static_cast<uint8_t>(peerList[i + 2]);
                uint8_t d = static_cast<uint8_t>(peerList[i + 3]);

                uint16_t port =
                    (static_cast<uint8_t>(peerList[i + 4]) << 8) |
                    static_cast<uint8_t>(peerList[i + 5]);

                Peer peer;
                peer.ip[0] = a;
                peer.ip[1] = b;
                peer.ip[2] = c;
                peer.ip[3] = d;
                peer.port = port;
                peer.isIPv6 = false;
                peers.insert(peer);
            }
        }
        it = result.find("peers6");
        if (it != result.end())
        {
            std::string &ipv6peerList = std::get<std::string>((*result.at("peers6")).value);
            for (size_t i = 0; i < ipv6peerList.size(); i += 18)
            {
                Peer peer;
                for (size_t j = 0; j < 16; j++)
                {
                    peer.ip[j] = static_cast<uint8_t>(ipv6peerList[i + j]);
                }
                uint16_t port =
                    (static_cast<uint8_t>(ipv6peerList[i + 16]) << 8) |
                    static_cast<uint8_t>(ipv6peerList[i + 17]);
                peer.port = port;
                peer.isIPv6 = true;
                localpeers.insert(peer);
            }
        }
    };
    {
        std::lock_guard<std::mutex> lock(peersMutex);
        peers.insert(localpeers.begin(), localpeers.end());
    };
}

std::string urlEncode(const uint8_t *data, size_t len)
{
    // this gets a pointer to a data;
    const char *hex = "0123456789ABCDEF";

    std::string result;
    while (len)
    {
        int left = *data >> 4;
        int right = *data & 0x0F;
        result += '%';
        result += hex[left];
        result += hex[right];
        data++;
        len--;
    }
    return result;
}

void httpTracker(const Torrent &torrent, const Tracker &tracker, std::unordered_set<Peer, PeerHash> &peers, std::mutex &peersMutex)
{
    if (tracker.protocol == "http")
        announceHTTP(torrent, tracker, peers, peersMutex);

    else if (tracker.protocol == "https")
        announceHTTPS(torrent, tracker, peers, peersMutex);

    else
        throw std::runtime_error("Invalid protocol");
}

bool isIPv6(const std::string &ip)
{
    return ip.find(':') != std::string::npos;
}