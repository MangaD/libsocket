/*
 * @file socket.hpp
 * @brief Modern cross-platform TCP/UDP/Unix socket abstraction for C++17 (Windows, Linux, POSIX).
 *
 * Provides the ServerSocket, Socket, DatagramSocket, and UnixSocket (POSIX) classes for easy, Java-like socket
 * programming. Handles platform differences, error reporting, and supports both TCP, UDP, and Unix domain sockets.
 *
 * Author: David Gonçalves (MangaD)
 */

#pragma once

#ifdef __GNUC__
#define QUOTE(s) #s
#define DIAGNOSTIC_PUSH() _Pragma("GCC diagnostic push")
#define DIAGNOSTIC_IGNORE(warning) _Pragma(QUOTE(GCC diagnostic ignored warning))
#define DIAGNOSTIC_POP() _Pragma("GCC diagnostic pop")
#else
#define DIAGNOSTIC_PUSH()
#define DIAGNOSTIC_IGNORE(warning)
#define DIAGNOSTIC_POP()
#endif

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32

#include <iphlpapi.h> // GetAdaptersInfo
#include <winsock2.h>
#include <ws2tcpip.h>

// https://msdn.microsoft.com/en-us/library/6sehtctf.aspx
#if !defined(WINVER) || (WINVER < _WIN32_WINNT_WINXP)
#error WINVER must be defined to something equal or above to 0x0501 // Win XP
#endif // WINVER
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < _WIN32_WINNT_WINXP)
#error _WIN32_WINNT must be defined to something equal or above to 0x0501 // Win XP
#endif // _WIN32_WINNT

// Enable AF_UNIX support on Windows 10+ (version 1803, build 17134) only
#if defined(_MSC_VER) && (_WIN32_WINNT >= _WIN32_WINNT_WIN10)
// https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/
#include <afunix.h> // Windows 10+ AF_UNIX support
#define AF_UNIX 1
#endif

/* If compiled with Visual C++
 * https://msdn.microsoft.com/en-us/library/windows/desktop/ms737629%28v=vs.85%29.aspx
 * http://stackoverflow.com/questions/3484434/what-does-pragma-comment-mean
 * http://stackoverflow.com/questions/70013/how-to-detect-if-im-compiling-code-with-visual-studio-2008
 */
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib") // GetAdaptersInfo
#endif

#else

// Assuming Linux
#include <arpa/inet.h>  //for inet_ntoa
#include <cstring>      //strerror
#include <errno.h>      //errno
#include <ifaddrs.h>    //getifaddrs
#include <netdb.h>      //for struct addrinfo
#include <netinet/in.h> //for sockaddr_in
#include <sys/ioctl.h>  //ioctl
#include <sys/socket.h> //socket
#include <sys/types.h>  //socket
#include <sys/un.h>     //for Unix domain sockets
#include <unistd.h>

#endif

namespace libsocket
{

#ifdef _WIN32

inline int InitSockets()
{
    WSADATA WSAData;
    return WSAStartup(MAKEWORD(2, 2), &WSAData);
}
inline int CleanupSockets()
{
    return WSACleanup();
}
inline int GetSocketError()
{
    return WSAGetLastError();
}
inline int CloseSocket(SOCKET fd)
{
    return closesocket(fd);
}

const char* inet_ntop_aux(int af, const void* src, char* dst, socklen_t size);

#else

typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr SOCKET SOCKET_ERROR = -1;
typedef SOCKADDR_UN sockaddr_un;

constexpr int InitSockets()
{
    return 0;
}
constexpr int CleanupSockets()
{
    return 0;
}
inline int GetSocketError()
{
    return errno;
}
inline int CloseSocket(SOCKET fd)
{
    return close(fd);
}

inline int ioctlsocket(SOCKET fd, long cmd, u_long* argp)
{
    return ioctl(fd, static_cast<unsigned long>(cmd), argp);
}

#endif

/**
 * @brief Get a human-readable error message for a socket error code.
 * @param error The error code (platform-specific).
 * @return Error message string.
 */
std::string SocketErrorMessage(int error);

/**
 * @brief Get a human-readable error message for a socket error code, wrapped with exception safety.
 * @param error The error code (platform-specific).
 * @return Error message string.
 */
std::string SocketErrorMessageWrap(int error);

/**
 * @brief Exception class for socket errors.
 *
 * Stores an error code and a descriptive error message. Thrown by all socket operations on error.
 */
class socket_exception : public std::exception
{
  public:
    explicit socket_exception(int code, std::string message = "socket_exception")
        : std::exception(), error_code(code), error_message(message)
    {
        error_message += " (" + std::to_string(code) + ")";
    }
    const char* what() const noexcept { return error_message.c_str(); }
    int get_error_code() const noexcept { return error_code; }
    ~socket_exception() = default;

