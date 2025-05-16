/*
 * @file socket.cpp
 * @brief Implementation of modern cross-platform TCP/UDP/Unix socket abstraction for C++17 (Windows, Linux, POSIX).
 *
 * Implements the ServerSocket, Socket, DatagramSocket, and UnixSocket (POSIX) classes for easy, Java-like socket
 * programming. Handles platform differences and error reporting.
 *
 * Author: David Gonçalves (MangaD)
 */

#include "libsocket/socket.hpp"

#include <cstring>   // Use std::memset()
#include <exception> // SocketErrorMessageWrap
#include <memory>

using namespace libsocket;

std::string libsocket::SocketErrorMessage(int error)
{
#ifdef _WIN32
    if (error == 0)
        return std::string();

    LPSTR buffer = nullptr;
    DWORD size; // Use DWORD for FormatMessageA

    // Try to get a human-readable error message for the given error code using FormatMessageA
    if ((size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |              // Allocate buffer for error message
                                   FORMAT_MESSAGE_FROM_SYSTEM |              // Get error message from system
                                   FORMAT_MESSAGE_IGNORE_INSERTS,            // Ignore insert sequences in message
                               nullptr,                                      // No source (system)
                               error,                                        // Error code
                               MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), // Use US English language
                               (LPSTR)&buffer,                               // Output buffer
                               0,                                            // Minimum size (let system decide)
                               nullptr)) == 0)                               // No arguments
    {
        // If FormatMessageA fails, print the error and reset the last error code
        std::cerr << "Format message failed: " << GetLastError() << std::endl;
        SetLastError(error);
        return std::string();
    }
    std::string errString(buffer, size);
    LocalFree(buffer);

    return errString;
#else
    return std::strerror(error);
#endif
}

