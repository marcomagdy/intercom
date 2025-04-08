#pragma once
#include <optional>

namespace Intercom {
class TcpConnection {
    friend class TcpConnectionListener;
    int m_sockfd;
    TcpConnection(int socket)
        : m_sockfd(socket)
    {
    }

public:
    ~TcpConnection();
    static std::optional<TcpConnection> connect(const char* hostname, uint16_t port);
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other);
    TcpConnection& operator=(TcpConnection&& other);
    std::pair<uint64_t, int> read(uint8_t* buffer, size_t length) const;
    std::pair<uint64_t, int> read_once(uint8_t* buffer, size_t length) const;
    std::pair<uint64_t, int> write(const uint8_t* buffer, size_t length) const;
    bool set_non_blocking();
    int socket() const { return m_sockfd; }
};

class TcpConnectionListener {
    int m_sockfd;
    TcpConnectionListener(int socket)
        : m_sockfd(socket)
    {
    }

public:
    ~TcpConnectionListener();
    static std::optional<TcpConnectionListener> listen(uint16_t port);
    TcpConnectionListener(const TcpConnectionListener&) = delete;
    TcpConnectionListener operator=(const TcpConnectionListener&) = delete;
    TcpConnectionListener(TcpConnectionListener&& other) noexcept;
    TcpConnectionListener& operator=(TcpConnectionListener&& other) noexcept;
    std::optional<TcpConnection> accept();
    void stop();
    int socket() const { return m_sockfd; }
};

}
