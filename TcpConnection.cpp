#include "TcpConnection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <stdio.h>
#include <unistd.h>
#include <string>

namespace Intercom {

static bool DnsResolve(const char* host, sockaddr_in* addr_out)
{
    addrinfo* result;
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, nullptr, &hints, &result) != 0) {
        return false;
    }
    *addr_out = *(sockaddr_in*)result->ai_addr;
    freeaddrinfo(result);
    return true;
}

TcpConnection::~TcpConnection()
{
    if (m_sockfd >= 0) {
        ::close(m_sockfd);
    }
}

TcpConnection::TcpConnection(TcpConnection&& other)
    : m_sockfd(other.m_sockfd)
{
    other.m_sockfd = -1;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other)
{
    if (this == &other) {
        return *this;
    }

    if (m_sockfd > 0) {
        ::close(m_sockfd);
    }
    m_sockfd = other.m_sockfd;
    other.m_sockfd = -1;
    return *this;
}

std::pair<uint64_t, int> TcpConnection::read_once(uint8_t* buffer, size_t len) const
{
    if (len > INT_MAX) {
        return { 0, EINVAL };
    }
    int64_t ret = ::read(m_sockfd, buffer, len);
    if (ret >= 0) {
        return { ret, 0 };
    }

    return { 0, EWOULDBLOCK };
}

std::pair<uint64_t, int> TcpConnection::read(uint8_t* buffer, size_t len) const
{
    if (len > INT_MAX) {
        return { 0, EINVAL };
    }
    uint64_t bytes_read = 0;
    while (bytes_read < len) {
        int64_t ret = ::read(m_sockfd, buffer + bytes_read, len - bytes_read);
        if (ret < 0) {
            return { ret, errno };
        }
        bytes_read += (uint64_t)ret; // should be safe to cast to uint64_t. It's always positive.
    }
    return { bytes_read, 0 };
}

std::pair<uint64_t, int> TcpConnection::write(const uint8_t* buffer, size_t length) const
{
    if (length > INT_MAX) {
        return { 0, EINVAL };
    }
    uint64_t bytes_written = 0;
    while (bytes_written < length) {
        int64_t ret = ::write(m_sockfd, buffer + bytes_written, length - bytes_written);
        if (ret < 0) {
            return { ret, errno };
        }
        bytes_written += (uint64_t)ret; // should be safe to cast to uint64_t. It's always positive.
    }
    return { bytes_written, 0 };
}

bool TcpConnection::set_non_blocking()
{
    int flags = fcntl(m_sockfd, F_GETFL, 0);
    int ret = fcntl(m_sockfd, F_SETFL, flags | O_NONBLOCK);
    return ret == 0;
}

std::optional<TcpConnection> TcpConnection::connect(const char* hostname, uint16_t port)
{
    struct sockaddr_in addr;
    if (!DnsResolve(hostname, &addr)) {
        fprintf(stderr, "Failed to resolve hostname: %s\n", hostname);
        return std::nullopt;
    }

    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "TcpConnection - Failed to create socket: %s", strerror(errno));
        return std::nullopt;
    }

    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        fprintf(stderr, "TcpConnection - Failed to set TCP_NODELAY: %s", strerror(errno));
        return std::nullopt;
    }

    flag = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
        fprintf(stderr, "TcpConnection - Failed to set SO_REUSEADDR: %s", strerror(errno));
        return std::nullopt;
    }

    addr.sin_port = htons(port);

    if (::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "TcpConnection - Failed to connect: %s", strerror(errno));
        return std::nullopt;
    }

    printf("TcpConnection - Connected to %s:%d\n", hostname, port);
    return TcpConnection { sockfd };
}

TcpConnectionListener::~TcpConnectionListener()
{
    if (m_sockfd > 0) {
        ::close(m_sockfd);
    }
}

TcpConnectionListener::TcpConnectionListener(TcpConnectionListener&& other) noexcept
    : m_sockfd(other.m_sockfd)
{
    other.m_sockfd = -1;
}

TcpConnectionListener& TcpConnectionListener::operator=(TcpConnectionListener&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    if (m_sockfd > 0) {
        ::close(m_sockfd);
    }
    m_sockfd = other.m_sockfd;
    other.m_sockfd = -1;
    return *this;
}