std::string libsocket::SocketErrorMessageWrap(int error)
{
    std::string errString{};
    try
    {
        errString = SocketErrorMessage(error);
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    return errString;
}

// --- ServerSocket Implementation ---

ServerSocket::ServerSocket(unsigned short port_) : port(port_)
{
    // Prepare the hints structure for getaddrinfo to specify the desired socket type and protocol.
    struct addrinfo hints
    {
    }; // Initialize the hints structure to zero

    // Specifies the address family: AF_UNSPEC allows both IPv4 (AF_INET) and IPv6 (AF_INET6)
    // addresses to be returned by getaddrinfo, enabling the server to support dual-stack networking
    // (accepting connections over both protocols). This makes the code portable and future-proof,
    // as it can handle whichever protocol is available on the system or preferred by clients.
    hints.ai_family = AF_UNSPEC;

    // Specifies the desired socket type. Setting it to `SOCK_STREAM` requests a stream socket (TCP),
    // which provides reliable, connection-oriented, byte-stream communication—this is the standard
    // socket type for TCP. By specifying `SOCK_STREAM`, the code ensures that only address results suitable
    // for TCP (as opposed to datagram-based protocols like UDP, which use `SOCK_DGRAM`) are returned by `getaddrinfo`.
    // This guarantees that the created socket will support the semantics required for TCP server operation, such as
    // connection establishment, reliable data transfer, and orderly connection termination.
    hints.ai_socktype = SOCK_STREAM;

    // This field ensures that only address results suitable for TCP sockets are returned by
    // `getaddrinfo`. While `SOCK_STREAM` already implies TCP in most cases, explicitly setting
    // `ai_protocol` to `IPPROTO_TCP` eliminates ambiguity and guarantees that the created socket
    // will use the TCP protocol. This is particularly important on platforms where multiple
    // protocols may be available for a given socket type, or where protocol selection is not
    // implicit. By specifying `IPPROTO_TCP`, the code ensures that the socket will provide
    // reliable, connection-oriented, byte-stream communication as required for a TCP server.
    hints.ai_protocol = IPPROTO_TCP;

    // Setting `hints.ai_flags = AI_PASSIVE` indicates that the returned socket addresses
    // should be suitable for binding a server socket that will accept incoming connections.
    // When `AI_PASSIVE` is set and the `node` parameter to `getaddrinfo` is `nullptr`,
    // the resolved address will be a "wildcard address" (INADDR_ANY for IPv4 or IN6ADDR_ANY_INIT for IPv6),
    // allowing the server to accept connections on any local network interface.
    //
    // This is essential for server applications, as it enables them to listen for client
    // connections regardless of which network interface the client uses to connect.
    // If `AI_PASSIVE` were not set, the resolved address would be suitable for a client
    // socket (i.e., for connecting to a specific remote host), not for a server socket.
    // If the `node` parameter to `getaddrinfo` is not NULL, then the AI_PASSIVE flag is ignored.
    hints.ai_flags = AI_PASSIVE;

    // Resolve the local address and port to be used by the server
    std::string portStr = std::to_string(port_);
    if (getaddrinfo(nullptr,         // Node (hostname or IP address); nullptr means local host
                    portStr.c_str(), // Service (port number or service name) as a C-string
                    &hints,          // Pointer to struct addrinfo with hints about the type of socket
                    &srv_addrinfo    // Output: pointer to a linked list of results (set by getaddrinfo)
                    ) != 0)
    {
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }

    // First, try to create a socket with IPv6 (prioritized)
    for (struct addrinfo* p = srv_addrinfo; p != nullptr; p = p->ai_next)
    {
        if (p->ai_family == AF_INET6)
        {
            // Attempt to create a socket using the current address
            serverSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (serverSocket == INVALID_SOCKET)
                continue;  // If socket creation failed, try the next address

            // Store the address that worked
            selectedAddrInfo = p;

            // Configure the socket for dual-stack (IPv4 + IPv6) if it's IPv6
            int optionValue = 0; // 0 means allow both IPv4 and IPv6 connections
            if (setsockopt(serverSocket, IPPROTO_IPV6, IPV6_V6ONLY, 
                reinterpret_cast<const char*>(&optionValue), sizeof(optionValue)) == SOCKET_ERROR)
            {
                cleanupAndThrow(GetSocketError());
            }
            break;  // Exit after successfully creating the IPv6 socket
        }
    }

    // If no IPv6 address worked, fallback to IPv4
    if (serverSocket == INVALID_SOCKET)
    {
        for (struct addrinfo* p = srv_addrinfo; p != nullptr; p = p->ai_next)
        {
            // Only attempt IPv4 if no valid IPv6 address has been used
            if (p->ai_family == AF_INET)
            {
                // Attempt to create a socket using the current address (IPv4)
                serverSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (serverSocket == INVALID_SOCKET)
                    continue;  // If socket creation failed, try the next address

                // Store the address that worked
                selectedAddrInfo = p;
                break;  // Exit after successfully creating the IPv4 socket
            }
        }
    }

    if (serverSocket == INVALID_SOCKET)
    {
        cleanupAndThrow(GetSocketError());
    }

    // Set socket options for address reuse
    int optval = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, getSocketReuseOption(), reinterpret_cast<const char*>(&optval),
                   sizeof(optval)) == SOCKET_ERROR)
    {
        cleanupAndThrow(GetSocketError());
    }
}

void ServerSocket::cleanupAndThrow(int errorCode)
{
    // Clean up addrinfo and throw exception with the error
    freeaddrinfo(srv_addrinfo);
    srv_addrinfo = nullptr;
    selectedAddrInfo = nullptr;
    throw socket_exception(errorCode, SocketErrorMessage(errorCode));
}

int ServerSocket::getSocketReuseOption() const
{
#ifdef _WIN32
    return SO_EXCLUSIVEADDRUSE; // Windows-specific
#else
    return SO_REUSEADDR; // Unix/Linux-specific
#endif
}

