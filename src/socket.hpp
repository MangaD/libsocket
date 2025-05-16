/*
 * @file socket.hpp
 * @brief Modern cross-platform TCP/UDP/Unix socket abstraction for C++17 (Windows, Linux, POSIX).
 *
 * Provides the ServerSocket, Socket, DatagramSocket, and UnixSocket (POSIX) classes for easy, Java-like socket programming.
 * Handles platform differences, error reporting, and supports both TCP, UDP, and Unix domain sockets.
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

#include <iostream>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32

	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <iphlpapi.h>//GetAdaptersInfo
	#include <versionhelpers.h>
	#if defined(_MSC_VER) && (_WIN32_WINNT >= 0x0A00)
		#include <afunix.h> // Windows 10+ AF_UNIX support
	#endif

	//https://msdn.microsoft.com/en-us/library/6sehtctf.aspx
	#if !defined(WINVER) || (WINVER < 0x0501)
		#error WINVER must be defined to something equal or above to 0x0501 // Win XP
	#endif // WINVER
	#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0501)
		#error _WIN32_WINNT must be defined to something equal or above to 0x0501 // Win XP
	#endif // _WIN32_WINNT

	/* If compiled with Visual C++
	 * https://msdn.microsoft.com/en-us/library/windows/desktop/ms737629%28v=vs.85%29.aspx
	 * http://stackoverflow.com/questions/3484434/what-does-pragma-comment-mean
	 * http://stackoverflow.com/questions/70013/how-to-detect-if-im-compiling-code-with-visual-studio-2008
	 */
	#ifdef _MSC_VER
		#pragma comment(lib, "Ws2_32.lib")
		#pragma comment(lib, "iphlpapi.lib")////GetAdaptersInfo
	#endif

#else

	//Assuming Linux
	#include <unistd.h>
	#include <errno.h>//errno
	#include <cstring>//strerror
	#include <sys/ioctl.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <netinet/in.h>
	#include <netdb.h>//for struct addrinfo
	#include <arpa/inet.h>//for inet_ntoa
	#include <ifaddrs.h>//getifaddrs
	#if !defined(_WIN32)
	#include <sys/un.h>
	#endif
#endif

namespace sock{

#ifdef _WIN32
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#include <sys/types.h>
#include <sys/un.h>
#include <versionhelpers.h>
	inline int InitSockets(){ WSADATA WSAData; return WSAStartup(MAKEWORD(2,2),&WSAData); }
	inline int CleanupSockets(){ return WSACleanup(); }
	inline int GetSocketError(){ return WSAGetLastError(); }
	inline int CloseSocket(SOCKET fd) { return closesocket(fd); }

	const char *inet_ntop_aux(int af, const void *src, char *dst, socklen_t size);

#else

	typedef int SOCKET;
	constexpr SOCKET INVALID_SOCKET = -1;
	constexpr SOCKET SOCKET_ERROR = -1;

	constexpr int InitSockets(){ return 0; }
	constexpr int CleanupSockets(){ return 0; }
	inline int GetSocketError(){ return errno; }
	inline int CloseSocket(SOCKET fd){ return close(fd); }

	inline int ioctlsocket(SOCKET fd,long cmd,u_long *argp){ return ioctl(fd,static_cast<unsigned long>(cmd),argp); }

#endif

	std::string SocketErrorMessage(int error);
	std::string SocketErrorMessageWrap(int error);

	/**
	 * @brief Exception class for socket errors.
	 *
	 * Stores an error code and a descriptive error message. Thrown by all socket operations on error.
	 */
	class socket_exception : public std::exception {
		public:
			explicit socket_exception(int code, std::string message = "socket_exception")
				: std::exception(), error_code(code), error_message(message) {
				error_message += " (" + std::to_string(code) + ")";
			}
			const char *what() const noexcept
			{
				return error_message.c_str();
			}
			int get_error_code() const noexcept
			{
				return error_code;
			}
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
	class SocketInitializer {
	public:
		inline SocketInitializer(){
			if(InitSockets() != 0) throw socket_exception( GetSocketError(), SocketErrorMessage(GetSocketError()) );
		}
		inline ~SocketInitializer() noexcept {
			if(CleanupSockets() != 0) std::cerr << "Socket cleanup failed: " << SocketErrorMessageWrap(GetSocketError()) << ": " << GetSocketError() << std::endl;
		}

