#include "TcpConnection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <stdio.h>
#include <unistd.h>

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

std::pair<int64_t, int> TcpConnection::read(uint8_t* buffer, size_t len) const
{
    if (len > INT_MAX) {
        return { -1, EINVAL };
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

std::pair<int64_t, int> TcpConnection::write(const uint8_t* buffer, size_t length) const
{
    if (length > INT_MAX) {
        return { -1, EINVAL };
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

} // namespace Intercom