ServerSocket::~ServerSocket() noexcept
{
    try
    {
        close();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    if (srv_addrinfo)
    {
        freeaddrinfo(srv_addrinfo);
        srv_addrinfo = nullptr;
    }
}

void ServerSocket::close()
{
    if (this->serverSocket != INVALID_SOCKET)
    {
        try
        {
            // It's good practice to call shutdown before closing a listening socket,
            // to ensure all resources are released and connections are properly terminated.
            shutdown();
        }
        catch (...)
        {
            // Ignore shutdown errors, proceed to close anyway.
        }
        if (CloseSocket(this->serverSocket))
            throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
        else
            this->serverSocket = INVALID_SOCKET;
    }
}

void ServerSocket::shutdown()
{
    constexpr int how =
#ifdef _WIN32
        SD_BOTH;
#else
        SHUT_RDWR;
#endif
    if (this->serverSocket != INVALID_SOCKET)
    {
        if (::shutdown(this->serverSocket, how))
            throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
    }
}

void ServerSocket::bind()
{
    // Ensure that we have already selected an address during construction
    if (selectedAddrInfo == nullptr)
        throw socket_exception(0, "bind() failed: no valid addrinfo found");

    int res = ::bind(serverSocket,                // The socket file descriptor to bind.
                     selectedAddrInfo->ai_addr,   // Pointer to the sockaddr structure (address and port) to bind to.
                     selectedAddrInfo->ai_addrlen // Size of the sockaddr structure.
    );

    if (res == SOCKET_ERROR)
    {
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }
}

void ServerSocket::listen(int backlog /* = SOMAXCONN */)
{
    int n = ::listen(serverSocket, backlog);
    if (n == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

Socket ServerSocket::accept()
{
    // Create structure to hold client address
    struct sockaddr_storage cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    // Accept an incoming connection
    SOCKET clientSocket = ::accept(serverSocket, (struct sockaddr*)&cli_addr, &clilen);

    // Handle socket creation failure
    if (clientSocket == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));

    Socket client(clientSocket, cli_addr, clilen);

    return client;
}

// --- Socket Implementation ---

Socket::Socket(SOCKET client, struct sockaddr_storage addr, socklen_t len)
    : clientSocket(client), remote_addr(addr), remote_addr_length(len)
{
}

Socket::Socket(std::string host, unsigned short port, std::size_t bufferSize)
    : remote_addr{}, buffer(bufferSize)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the local address and port to be used by the server
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &cli_addrinfo) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));

    // Try all available addresses (IPv6/IPv4) to create a SOCKET for
    clientSocket = INVALID_SOCKET;
    for (struct addrinfo* p = cli_addrinfo; p != nullptr; p = p->ai_next)
    {
        clientSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (clientSocket != INVALID_SOCKET)
        {
            // Store the address that worked
            selectedAddrInfo = p;
            break;
        }
    }
    if (clientSocket == INVALID_SOCKET)
    {
        int error = GetSocketError();
        freeaddrinfo(cli_addrinfo);
        cli_addrinfo = nullptr;
        throw socket_exception(error, SocketErrorMessage(error));
    }
}

void Socket::connect()
{
    if (selectedAddrInfo == nullptr)
    {
        throw socket_exception(0, "Address information not available for connection.");
    }

    // Attempt to connect using the previously selected address
    int res = ::connect(clientSocket, selectedAddrInfo->ai_addr, selectedAddrInfo->ai_addrlen);
    if (res == SOCKET_ERROR)
    {
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }
}

/**
 * @brief Destructor. Closes the socket and frees resources.
 */