		SocketInitializer(const SocketInitializer &rhs)=delete;
		SocketInitializer &operator=(const SocketInitializer &rhs)=delete;
	};

	/**
	 * @brief TCP client socket abstraction (Java-like interface).
	 *
	 * Provides connect, read, write, close, and address info. Handles both IPv4 and IPv6.
	 *
	 * @note Not thread-safe. Each socket should only be used from one thread at a time.
	 */
	class Socket {
		friend class ServerSocket;
		private:
			SOCKET clientSocket = INVALID_SOCKET;
			struct sockaddr_storage remote_addr; ///< sockaddr_in for IPv4; sockaddr_in6 for IPv6; sockaddr_storage for both (portability)
			mutable socklen_t remote_addr_length = 0;
			struct addrinfo *cli_addrinfo = nullptr;
			std::vector<char> buffer = std::vector<char>(512);
		protected:
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
			//move constructor
			Socket(const Socket& rhs)=delete; //-Weffc++
			Socket(Socket&& rhs) noexcept :
				clientSocket(rhs.clientSocket),
				remote_addr(rhs.remote_addr),
				remote_addr_length(rhs.remote_addr_length),
				cli_addrinfo(rhs.cli_addrinfo),
				buffer(std::move(rhs.buffer))
			{
				rhs.clientSocket = INVALID_SOCKET;
				rhs.cli_addrinfo = nullptr;
			}
			//move assignment function
			Socket &operator=(const Socket& rhs)=delete; //-Weffc++
			Socket& operator=(Socket&& rhs) noexcept {
				if (this != &rhs) {
					if (this->clientSocket != INVALID_SOCKET) {
						if (CloseSocket(this->clientSocket))
							std::cerr << "closesocket() failed: " << SocketErrorMessage(GetSocketError()) << ": " << GetSocketError() << std::endl;
					}
					if (cli_addrinfo) {
						freeaddrinfo(cli_addrinfo);
						cli_addrinfo = nullptr;
					}
					clientSocket = rhs.clientSocket;
					remote_addr = rhs.remote_addr;
					remote_addr_length = rhs.remote_addr_length;
					cli_addrinfo = rhs.cli_addrinfo;
					buffer = std::move(rhs.buffer);
					rhs.clientSocket = INVALID_SOCKET;
					rhs.cli_addrinfo = nullptr;
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
			 * @brief Connect to the remote host.
			 * @throws socket_exception on failure.
			 */
			void connect();
			/**
			 * @brief Read a trivially copyable type from the socket.
			 * @tparam T Type to read (must be trivially copyable).
			 * @return Value of type T read from the socket.
			 * @throws socket_exception on error or disconnect.
			 */
			template <typename T>
			T read() {
				static_assert(std::is_trivially_copyable<T>::value, "Socket::read<T>() only supports trivially copyable types");
				T r;
				int len = recv(this->clientSocket, reinterpret_cast<char*>(&r), sizeof(T), 0);
				if (len == SOCKET_ERROR) throw socket_exception( GetSocketError(), SocketErrorMessage(GetSocketError()) );
				if (len == 0) throw socket_exception(0, "Connection closed by remote host.");
				return r;
			}
			/**
			 * @brief Close the socket.
			 * @throws socket_exception on error.
			 */
			void close();
			/**
			 * @brief Shutdown the socket for both send and receive.
			 * @throws socket_exception on error.
			 */
			void shutdown();
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
			 * @brief Gracefully shutdown the socket for reading, writing, or both.
			 *
			 * @param how 0 = shutdown read, 1 = shutdown write, 2 = shutdown both.
			 * @throws socket_exception on error.
			 */
			void shutdown(int how);

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
			 * @brief Set the address family preference (IPv4, IPv6, or both).
			 *
			 * @param family AF_INET, AF_INET6, or AF_UNSPEC.
			 */
			// void setAddressFamily(int family); // Not implemented

			/**
			 * @brief Check if the socket is still connected (TCP only).
			 *
			 * @return true if connected, false otherwise.
			 */
			bool isConnected() const;

			/**
			 * @brief Enable or disable TCP_NODELAY (Nagle's algorithm).
			 *
			 * @param enable true to disable Nagle (enable TCP_NODELAY), false to enable Nagle.
			 */
			void enableNoDelay(bool enable);

			/**
			 * @brief Enable or disable SO_KEEPALIVE.
			 *
			 * @param enable true to enable keepalive, false to disable.
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
	class ServerSocket {
		private:
			SOCKET serverSocket = INVALID_SOCKET;
			struct addrinfo *srv_addrinfo = nullptr;
			unsigned short port;
		public:
			/**
			 * @brief Construct a server socket bound to a port.
			 * @param port TCP port to listen on.
			 * @throws socket_exception on failure.
			 */
			explicit ServerSocket(unsigned short port);
			//move constructor
			ServerSocket(const ServerSocket& rhs)=delete; //-Weffc++
			ServerSocket(ServerSocket&& rhs) noexcept :
				serverSocket(rhs.serverSocket),
				srv_addrinfo(rhs.srv_addrinfo),
				port(rhs.port)
			{
				rhs.serverSocket = INVALID_SOCKET;
				rhs.srv_addrinfo = nullptr;
			}
			ServerSocket &operator=(const ServerSocket& rhs)=delete; //-Weffc++
			/**
			 * @brief Destructor. Closes the server socket and frees resources.
			 */
			~ServerSocket();
			/**
			 * @brief Bind the server socket to the configured port.
			 * @throws socket_exception on error.
			 */
			void bind();
			/**
			 * @brief Start listening for incoming connections.
			 * @throws socket_exception on error.
			 */
			void listen();
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

	template<>
	inline std::string Socket::read(){
		DIAGNOSTIC_PUSH()
		DIAGNOSTIC_IGNORE("-Wuseless-cast")
		int len = static_cast<int>(recv(clientSocket, buffer.data(), buffer.size(), 0));
		DIAGNOSTIC_POP()
		if (len == SOCKET_ERROR) throw socket_exception( GetSocketError(), SocketErrorMessage(GetSocketError()) );
		if (len == 0) throw socket_exception(0, "Connection closed by remote host.");
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
	class DatagramSocket {
	private:
		SOCKET sockfd = INVALID_SOCKET; ///< Underlying socket file descriptor.
		struct sockaddr_storage local_addr{}; ///< Local address structure.
		mutable socklen_t local_addr_length = 0; ///< Length of local address.
		struct addrinfo* addrinfo_ptr = nullptr; ///< Address info pointer for bind/connect.
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

#if defined(_WIN32)
/**
 * @brief Unix domain socket abstraction for POSIX and Windows 10+ (AF_UNIX support).
 *
 * On POSIX, uses native AF_UNIX sockets. On Windows, only available on Windows 10 (version 1803, build 17134) and later.
 * Uses AF_UNIX support in Winsock2. Fails gracefully on older Windows.
 *
 * @note Not thread-safe. Each UnixSocket should only be used from one thread at a time.
 */
class UnixSocket {
private:
#if defined(_WIN32)
    SOCKET sockfd = INVALID_SOCKET;
    std::string path;
    bool is_server = false;
#else
    int sockfd = -1;
    struct sockaddr_un addr{};
    std::vector<char> buffer = std::vector<char>(512);
#endif
public:
    UnixSocket();
    explicit UnixSocket(const std::string& path);
    ~UnixSocket() noexcept;

    void bind(const std::string& path);
    void listen(int backlog = 5);
    UnixSocket accept();
    void connect(const std::string& path);
    int write(std::string_view data);
    std::string read();
    void close();
    bool isValid() const;
};
#endif

#if defined(_WIN32) && (!defined(_MSC_VER) || (_WIN32_WINNT < 0x0A00))
// Stub UnixSocket for unsupported Windows versions
class UnixSocket {
public:
    UnixSocket() { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    explicit UnixSocket(const std::string&) { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    ~UnixSocket() noexcept {}
    void bind(const std::string&) { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    void listen(int = 5) { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    UnixSocket accept() { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    void connect(const std::string&) { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    int write(std::string_view) { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    std::string read() { throw socket_exception(-1, "AF_UNIX not supported on this Windows version"); }
    void close() {}
    bool isValid() const { return false; }
};
#endif
} // namespace sock