std::optional<TcpConnectionListener> TcpConnectionListener::listen(uint16_t port)
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "TcpConnectionListener - Failed to create socket: %s", strerror(errno));
        return std::nullopt;
    }

    int flag = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
        fprintf(stderr, "TcpConnectionListener - Failed to set SO_REUSEADDR: %s", strerror(errno));
        return std::nullopt;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr = { INADDR_ANY };
    addr.sin_port = htons(port);

    if (::bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "TcpConnectionListener - Failed to bind socket: %s", strerror(errno));
        return std::nullopt;
    }

    if (::listen(sockfd, 6) < 0) {
        fprintf(stderr, "TcpConnectionListener - Failed to listen on socket: %s", strerror(errno));
        return std::nullopt;
    }

    printf("TcpConnectionListener - Listening on port %d", port);
    return TcpConnectionListener { sockfd };
}

std::optional<TcpConnection> TcpConnectionListener::accept()
{
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_socket = ::accept(m_sockfd, (struct sockaddr*)&client_addr, &len);
    if (client_socket < 0) {
        fprintf(stderr, "TcpConnectionListener - Failed to accept connection: %s", strerror(errno));
        return std::nullopt;
    }

    int flag = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
        fprintf(stderr, "TcpConnection - Failed to set TCP_NODELAY: %s", strerror(errno));
        return std::nullopt;
    }

    printf("TcpConnectionListener - Accepted connection");
    return TcpConnection { client_socket };
}

void TcpConnectionListener::stop()
{
    if (m_sockfd > 0) {
        ::close(m_sockfd);
        m_sockfd = -1;
    }
}

UdpSocket::~UdpSocket()
{
    if (m_sockfd >= 0) {
        ::close(m_sockfd);
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_sockfd(other.m_sockfd)
    , m_port(other.m_port)
{
    other.m_sockfd = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    if (m_sockfd >= 0) {
        ::close(m_sockfd);
    }

    m_sockfd = other.m_sockfd;
    m_port = other.m_port;
    other.m_sockfd = -1;
    return *this;
}

std::optional<UdpSocket> UdpSocket::create(uint16_t port)
{
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "UdpSocket - Failed to create socket: %s", strerror(errno));
        return std::nullopt;
    }

    int flag = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(flag)) < 0) {
        fprintf(stderr, "UdpSocket - Failed to set SO_BROADCAST: %s", strerror(errno));
        return std::nullopt;
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr = { INADDR_ANY };
    addr.sin_port = htons(port);

    if (::bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "UdpSocket - Failed to bind socket: %s", strerror(errno));
        return std::nullopt;
    }

    printf("UdpSocket - Listening on port %d\n", port);
    return UdpSocket { sockfd, port };
}

void UdpSocket::broadcast(const uint8_t* data, size_t length)
{
    if (length > INT_MAX) {
        return;
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    auto ret = ::sendto(m_sockfd, data, length, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        fprintf(stderr, "UdpSocket - Failed to broadcast: %s\n", strerror(errno));
    }
}

std::pair<uint64_t, int> UdpSocket::receive_from(uint8_t* buffer, size_t length, std::string& sender_address) const
{
    if (length > INT_MAX) {
        return { 0, EINVAL };
    }

    struct sockaddr_in sender {};
    socklen_t sender_len = sizeof(sender);
    ssize_t ret = ::recvfrom(m_sockfd, buffer, length, 0, (struct sockaddr*)&sender, &sender_len);
    if (ret < 0) {
        return { ret, errno };
    }

    char ip_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &sender.sin_addr, ip_str, sizeof(ip_str)) == nullptr) {
        return { ret, errno };
    }
    sender_address = ip_str;
    return { ret, 0 };
}

std::pair<uint64_t, int> UdpSocket::send_to(const uint8_t* buffer, size_t length, const char* address) const
{
    if (length > INT_MAX) {
        return { 0, EINVAL };
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    if (inet_pton(AF_INET, address, &addr.sin_addr) <= 0) {
        return { 0, EINVAL };
    }

    ssize_t ret = ::sendto(m_sockfd, buffer, length, 0, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        return { ret, errno };
    }
    return { ret, 0 };
}

} // namespace Intercom
