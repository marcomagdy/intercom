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
    // A non-blocking read that returns immediately if no data is available.
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

class UdpSocket {
    int m_sockfd;
    uint16_t m_port;
    UdpSocket(int socket, uint16_t port)
        : m_sockfd(socket)
        , m_port(port)
    {
    }
public:
    ~UdpSocket();
    static std::optional<UdpSocket> create(uint16_t port);
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // Broadcast data on the socket
    void broadcast(const uint8_t* data, size_t length);

    std::pair<uint64_t, int> receive_from(uint8_t* buffer, size_t length, std::string& sender_address) const;
    std::pair<uint64_t, int> send_to(const uint8_t* buffer, size_t length, const char* address) const;
};
}