  private:
    int error_code = 0;
    std::string error_message;
};

/**
 * @brief Helper class to initialize and cleanup sockets (RAII).
 *
 * On Windows, calls WSAStartup/WSACleanup. On Linux/POSIX, does nothing.
 * Throws socket_exception on failure.
 */
class SocketInitializer
{
  public:
    inline SocketInitializer()
    {
        if (InitSockets() != 0)
            throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    }
    inline ~SocketInitializer() noexcept
    {
        if (CleanupSockets() != 0)
            std::cerr << "Socket cleanup failed: " << SocketErrorMessageWrap(GetSocketError()) << ": "
                      << GetSocketError() << std::endl;
    }

    SocketInitializer(const SocketInitializer& rhs) = delete;
    SocketInitializer& operator=(const SocketInitializer& rhs) = delete;
};

/**
 * @brief Enum for socket shutdown modes.
 *
 * Used to specify how to shutdown a socket (read, write, or both).
 */
enum class ShutdownMode
{
    Read,  // Shutdown read operations (SHUT_RD or SD_RECEIVE)
    Write, // Shutdown write operations (SHUT_WR or SD_SEND)
    Both   // Shutdown both read and write operations (SHUT_RDWR or SD_BOTH)
};

/**
 * @brief TCP client socket abstraction (Java-like interface).
 *
 * Provides connect, read, write, close, and address info. Handles both IPv4 and IPv6.
 *
 * @note Not thread-safe. Each socket should only be used from one thread at a time.
 */
class Socket
{
    friend class ServerSocket;

  private:
    SOCKET clientSocket = INVALID_SOCKET; ///< Underlying socket file descriptor.
    struct sockaddr_storage
        remote_addr; ///< sockaddr_in for IPv4; sockaddr_in6 for IPv6; sockaddr_storage for both (portability)
    mutable socklen_t remote_addr_length = 0; ///< Length of remote address (for recvfrom/recvmsg)
    struct addrinfo* cli_addrinfo = nullptr; ///< Address info for connection (from getaddrinfo)
    struct addrinfo* selectedAddrInfo = nullptr; ///< Selected address info for connection
    std::vector<char> buffer; ///< Internal buffer for read operations

  protected:
    /** To be used internally by the accept function of ServerSocket. */
    Socket(SOCKET client, struct sockaddr_storage addr, socklen_t len);

  public:
    /**
     * @brief Construct a Socket for a given host and port.
     * @param host Hostname or IP address to connect to.
     * @param port TCP port number.
     * @param bufferSize Size of the internal read buffer (default: 512).
     * @throws socket_exception on failure.
     */
    explicit Socket(std::string host, unsigned short port, std::size_t bufferSize = 512);

    // move constructor
    Socket(const Socket& rhs) = delete; //-Weffc++
    Socket(Socket&& rhs) noexcept
        : clientSocket(rhs.clientSocket), remote_addr(rhs.remote_addr), remote_addr_length(rhs.remote_addr_length),
          cli_addrinfo(rhs.cli_addrinfo), selectedAddrInfo(rhs.selectedAddrInfo), buffer(std::move(rhs.buffer))
    {
        rhs.clientSocket = INVALID_SOCKET;
        rhs.cli_addrinfo = nullptr;
        rhs.selectedAddrInfo = nullptr;
    }