Socket::~Socket() noexcept
{
    try
    {
        close();
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
}

/**
 * @brief Close the socket.
 * @throws socket_exception on error.
 */
void Socket::close()
{
    if (this->clientSocket != INVALID_SOCKET)
    {
        if (CloseSocket(this->clientSocket))
            throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
        else
            this->clientSocket = INVALID_SOCKET;
    }
    if (cli_addrinfo)
    {
        freeaddrinfo(cli_addrinfo);
        cli_addrinfo = nullptr;
    }
    selectedAddrInfo = nullptr;
}

void Socket::shutdown(ShutdownMode how)
{
    // Convert ShutdownMode to platform-specific shutdown constants
    int shutdownType;
#ifdef _WIN32
    switch (how)
    {
    case ShutdownMode::Read:
        shutdownType = SD_RECEIVE;
        break;
    case ShutdownMode::Write:
        shutdownType = SD_SEND;
        break;
    case ShutdownMode::Both:
        shutdownType = SD_BOTH;
        break;
    }
#else
    switch (how)
    {
    case ShutdownMode::Read:
        shutdownType = SHUT_RD;
        break;
    case ShutdownMode::Write:
        shutdownType = SHUT_WR;
        break;
    case ShutdownMode::Both:
        shutdownType = SHUT_RDWR;
        break;
    }
#endif

    // Ensure the socket is valid before attempting to shutdown
    if (this->clientSocket != INVALID_SOCKET)
    {
        if (::shutdown(this->clientSocket, shutdownType))
        {
            throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
        }
    }
}

/**
 * @brief Get the remote socket's address as a string (ip:port).
 * @return String representation of the remote address.
 */
// http://www.microhowto.info/howto/convert_an_ip_address_to_a_human_readable_string_in_c.html
std::string Socket::getRemoteSocketAddress() const
{
    std::string ip_port = "null";
    if (remote_addr_length > 0)
    {
        if (remote_addr.ss_family == AF_INET6)
        {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&remote_addr;
            if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr))
            {
                struct sockaddr_in addr4;
                std::memset(&addr4, 0, sizeof(addr4));
                addr4.sin_family = AF_INET;
                addr4.sin_port = addr6->sin6_port;
                memcpy(&addr4.sin_addr.s_addr, addr6->sin6_addr.s6_addr + 12, sizeof(addr4.sin_addr.s_addr));
                memcpy((void*)&remote_addr, &addr4, sizeof(addr4));
                remote_addr_length = sizeof(addr4);
            }
        }
        char ip_s[INET6_ADDRSTRLEN];
        char port_s[6];
        getnameinfo((struct sockaddr*)&remote_addr, remote_addr_length, ip_s, sizeof(ip_s), port_s, sizeof(port_s),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        ip_port = std::string(ip_s) + ":" + port_s;
    }
    return ip_port;
}

/**
 * @brief Write a string to the socket.
 * @param message The string to send.
 * @return Number of bytes sent.
 * @throws socket_exception on error.
 */
int Socket::write(std::string_view message)
{
    int flags = 0;
#ifndef _WIN32
    flags = MSG_NOSIGNAL;
#endif
    int len = static_cast<int>(send(clientSocket, message.data(), message.size(), flags));
    if (len == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return len;
}

/**
 * @brief Set the internal buffer size.
 * @param newLen New buffer size in bytes.
 */
void Socket::setBufferSize(std::size_t newLen)
{
    buffer.resize(newLen);
    buffer.shrink_to_fit();
}

void Socket::setNonBlocking(bool nonBlocking)
{
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(clientSocket, FIONBIO, &mode) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    if (nonBlocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl(clientSocket, F_SETFL, flags) == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

void Socket::setTimeout(int millis, bool forConnect)
{
#ifdef _WIN32
    if (forConnect)
    {
        // No direct connect timeout in WinSock; use non-blocking connect + select in connect()
        return;
    }
    int timeout = millis;
    if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR ||
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
    timeval tv{millis / 1000, (millis % 1000) * 1000};
    if (forConnect)
    {
        // No direct connect timeout in POSIX; use non-blocking connect + select in connect()
        return;
    }
    if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR ||
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

void Socket::shutdown(int how)
{
    int shutdown_how;
#ifdef _WIN32
    switch (how)
    {
    case 0:
        shutdown_how = SD_RECEIVE;
        break;
    case 1:
        shutdown_how = SD_SEND;
        break;
    case 2:
        shutdown_how = SD_BOTH;
        break;
    default:
        throw socket_exception(0, "Invalid shutdown mode");
    }
#else
    switch (how)
    {
    case 0:
        shutdown_how = SHUT_RD;
        break;
    case 1:
        shutdown_how = SHUT_WR;
        break;
    case 2:
        shutdown_how = SHUT_RDWR;
        break;
    default:
        throw socket_exception(0, "Invalid shutdown mode");
    }
#endif
    if (this->clientSocket != INVALID_SOCKET)
    {
        if (::shutdown(this->clientSocket, shutdown_how))
            throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
    }
}

bool Socket::waitReady(bool forWrite, int timeoutMillis) const
{
    if (clientSocket == INVALID_SOCKET)
        throw socket_exception(0, "Invalid socket");

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(clientSocket, &fds);

    timeval tv;
    tv.tv_sec = timeoutMillis / 1000;
    tv.tv_usec = (timeoutMillis % 1000) * 1000;

    int result;
#ifdef _WIN32
    // On Windows, the first parameter to select is ignored, but must be set to 0.
    if (forWrite)
        result = select(0, nullptr, &fds, nullptr, &tv);
    else
        result = select(0, &fds, nullptr, nullptr, &tv);
#else
    if (forWrite)
        result = select(static_cast<int>(clientSocket) + 1, nullptr, &fds, nullptr, &tv);
    else
        result = select(static_cast<int>(clientSocket) + 1, &fds, nullptr, nullptr, &tv);
#endif

    if (result < 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return result > 0;
}

bool Socket::isConnected() const
{
    if (clientSocket == INVALID_SOCKET)
        return false;
    char buf;
#ifdef _WIN32
    u_long bytesAvailable = 0;
    if (ioctlsocket(clientSocket, FIONREAD, &bytesAvailable) == SOCKET_ERROR)
        return false;
    if (bytesAvailable > 0)
        return true;
    // Try a non-blocking recv with MSG_PEEK
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);
    int ret = recv(clientSocket, &buf, 1, MSG_PEEK);
    mode = 0;
    ioctlsocket(clientSocket, FIONBIO, &mode);
    if (ret == 0)
        return false; // connection closed
    if (ret < 0)
    {
        int err = WSAGetLastError();
        return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
    }
    return true;
#else
    int flags = fcntl(clientSocket, F_GETFL, 0);
    bool wasNonBlocking = flags & O_NONBLOCK;
    if (!wasNonBlocking)
        fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
    int ret = recv(clientSocket, &buf, 1, MSG_PEEK);
    if (!wasNonBlocking)
        fcntl(clientSocket, F_SETFL, flags);
    if (ret == 0)
        return false;
    if (ret < 0)
        return errno == EWOULDBLOCK || errno == EAGAIN;
    return true;
#endif
}

void Socket::enableNoDelay(bool enable)
{
    int flag = enable ? 1 : 0;
    if (setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag)) ==
        SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

void Socket::enableKeepAlive(bool enable)
{
    int flag = enable ? 1 : 0;
    if (setsockopt(clientSocket, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&flag), sizeof(flag)) ==
        SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

std::string Socket::addressToString(const sockaddr_storage& addr)
{
    char ip[INET6_ADDRSTRLEN] = {0};
    char port[6] = {0};

    if (addr.ss_family == AF_INET)
    {
        const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&addr);
        getnameinfo(reinterpret_cast<const sockaddr*>(addr4), sizeof(sockaddr_in), ip, sizeof(ip), port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    }
    else if (addr.ss_family == AF_INET6)
    {
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&addr);
        getnameinfo(reinterpret_cast<const sockaddr*>(addr6), sizeof(sockaddr_in6), ip, sizeof(ip), port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    }
    else
    {
        return "unknown";
    }
    return std::string(ip) + ":" + port;
}

bool Socket::stringToAddress(const std::string& str, sockaddr_storage& addr)
{
    std::memset(&addr, 0, sizeof(addr));
    // Find last ':' (to allow IPv6 addresses with ':')
    auto pos = str.rfind(':');
    if (pos == std::string::npos)
        return false;

    std::string host = str.substr(0, pos);
    std::string port = str.substr(pos + 1);

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    struct addrinfo* res = nullptr;
    int ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (ret != 0 || !res)
        return false;

    std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return true;
}

// --- DatagramSocket Implementation ---
DatagramSocket::DatagramSocket() {}

DatagramSocket::DatagramSocket(unsigned short port)
{
    bind(port);
}

DatagramSocket::DatagramSocket(const std::string& host, unsigned short port)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    std::string s = std::to_string(port);
    if (getaddrinfo(host.c_str(), s.c_str(), &hints, &addrinfo_ptr) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    sockfd = socket(addrinfo_ptr->ai_family, addrinfo_ptr->ai_socktype, addrinfo_ptr->ai_protocol);
    if (sockfd == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

DatagramSocket::~DatagramSocket() noexcept
{
    close();
    if (addrinfo_ptr)
    {
        freeaddrinfo(addrinfo_ptr);
        addrinfo_ptr = nullptr;
    }
}

void DatagramSocket::bind(unsigned short port)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = IPPROTO_UDP;
    std::string s = std::to_string(port);
    if (getaddrinfo(nullptr, s.c_str(), &hints, &addrinfo_ptr) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    sockfd = socket(addrinfo_ptr->ai_family, addrinfo_ptr->ai_socktype, addrinfo_ptr->ai_protocol);
    if (sockfd == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    if (::bind(sockfd, addrinfo_ptr->ai_addr, addrinfo_ptr->ai_addrlen) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    local_addr_length = addrinfo_ptr->ai_addrlen;
    std::memcpy(&local_addr, addrinfo_ptr->ai_addr, local_addr_length);
}

int DatagramSocket::sendTo(const void* data, std::size_t len, const std::string& host, unsigned short port)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    struct addrinfo* destinfo = nullptr;
    std::string s = std::to_string(port);
    if (getaddrinfo(host.c_str(), s.c_str(), &hints, &destinfo) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    int sent = static_cast<int>(sendto(sockfd, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                                       destinfo->ai_addr, destinfo->ai_addrlen));
    freeaddrinfo(destinfo);
    if (sent == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return sent;
}

int DatagramSocket::recvFrom(void* data, std::size_t len, std::string& senderAddr, unsigned short& senderPort)
{
    sockaddr_storage src_addr;
    socklen_t addrlen = sizeof(src_addr);
    int recvd = static_cast<int>(
        recvfrom(sockfd, reinterpret_cast<char*>(data), static_cast<int>(len), 0, (sockaddr*)&src_addr, &addrlen));
    if (recvd == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    char host[INET6_ADDRSTRLEN] = {0};
    char serv[6] = {0};
    getnameinfo((sockaddr*)&src_addr, addrlen, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    senderAddr = host;
    senderPort = static_cast<unsigned short>(std::stoi(serv));
    return recvd;
}

void DatagramSocket::close()
{
    if (sockfd != INVALID_SOCKET)
    {
        CloseSocket(sockfd);
        sockfd = INVALID_SOCKET;
    }
}

void DatagramSocket::setNonBlocking(bool nonBlocking)
{
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(sockfd, FIONBIO, &mode) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    if (nonBlocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

void DatagramSocket::setTimeout(int millis)
{
#ifdef _WIN32
    int timeout = millis;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
    timeval tv{millis / 1000, (millis % 1000) * 1000};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

void DatagramSocket::setOption(int level, int optname, int value)
{
    if (setsockopt(sockfd, level, optname, reinterpret_cast<const char*>(&value), sizeof(value)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

int DatagramSocket::getOption(int level, int optname) const
{
    int value = 0;
    socklen_t len = sizeof(value);
    if (getsockopt(sockfd, level, optname, reinterpret_cast<char*>(&value), &len) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return value;
}

std::string DatagramSocket::getLocalSocketAddress() const
{
    char host[INET6_ADDRSTRLEN] = {0};
    char serv[6] = {0};
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(sockfd, (sockaddr*)&addr, &len) == 0)
    {
        getnameinfo((sockaddr*)&addr, len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
        return std::string(host) + ":" + serv;
    }
    return "null";
}

#if defined(_WIN32) && defined(AF_UNIX) || defined(__unix__) || defined(__APPLE__)

UnixSocket::UnixSocket() : sockfd(INVALID_SOCKET) {}

UnixSocket::UnixSocket(const std::string& path, bool server) : sockfd(INVALID_SOCKET), socket_path(path)
{
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

UnixSocket::~UnixSocket() noexcept
{
    close();
}

void UnixSocket::bind()
{
    unlink(socket_path.c_str());
    if (::bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        CloseSocket(sockfd);
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }
}

void UnixSocket::listen(int backlog)
{
    if (::listen(sockfd, backlog) == SOCKET_ERROR)
    {
        CloseSocket(sockfd);
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }
}

void UnixSocket::connect()
{
    if (::connect(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        CloseSocket(sockfd);
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }
}

void UnixSocket::close()
{
    if (sockfd != INVALID_SOCKET)
    {
        CloseSocket(sockfd);
        sockfd = INVALID_SOCKET;
    }
}

UnixSocket UnixSocket::accept()
{
    sockaddr_un client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = ::accept(sockfd, reinterpret_cast<sockaddr*>(&client_addr), &len);
    if (client_fd == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    UnixSocket client;
    client.sockfd = client_fd;
    client.addr = client_addr;
    client.socket_path = client_addr.sun_path;
    return client;
}

int UnixSocket::write(std::string_view message)
{
    size_t n = ::send(sockfd, message.data(), message.size(), 0);
    if (n == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return static_cast<int>(n);
}

int UnixSocket::read(char* buffer, std::size_t len)
{
    size_t n = ::recv(sockfd, buffer, len, 0);
    if (n == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return static_cast<int>(n);
}

void UnixSocket::setNonBlocking(bool nonBlocking)
{
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(sockfd, FIONBIO, &mode) != 0)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    if (nonBlocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

void UnixSocket::setTimeout(int millis)
{
#ifdef _WIN32
    int timeout = millis;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ==
            SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
    timeval tv{millis / 1000, (millis % 1000) * 1000};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

#endif

#ifdef _WIN32
/**
 * Redefine because not available on Windows XP
 * http://stackoverflow.com/questions/13731243/what-is-the-windows-xp-equivalent-of-inet-pton-or-inetpton
 */
const char* libsocket::inet_ntop_aux(int af, const void* src, char* dst, socklen_t size)
{
    struct sockaddr_storage ss;
    unsigned long s = size;

    ZeroMemory(&ss, sizeof(ss));
    ss.ss_family = static_cast<short>(af);

    switch (af)
    {
    case AF_INET:
        ((struct sockaddr_in*)&ss)->sin_addr = *(struct in_addr*)src;
        break;
    case AF_INET6:
        ((struct sockaddr_in6*)&ss)->sin6_addr = *(struct in6_addr*)src;
        break;
    default:
        return NULL;
    }
    /* cannot direclty use &size because of strict aliasing rules */
    return (WSAAddressToString((struct sockaddr*)&ss, sizeof(ss), nullptr, dst, &s) == 0) ? dst : NULL;
}
#endif

std::vector<std::string> libsocket::getHostAddr()
{
    std::vector<std::string> ips;

#ifdef _WIN32
    /* Declare and initialize variables */
    const int MAX_TRIES = 3;
    DWORD dwRetVal = 0;
    unsigned int i = 0;
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX; // Set the flags to pass to GetAdaptersAddresses
    ULONG family = AF_UNSPEC;              // default to unspecified address family (both)
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
    ULONG outBufLen = 15000; // Allocate a 15 KB buffer to start with.
    ULONG Iterations = 0;
    char buff[100];
    DWORD bufflen = 100;
    PIP_ADAPTER_ADDRESSES pCurrAddresses = nullptr;
    PIP_ADAPTER_UNICAST_ADDRESS pUnicast = nullptr;
    PIP_ADAPTER_ANYCAST_ADDRESS pAnycast = nullptr;
    PIP_ADAPTER_MULTICAST_ADDRESS pMulticast = nullptr;

    do
    {
        pAddresses = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, (outBufLen));
        if (pAddresses == nullptr)
        {
            std::cerr << "Memory allocation failed for IP_ADAPTER_ADDRESSES struct." << std::endl;
            return ips;
        }
        dwRetVal = GetAdaptersAddresses(family, flags, nullptr, pAddresses, &outBufLen);
        if (dwRetVal == ERROR_BUFFER_OVERFLOW)
        {
            HeapFree(GetProcessHeap(), 0, (pAddresses));
            pAddresses = nullptr;
        }
        else
        {
            break;
        }
        Iterations++;
    } while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (Iterations < MAX_TRIES));

    if (dwRetVal == NO_ERROR)
    {
        // If successful, output some information from the data we received
        pCurrAddresses = pAddresses;
        while (pCurrAddresses)
        {
            pUnicast = pCurrAddresses->FirstUnicastAddress;
            if (pUnicast)
            {
                for (i = 0; pUnicast != nullptr; i++)
                {
                    if (pUnicast->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        sockaddr_in* sa_in = (sockaddr_in*)pUnicast->Address.lpSockaddr;
                        ips.emplace_back(std::string(pCurrAddresses->AdapterName) + " IPv4 Address " +
                                         inet_ntop_aux(AF_INET, &(sa_in->sin_addr), buff, bufflen));
                    }
                    else if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6)
                    {
                        sockaddr_in6* sa_in6 = (sockaddr_in6*)pUnicast->Address.lpSockaddr;
                        ips.emplace_back(std::string(pCurrAddresses->AdapterName) + " IPv6 Address " +
                                         inet_ntop_aux(AF_INET6, &(sa_in6->sin6_addr), buff, bufflen));
                    }
                    // else{printf("\tUNSPEC");}
                    pUnicast = pUnicast->Next;
                }
            }

            pAnycast = pCurrAddresses->FirstAnycastAddress;
            if (pAnycast)
            {
                for (i = 0; pAnycast != nullptr; i++)
                {
                    if (pAnycast->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        sockaddr_in* sa_in = (sockaddr_in*)pAnycast->Address.lpSockaddr;
                        ips.emplace_back(std::string(pCurrAddresses->AdapterName) + " IPv4 Address " +
                                         inet_ntop_aux(AF_INET, &(sa_in->sin_addr), buff, bufflen));
                    }
                    else if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6)
                    {
                        sockaddr_in6* sa_in6 = (sockaddr_in6*)pAnycast->Address.lpSockaddr;
                        ips.emplace_back(std::string(pCurrAddresses->AdapterName) + " IPv6 Address " +
                                         inet_ntop_aux(AF_INET6, &(sa_in6->sin6_addr), buff, bufflen));
                    }
                    // else{printf("\tUNSPEC");}
                    pAnycast = pAnycast->Next;
                }
            }

            pMulticast = pCurrAddresses->FirstMulticastAddress;
            if (pMulticast)
            {
                for (i = 0; pMulticast != nullptr; i++)
                {
                    if (pMulticast->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        sockaddr_in* sa_in = (sockaddr_in*)pMulticast->Address.lpSockaddr;
                        ips.emplace_back(std::string(pCurrAddresses->AdapterName) + " IPv4 Address " +
                                         inet_ntop_aux(AF_INET, &(sa_in->sin_addr), buff, bufflen));
                    }
                    else if (pMulticast->Address.lpSockaddr->sa_family == AF_INET6)
                    {
                        sockaddr_in6* sa_in6 = (sockaddr_in6*)pMulticast->Address.lpSockaddr;
                        ips.emplace_back(std::string(pCurrAddresses->AdapterName) + " IPv6 Address " +
                                         inet_ntop_aux(AF_INET6, &(sa_in6->sin6_addr), buff, bufflen));
                    }
                    // else{printf("\tUNSPEC");}
                    pMulticast = pMulticast->Next;
                }
            }

            pCurrAddresses = pCurrAddresses->Next;
        }
    }
    else
    {
        if (pAddresses)
        {
            HeapFree(GetProcessHeap(), 0, (pAddresses));
        }
        socket_exception(dwRetVal, SocketErrorMessage(dwRetVal));
    }

    if (pAddresses)
    {
        HeapFree(GetProcessHeap(), 0, (pAddresses));
    }
#else
    struct ifaddrs* ifAddrStruct = nullptr;
    struct ifaddrs* ifa = nullptr;
    void* tmpAddrPtr = nullptr;

    if (getifaddrs(&ifAddrStruct))
    {
        socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }

    for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
        {
            continue;
        }
        DIAGNOSTIC_PUSH()
        DIAGNOSTIC_IGNORE("-Wcast-align")
        if (ifa->ifa_addr->sa_family == AF_INET)
        { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr = &(reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr))->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            ips.emplace_back(std::string(ifa->ifa_name) + " IPv4 Address " + addressBuffer);
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6)
        { // check it is IP6
            // is a valid IP6 Address
            tmpAddrPtr = &(reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr))->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            ips.emplace_back(std::string(ifa->ifa_name) + " IPv6 Address " + addressBuffer);
        }
        DIAGNOSTIC_POP()
    }
    if (ifAddrStruct != nullptr)
        freeifaddrs(ifAddrStruct);
#endif

    return ips;
}