    // move assignment function
    Socket& operator=(const Socket& rhs) = delete; //-Weffc++
    Socket& operator=(Socket&& rhs) noexcept
    {
        if (this != &rhs)
        {
            if (this->clientSocket != INVALID_SOCKET)
            {
                if (CloseSocket(this->clientSocket))
                    std::cerr << "closesocket() failed: " << SocketErrorMessage(GetSocketError()) << ": "
                              << GetSocketError() << std::endl;
            }
            if (cli_addrinfo)
            {
                freeaddrinfo(cli_addrinfo);
                cli_addrinfo = nullptr;
            }
            clientSocket = rhs.clientSocket;
            remote_addr = rhs.remote_addr;
            remote_addr_length = rhs.remote_addr_length;
            cli_addrinfo = rhs.cli_addrinfo;
            selectedAddrInfo = rhs.selectedAddrInfo;
            buffer = std::move(rhs.buffer);
            rhs.clientSocket = INVALID_SOCKET;
            rhs.cli_addrinfo = nullptr;
            rhs.selectedAddrInfo = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor. Closes the socket and frees resources.
     */
    ~Socket() noexcept;

    /**
     * @brief Get the remote socket's address as a string (ip:port).
     * @return String representation of the remote address.
     */
    std::string getRemoteSocketAddress() const;

    /**
     * @brief Connect the socket to the remote host:port.
     * @throws socket_exception on error.
     */
    void connect();

    /**
     * @brief Read a trivially copyable type from the socket.
     * @tparam T Type to read (must be trivially copyable).
     * @return Value of type T read from the socket.
     * @throws socket_exception on error or disconnect.
     */
    template <typename T> T read()
    {
        static_assert(std::is_trivially_copyable<T>::value, "Socket::read<T>() only supports trivially copyable types");
        T r;
        int len = recv(this->clientSocket, reinterpret_cast<char*>(&r), sizeof(T), 0);
        if (len == SOCKET_ERROR)
            throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
        if (len == 0)
            throw socket_exception(0, "Connection closed by remote host.");
        return r;
    }

    /**
     * @brief Close the socket.
     * @throws socket_exception on error.
     */
    void close();

    /**
     * @brief Shutdown the socket for reading, writing, or both.
     * @param how Specifies the shutdown type:
     *      - `ShutdownMode::Read` - Shutdown read operations (no more incoming data)
     *      - `ShutdownMode::Write` - Shutdown write operations (no more outgoing data)
     *      - `ShutdownMode::Both` - Shutdown both read and write operations (default)
     * @throws socket_exception on error.
     */
    void shutdown(ShutdownMode how);

    /**
     * @brief Write a string to the socket.
     * @param message The string to send.
     * @return Number of bytes sent.
     * @throws socket_exception on error.
     */
    [[nodiscard]] int write(std::string_view message);

    /**
     * @brief Set the internal buffer size.
     * @param newLen New buffer size in bytes.
     */
    void setBufferSize(std::size_t newLen);

    /**
     * @brief Check if the socket is valid (open).
     * @return true if valid, false otherwise.
     */
    inline bool isValid() const { return this->clientSocket != INVALID_SOCKET; }

    /**
     * @brief Set the socket to non-blocking or blocking mode.
     *
     * Allows the socket to operate in non-blocking mode, where operations
     * such as connect, read, and write will return immediately if they
     * cannot be completed, or in blocking mode, where these operations
     * will wait until they can be completed.
     *
     * @param nonBlocking true to set non-blocking mode, false for blocking mode.
     * @throws socket_exception on error.
     */
    void setNonBlocking(bool nonBlocking);

    /**
     * @brief Set a timeout for socket operations.
     *
     * Sets the timeout for send and receive operations on the socket.
     *
     * @param millis Timeout in milliseconds.
     * @param forConnect If true, sets connect timeout; if false, sets read/write timeout.
     * @throws socket_exception on error.
     */
    void setTimeout(int millis, bool forConnect = false);

    /**
     * @brief Wait for the socket to be ready for reading or writing.
     *
     * @param forWrite true to wait for write, false for read.
     * @param timeoutMillis Timeout in milliseconds.
     * @return true if ready, false if timeout.
     * @throws socket_exception on error.
     */
    bool waitReady(bool forWrite, int timeoutMillis) const;

    /**
     * @brief Check if the socket is still connected (TCP only).
     *
     * @return true if connected, false otherwise.
     */
    bool isConnected() const;

    /**
     * @brief Enable or disable TCP_NODELAY (Nagle's algorithm) on the socket.
     *
     * When TCP_NODELAY is enabled (set to true), Nagle's algorithm is disabled. This means that small packets
     * of data are sent immediately over the network, without waiting to accumulate more data. This can reduce
     * latency for applications that require fast, interactive communication (such as games, real-time systems,
     * or protocols where low latency is more important than bandwidth efficiency).
     *
     * When TCP_NODELAY is disabled (set to false), Nagle's algorithm is enabled. This causes the socket to
     * buffer small outgoing packets and send them together, which can reduce network congestion and improve
     * throughput for bulk data transfers, but may introduce slight delays for small messages.
     *
     * By default, TCP_NODELAY is disabled (i.e., Nagle's algorithm is enabled) on new sockets.
     *
     * @param enable true to disable Nagle's algorithm (enable TCP_NODELAY, lower latency),
     *               false to enable Nagle's algorithm (disable TCP_NODELAY, higher throughput).
     */
    void enableNoDelay(bool enable);

    /**
     * @brief Enable or disable SO_KEEPALIVE on the socket.
     *
     * SO_KEEPALIVE is a socket option that enables periodic transmission of keepalive probes on an otherwise idle TCP
     * connection. When enabled (set to true), the operating system will periodically send keepalive messages to the
     * remote peer if no data has been exchanged for a certain period. If the peer does not respond, the connection is
     * considered broken and will be closed.
     *
     * This feature is useful for detecting dead peers or broken network links, especially for long-lived connections
     * where silent disconnects would otherwise go unnoticed. It is commonly used in server applications, remote control
     * systems, and protocols that require reliable detection of dropped connections.
     *
     * By default, SO_KEEPALIVE is disabled (i.e., keepalive probes are not sent) on new sockets.
     *
     * @param enable true to enable keepalive probes (SO_KEEPALIVE), false to disable (default).
     */
    void enableKeepAlive(bool enable);

    /**
     * @brief Convert an address and port to a string.
     *
     * @param addr sockaddr_storage structure.
     * @return String representation (ip:port).
     */
    static std::string addressToString(const sockaddr_storage& addr);

    /**
     * @brief Convert a string (ip:port) to sockaddr_storage.
     *
     * @param str Address string.
     * @param addr Output sockaddr_storage.
     * @return true on success, false on failure.
     */
    static bool stringToAddress(const std::string& str, sockaddr_storage& addr);
};

/**
 * @brief TCP server socket abstraction (Java-like interface).
 *
 * Listens for incoming connections and accepts them as Socket objects.
 *
 * @note Not thread-safe. Each server socket should only be used from one thread at a time.
 */
class ServerSocket
{
  private:
    SOCKET serverSocket = INVALID_SOCKET; ///< Underlying socket file descriptor.
    struct addrinfo* srv_addrinfo = nullptr; ///< Address info for binding (from getaddrinfo)
    struct addrinfo* selectedAddrInfo = nullptr; ///< Selected address info for binding
    unsigned short port; ///< Port number the server will listen on

  protected:
    void cleanupAndThrow(int errorCode);
    int getSocketReuseOption() const;

  public:
    /**
     * @brief Constructs a ServerSocket object and prepares it to listen for incoming TCP connections on the specified
     * port.
     *
     * This constructor initializes a server socket that can accept both IPv4 and IPv6 connections.
     * It performs the following steps:
     *   - Sets up address resolution hints for TCP sockets, supporting both IPv4 and IPv6.
     *   - Uses getaddrinfo to resolve the local address and port for binding.
     *   - Iterates through the resolved addresses, attempting to create a socket for each until successful.
     *   - For IPv6 sockets, configures the socket to accept both IPv6 and IPv4 connections by disabling IPV6_V6ONLY.
     *   - Sets socket options to control address reuse behavior:
     *       - On Windows, uses SO_EXCLUSIVEADDRUSE to prevent port hijacking.
     *       - On other platforms, uses SO_REUSEADDR to allow quick reuse of the port after closure.
     *   - Throws socket_exception on failure at any step, providing error details.
     *
     * @param port_ The port number on which the server will listen for incoming connections.
     *
     * @throws socket_exception If address resolution, socket creation, or socket option configuration fails.
     */
    explicit ServerSocket(unsigned short port);

    // move constructor
    ServerSocket(const ServerSocket& rhs) = delete; //-Weffc++
    ServerSocket(ServerSocket&& rhs) noexcept
        : serverSocket(rhs.serverSocket), srv_addrinfo(rhs.srv_addrinfo), port(rhs.port)
    {
        rhs.serverSocket = INVALID_SOCKET;
        rhs.srv_addrinfo = nullptr;
    }

    ServerSocket& operator=(const ServerSocket& rhs) = delete; //-Weffc++

    /**
     * @brief Destructor. Closes the server socket and frees resources.
     */
    ~ServerSocket() noexcept;

    /**
     * @brief Bind the server socket to the configured port.
     * @throws socket_exception on error.
     */
    void bind();

    /**
     * @brief Start listening for incoming connections.
     * @param backlog Maximum length of the queue of pending connections (default: SOMAXCONN).
     * @throws socket_exception on error.
     */
    void listen(int backlog = SOMAXCONN);

    /**
     * @brief Accept an incoming connection.
     * @return A Socket object for the client.
     * @throws socket_exception on error.
     */
    Socket accept();

    /**
     * @brief Close the server socket.
     * @throws socket_exception on error.
     */
    void close();

    /**
     * @brief Shutdown the server socket for both send and receive.
     * @throws socket_exception on error.
     */
    void shutdown();

    /**
     * @brief Check if the server socket is valid (open).
     * @return true if valid, false otherwise.
     */
    inline bool isValid() { return this->serverSocket != INVALID_SOCKET; }
};

template <> inline std::string Socket::read()
{
    DIAGNOSTIC_PUSH()
    DIAGNOSTIC_IGNORE("-Wuseless-cast")
    int len = static_cast<int>(recv(clientSocket, buffer.data(), buffer.size(), 0));
    DIAGNOSTIC_POP()
    if (len == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    if (len == 0)
        throw socket_exception(0, "Connection closed by remote host.");
    return std::string(buffer.data(), static_cast<size_t>(len));
}

/**
 * @brief UDP datagram socket abstraction (Java-like interface).
 *
 * Provides methods for sending and receiving datagrams (UDP packets),
 * binding to a port, and setting socket options. Handles both IPv4 and IPv6.
 *
 * @note Not thread-safe. Each datagram socket should only be used from one thread at a time.
 */
class DatagramSocket
{
  private:
    SOCKET sockfd = INVALID_SOCKET; ///< Underlying socket file descriptor.
    struct sockaddr_storage local_addr
    {
    }; ///< Local address structure.
    mutable socklen_t local_addr_length = 0;           ///< Length of local address.
    struct addrinfo* addrinfo_ptr = nullptr;           ///< Address info pointer for bind/connect.
    std::vector<char> buffer = std::vector<char>(512); ///< Internal buffer for receive operations.
  public:
    /**
     * @brief Default constructor. Does not bind or connect.
     */
    DatagramSocket();
    /**
     * @brief Construct and bind a datagram socket to a local port.
     * @param port UDP port to bind to.
     * @throws socket_exception on failure.
     */
    DatagramSocket(unsigned short port);
    /**
     * @brief Construct and connect a datagram socket to a remote host and port.
     * @param host Hostname or IP address to connect to.
     * @param port UDP port number.
     * @throws socket_exception on failure.
     */
    DatagramSocket(const std::string& host, unsigned short port);
    /**
     * @brief Destructor. Closes the socket and frees resources.
     */
    ~DatagramSocket() noexcept;
    /**
     * @brief Bind the datagram socket to a local port.
     * @param port UDP port to bind to.
     * @throws socket_exception on error.
     */
    void bind(unsigned short port);
    /**
     * @brief Send a datagram to a specific host and port.
     * @param data Pointer to the buffer to send.
     * @param len Number of bytes to send.
     * @param host Destination hostname or IP address.
     * @param port Destination UDP port.
     * @return Number of bytes sent.
     * @throws socket_exception on error.
     */
    int sendTo(const void* data, std::size_t len, const std::string& host, unsigned short port);
    /**
     * @brief Receive a datagram from any sender.
     * @param data Pointer to the buffer to receive data.
     * @param len Maximum number of bytes to receive.
     * @param senderAddr Output string for sender's address.
     * @param senderPort Output variable for sender's port.
     * @return Number of bytes received.
     * @throws socket_exception on error.
     */
    int recvFrom(void* data, std::size_t len, std::string& senderAddr, unsigned short& senderPort);
    /**
     * @brief Close the datagram socket.
     */
    void close();
    /**
     * @brief Set the socket to non-blocking or blocking mode.
     * @param nonBlocking true for non-blocking, false for blocking.
     * @throws socket_exception on error.
     */
    void setNonBlocking(bool nonBlocking);
    /**
     * @brief Set a timeout for datagram socket operations.
     * @param millis Timeout in milliseconds.
     * @throws socket_exception on error.
     */
    void setTimeout(int millis);
    /**
     * @brief Set a socket option (integer value).
     * @param level Option level (e.g., SOL_SOCKET).
     * @param optname Option name (e.g., SO_BROADCAST).
     * @param value Integer value to set for the option.
     * @throws socket_exception on error.
     */
    void setOption(int level, int optname, int value);
    /**
     * @brief Get a socket option (integer value).
     * @param level Option level (e.g., SOL_SOCKET).
     * @param optname Option name (e.g., SO_BROADCAST).
     * @return Integer value of the option.
     * @throws socket_exception on error.
     */
    int getOption(int level, int optname) const;
    /**
     * @brief Get the local socket's address as a string (ip:port).
     * @return String representation of the local address.
     */
    std::string getLocalSocketAddress() const;
    /**
     * @brief Check if the datagram socket is valid (open).
     * @return true if valid, false otherwise.
     */
    bool isValid() const { return sockfd != INVALID_SOCKET; }

    // Move operations for DatagramSocket
    DatagramSocket(DatagramSocket&&) noexcept = default;
    DatagramSocket& operator=(DatagramSocket&&) noexcept = default;
};

/**
 * @brief Get all local network interface addresses as strings.
 *
 * @return Vector of strings describing each interface and address.
 */
std::vector<std::string> getHostAddr();

#if defined(_WIN32) && defined(AF_UNIX) || defined(__unix__) || defined(__APPLE__)

/**
 * @class UnixSocket
 * @brief A cross-platform wrapper for Unix domain sockets.
 *
 * On POSIX, uses native AF_UNIX sockets. On Windows, only available on Windows 10 (version 1803, build 17134) and
 * later. Uses AF_UNIX support in Winsock2.
 *
 * This class provides an interface for creating, binding, listening, accepting,
 * connecting, reading from, writing to, and closing Unix domain sockets.
 * It abstracts away platform-specific details for both Unix-like systems and Windows.
 *
 * @note Not thread-safe. Each UnixSocket should only be used from one thread at a time.
 */
class UnixSocket
{
  public:
    /**
     * @brief Default constructor.
     *
     * Initializes a new UnixSocket instance.
     */
    UnixSocket();

    /**
     * @brief Constructs a UnixSocket and connects or binds to the specified path.
     * @param path The filesystem path for the Unix domain socket.
     * @param server If true, binds as a server; if false, connects as a client.
     */
    UnixSocket(const std::string& path, bool server);

    /**
     * @brief Destructor.
     *
     * Closes the socket if it is open.
     */
    ~UnixSocket() noexcept;

    /**
     * @brief Binds the socket.
     * @throws std::socket_exception if binding fails.
     */
    void bind();

    /**
     * @brief Marks the socket as a passive socket to accept incoming connections.
     * @param backlog The maximum length to which the queue of pending connections may grow.
     * @throws std::socket_exception if listen fails.
     *
     * The backlog parameter defines the maximum number of pending connections
     * that can be queued up before connections are refused.
     */
    void listen(int backlog = SOMAXCONN);

    /**
     * @brief Accepts an incoming connection.
     * @return A new UnixSocket representing the accepted connection.
     * @throws std::socket_exception if accept fails.
     */
    UnixSocket accept();

    /**
     * @brief Connects the socket.
     * @throws std::socket_exception if connection fails.
     */
    void connect();

    /**
     * @brief Writes data to the socket.
     * @param data The data to write.
     * @return The number of bytes written.
     * @throws std::socket_exception if writing fails.
     */
    int write(std::string_view data);

    /**
     * @brief Reads data from the socket into a buffer.
     * @param buffer Pointer to the buffer to read data into.
     * @param len Maximum number of bytes to read.
     * @return The number of bytes read.
     * @throws std::socket_exception if reading fails.
     */
    int read(char* buffer, std::size_t len);

    /**
     * @brief Reads a trivially copyable type from the socket.
     * @tparam T Type to read (must be trivially copyable).
     * @return Value of type T read from the socket.
     * @throws socket_exception on error or disconnect.
     */
    template <typename T> T read()
    {
        static_assert(std::is_trivially_copyable<T>::value,
                      "UnixSocket::read<T>() only supports trivially copyable types");
        T value;
        int len = ::recv(sockfd, reinterpret_cast<char*>(&value), sizeof(T), 0);
        if (len == SOCKET_ERROR)
            throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
        if (len == 0)
            throw socket_exception(0, "Connection closed by remote socket.");
        return value;
    }

    /**
     * @brief Closes the socket.
     */
    void close();

    /**
     * @brief Checks if the socket is valid (open).
     * @return true if the socket is valid, false otherwise.
     */
    bool isValid() const;

    /**
     * @brief Sets the socket to non-blocking or blocking mode.
     *
     * This function configures the socket to operate in either non-blocking or blocking mode,
     * depending on the value of the `nonBlocking` parameter. In non-blocking mode, socket operations
     * will return immediately if they cannot be completed, rather than blocking the calling thread.
     *
     * @param nonBlocking If true, sets the socket to non-blocking mode; if false, sets it to blocking mode.
     */
    void setNonBlocking(bool nonBlocking);

    std::string UnixSocket::getSocketPath() const { return socket_path; }

    /**
     * @brief Sets a timeout for socket operations.
     * @param millis Timeout in milliseconds.
     */
    void setTimeout(int millis);

  private:
    SOCKET sockfd = INVALID_SOCKET;
    std::string socket_path;
    bool is_server = false;
    SOCKADDR_UN addr;
};

#endif

} // namespace libsocket
