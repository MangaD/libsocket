/*
 * @file socket.cpp
 * @brief Implementation of modern cross-platform TCP/UDP/Unix socket abstraction for C++17 (Windows, Linux, POSIX).
 *
 * Implements the ServerSocket, Socket, DatagramSocket, and UnixSocket (POSIX) classes for easy, Java-like socket programming.
 * Handles platform differences and error reporting.
 *
 * Author: David Gonçalves (MangaD)
 */

#include "socket.hpp"

#include <exception>//SocketErrorMessageWrap
#include <cstring>//Use std::memset()
#include <memory>

using namespace sock;

/**
 * @brief Get a human-readable error message for a socket error code.
 * @param error The error code (platform-specific).
 * @return Error message string.
 */
#ifdef _WIN32
std::string sock::SocketErrorMessage(int error) {
	if(error == 0)
		return std::string();

	LPSTR buffer = nullptr;
	DWORD size; // Use DWORD for FormatMessageA

	if( (size = FormatMessageA(
	        FORMAT_MESSAGE_ALLOCATE_BUFFER |
	        FORMAT_MESSAGE_FROM_SYSTEM |
	        FORMAT_MESSAGE_IGNORE_INSERTS,
	        nullptr,
	        error,
	        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
	        (LPSTR) &buffer, // Fix: use LPSTR, not LPTSTR
	        0,
	        nullptr ))
	   == 0)
	{
		std::cerr << "Format message failed: " << GetLastError() << std::endl;
		SetLastError(error);
		return std::string();
	}
	std::string errString(buffer, size);
	LocalFree(buffer);

	return errString;
}
#else
std::string sock::SocketErrorMessage(int error) {
	return std::strerror(error);
}
#endif

/**
 * @brief Get a human-readable error message for a socket error code, wrapped with exception safety.
 * @param error The error code (platform-specific).
 * @return Error message string.
 */
std::string sock::SocketErrorMessageWrap(int error) {
	std::string errString{};
	try {
		errString = SocketErrorMessage(error);
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	return errString;
}

// --- ServerSocket Implementation ---

//ServerSocket Constructor - creates a server socket
/**
 * @brief Construct a server socket bound to a port.
 * @param port_ TCP port to listen on.
 * @throws socket_exception on failure.
 */
ServerSocket::ServerSocket (unsigned short port_)
    : port(port_), srv_addrinfo(nullptr), serverSocket(INVALID_SOCKET)
{
	//The hints argument points to an addrinfo structure that specifies criteria for
	//selecting the socket address structures returned in the list by getaddrinfo
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; //IPv4 (AF_INET) or IPv6 (AF_INET6)
	hints.ai_socktype = SOCK_STREAM; //Used with TCP protocol
	hints.ai_protocol = IPPROTO_TCP;

	/* If the AI_PASSIVE flag is specified in hints.ai_flags, and node is
	 * NULL, then the returned socket addresses will be suitable for
	 * bind(2)ing a socket that will accept(2) connections.  The returned
	 * socket address will contain the "wildcard address" (INADDR_ANY for
	 * IPv4 addresses, IN6ADDR_ANY_INIT for IPv6 address).  The wildcard
	 * address is used by applications (typically servers) that intend to
	 * accept connections on any of the hosts's network addresses.  If node
	 * is not NULL, then the AI_PASSIVE flag is ignored.
	 */
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	std::string s = std::to_string(port_);
	if (getaddrinfo(nullptr, s.c_str(), &hints, &srv_addrinfo) != 0)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));

	// Try all available addresses (IPv6/IPv4) to create a SOCKET for
	// the server to listen for client connections
	for (struct addrinfo* p = srv_addrinfo; p != nullptr; p = p->ai_next) {
		if (serverSocket != INVALID_SOCKET) CloseSocket(serverSocket);
		serverSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (serverSocket == INVALID_SOCKET) continue;
		if (p->ai_family == AF_INET6) {
			int OptionValue = 0; // So that it also accepts IPv4 connections.
			if (setsockopt(serverSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&OptionValue, sizeof(OptionValue)) == SOCKET_ERROR) {
				int error = GetSocketError();
				freeaddrinfo(srv_addrinfo); srv_addrinfo = nullptr;
				throw socket_exception(error, SocketErrorMessage(error));
			}
		}
		break;
	}
	if (serverSocket == INVALID_SOCKET) {
		int error = GetSocketError();
		freeaddrinfo(srv_addrinfo); srv_addrinfo = nullptr;
		throw socket_exception(error, SocketErrorMessage(error));
	}
	int optval = 1;
#ifdef _WIN32
	//Otherwise, it appears Windows would let the socket bind to a port already in use...
	//https://msdn.microsoft.com/en-us/library/windows/desktop/ms740668%28v=vs.85%29.aspx#WSAEADDRINUSE
	if(setsockopt(serverSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,(const char*) &optval, sizeof optval) == SOCKET_ERROR)
		throw socket_exception ( GetSocketError(), SocketErrorMessage(GetSocketError()) );
#else
	// Let the socket bind to a port that is not active. Otherwise "The reason you can't normally listen on the same port
	// right away is because the socket, though closed, remains in the 2MSL state for some amount of time (generally a few minutes).     
	// http://stackoverflow.com/questions/4979425/difference-between-address-in-use-with-bind-in-windows-and-on-linux-errno
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof optval) == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

/**
 * @brief Destructor. Closes the server socket and frees resources.
 */
ServerSocket::~ServerSocket() noexcept {
	try {
		close();
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
	if (srv_addrinfo) {
		freeaddrinfo(srv_addrinfo);
		srv_addrinfo = nullptr;
	}
}

/**
 * @brief Close the server socket.
 * @throws socket_exception on error.
 */
void ServerSocket::close() {
	if (this->serverSocket != INVALID_SOCKET) {
		if (CloseSocket(this->serverSocket))
			throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
		else
			this->serverSocket = INVALID_SOCKET;
	}
}

/**
 * @brief Shutdown the server socket for both send and receive.
 * @throws socket_exception on error.
 */
void ServerSocket::shutdown() {
	int how;
#ifdef _WIN32
	how = SD_BOTH;
#else
	how = SHUT_RDWR;
#endif
	if (this->serverSocket != INVALID_SOCKET) {
		if( ::shutdown(this->serverSocket, how) )
			throw socket_exception ( GetSocketError(), SocketErrorMessageWrap(GetSocketError()) );
	}
}

/**
 * @brief Bind the server socket to the configured port.
 * @throws socket_exception on error.
 */
void ServerSocket::bind() {
	struct addrinfo *addr_in_use = nullptr;
	for(struct addrinfo *p = srv_addrinfo; p != nullptr; p = p->ai_next) {
		if(p->ai_family == AF_INET)
			addr_in_use = p;
		else if(p->ai_family == AF_INET6){//Use IPv6 address if it exists.
			addr_in_use = p;
			break;
		}
	}
	if(addr_in_use == nullptr) throw socket_exception (0,"bind() invalid addrinfo");
	int res = ::bind( serverSocket, addr_in_use->ai_addr, addr_in_use->ai_addrlen);
	if (res == SOCKET_ERROR) {
		throw socket_exception ( GetSocketError(), SocketErrorMessage(GetSocketError()) );
	}
}

/**
 * @brief Start listening for incoming connections.
 * @throws socket_exception on error.
 */
void ServerSocket::listen() {
	int n = ::listen(serverSocket, SOMAXCONN); // SOMAXCONN - how many clients can be in queue.
	if (n == SOCKET_ERROR)
		throw socket_exception ( GetSocketError(), SocketErrorMessage(GetSocketError()) );
}

/**
 * @brief Accept an incoming connection.
 * @return A Socket object for the client.
 * @throws socket_exception on error.
 */
Socket ServerSocket::accept() {
	//Create structure to hold client address (optional)
	struct sockaddr_storage cli_addr;
	socklen_t clilen;
	clilen = sizeof(cli_addr);

	SOCKET clientSocket = ::accept(serverSocket, (struct sockaddr *)&cli_addr, &clilen);
	if (clientSocket == INVALID_SOCKET)
		throw socket_exception ( GetSocketError(), SocketErrorMessage(GetSocketError()) );
	Socket client(clientSocket, cli_addr, clilen);

	return client;
}

// --- Socket Implementation ---

//Socket Constructor - creates a client socket from the accept function of ServerSocket
Socket::Socket(SOCKET client, struct sockaddr_storage addr, socklen_t len)
    : remote_addr(addr), remote_addr_length(len)
{
	clientSocket = client;
}


/**
 * @brief Construct a Socket for a given host and port.
 * @param host Hostname or IP address to connect to.
 * @param port TCP port number.
 * @param bufferSize Size of the internal read buffer (default: 512).
 * @throws socket_exception on failure.
 */
Socket::Socket(std::string host, unsigned short port, std::size_t bufferSize)
    : remote_addr{}, remote_addr_length{0}, cli_addrinfo(nullptr), buffer(bufferSize)
{
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the local address and port to be used by the server
	std::string s = std::to_string(port);
	if (getaddrinfo(host.c_str(), s.c_str(), &hints, &cli_addrinfo) != 0)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
	clientSocket = INVALID_SOCKET;
	for (struct addrinfo* p = cli_addrinfo; p != nullptr; p = p->ai_next) {
		clientSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (clientSocket != INVALID_SOCKET) break;
	}
	if (clientSocket == INVALID_SOCKET) {
		int error = GetSocketError();
		freeaddrinfo(cli_addrinfo); cli_addrinfo = nullptr;
		throw socket_exception(error, SocketErrorMessage(error));
	}

}

/**
 * @brief Connect the socket to the remote host:port.
 * @throws socket_exception on error.
 */
void Socket::connect() {
	// Should really try the next address returned by getaddrinfo
	// if the connect call failed
	struct addrinfo *p;
	for(p = cli_addrinfo; p != nullptr; p = p->ai_next) {
		// Connect to server.
		int res = ::connect( clientSocket, p->ai_addr, p->ai_addrlen);
		if (res == SOCKET_ERROR && p->ai_next == nullptr) {
			throw socket_exception ( GetSocketError(), SocketErrorMessage(GetSocketError()) );
		} else if (res == SOCKET_ERROR) continue;
		else break;
	}
}

/**
 * @brief Destructor. Closes the socket and frees resources.
 */
Socket::~Socket() noexcept {
	try{
		close();
	}
	catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
	if (cli_addrinfo) {
		freeaddrinfo(cli_addrinfo);
		cli_addrinfo = nullptr;
	}
}

/**
 * @brief Close the socket.
 * @throws socket_exception on error.
 */
void Socket::close() {
	if (this->clientSocket != INVALID_SOCKET) {
		if (CloseSocket(this->clientSocket))
			throw socket_exception(GetSocketError(), SocketErrorMessageWrap(GetSocketError()));
		else
			this->clientSocket = INVALID_SOCKET;
	}
}

/**
 * @brief Shutdown the socket for both send and receive.
 * @throws socket_exception on error.
 */
void Socket::shutdown() {
	int how;
#ifdef _WIN32
	how = SD_BOTH;
#else
	how = SHUT_RDWR;
#endif
	if (this->clientSocket != INVALID_SOCKET) {
		if( ::shutdown(this->clientSocket, how) )
			throw socket_exception ( GetSocketError(), SocketErrorMessageWrap(GetSocketError()) );
	}
}

/**
 * @brief Get the remote socket's address as a string (ip:port).
 * @return String representation of the remote address.
 */
//http://www.microhowto.info/howto/convert_an_ip_address_to_a_human_readable_string_in_c.html
std::string Socket::getRemoteSocketAddress() const {
	std::string ip_port = "null";
	if (remote_addr_length > 0) {
		if (remote_addr.ss_family == AF_INET6) {
			struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&remote_addr;
			if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
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
		getnameinfo((struct sockaddr*)&remote_addr, remote_addr_length, ip_s, sizeof(ip_s), port_s, sizeof(port_s), NI_NUMERICHOST | NI_NUMERICSERV);
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
int Socket::write(std::string_view message) {
    int flags = 0;
#ifndef _WIN32
    flags = MSG_NOSIGNAL;
#endif
    int len = static_cast<int>(send(clientSocket, message.data(), message.size(), flags));
    if (len == SOCKET_ERROR) throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return len;
}

/**
 * @brief Set the internal buffer size.
 * @param newLen New buffer size in bytes.
 */
void Socket::setBufferSize(std::size_t newLen) {
	buffer.resize(newLen);
	buffer.shrink_to_fit();
}

void Socket::setNonBlocking(bool nonBlocking) {
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

// --- Fix Windows setTimeout: use int not timeval ---
void Socket::setTimeout(int millis, bool forConnect) {
#ifdef _WIN32
	if (forConnect) {
		// No direct connect timeout in WinSock; use non-blocking connect + select in connect()
		return;
	}
	int timeout = millis;
	if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR ||
	    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
	timeval tv{ millis / 1000, (millis % 1000) * 1000 };
	if (forConnect) {
		// No direct connect timeout in POSIX; use non-blocking connect + select in connect()
		return;
	}
	if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR ||
	    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

// --- Fix isConnected: remove MSG_DONTWAIT, use platform-specific non-blocking check ---
bool Socket::isConnected() const {
	if (clientSocket == INVALID_SOCKET) return false;
	char buf;
#ifdef _WIN32
	u_long bytesAvailable = 0;
	if (ioctlsocket(clientSocket, FIONREAD, &bytesAvailable) == SOCKET_ERROR)
		return false;
	if (bytesAvailable > 0) return true;
	// Try a non-blocking recv with MSG_PEEK
	u_long mode = 1;
	ioctlsocket(clientSocket, FIONBIO, &mode);
	int ret = recv(clientSocket, &buf, 1, MSG_PEEK);
	mode = 0;
	ioctlsocket(clientSocket, FIONBIO, &mode);
	if (ret == 0) return false; // connection closed
	if (ret < 0) {
		int err = WSAGetLastError();
		return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
	}
	return true;
#else
	int flags = fcntl(clientSocket, F_GETFL, 0);
	bool wasNonBlocking = flags & O_NONBLOCK;
	if (!wasNonBlocking) fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
	int ret = recv(clientSocket, &buf, 1, MSG_PEEK);
	if (!wasNonBlocking) fcntl(clientSocket, F_SETFL, flags);
	if (ret == 0) return false;
	if (ret < 0) return errno == EWOULDBLOCK || errno == EAGAIN;
	return true;
#endif
}

// --- DatagramSocket Implementation ---
DatagramSocket::DatagramSocket() {}

DatagramSocket::DatagramSocket(unsigned short port) {
	bind(port);
}

DatagramSocket::DatagramSocket(const std::string& host, unsigned short port) {
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

DatagramSocket::~DatagramSocket() noexcept {
	close();
	if (addrinfo_ptr) {
		freeaddrinfo(addrinfo_ptr);
		addrinfo_ptr = nullptr;
	}
}

void DatagramSocket::bind(unsigned short port) {
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

int DatagramSocket::sendTo(const void* data, std::size_t len, const std::string& host, unsigned short port) {
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	struct addrinfo* destinfo = nullptr;
	std::string s = std::to_string(port);
	if (getaddrinfo(host.c_str(), s.c_str(), &hints, &destinfo) != 0)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
	int sent = static_cast<int>(sendto(sockfd, reinterpret_cast<const char*>(data), static_cast<int>(len), 0, destinfo->ai_addr, destinfo->ai_addrlen));
	freeaddrinfo(destinfo);
	if (sent == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
	return sent;
}

int DatagramSocket::recvFrom(void* data, std::size_t len, std::string& senderAddr, unsigned short& senderPort) {
	sockaddr_storage src_addr;
	socklen_t addrlen = sizeof(src_addr);
	int recvd = static_cast<int>(recvfrom(sockfd, reinterpret_cast<char*>(data), static_cast<int>(len), 0, (sockaddr*)&src_addr, &addrlen));
	if (recvd == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
	char host[INET6_ADDRSTRLEN] = {0};
	char serv[6] = {0};
	getnameinfo((sockaddr*)&src_addr, addrlen, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
	senderAddr = host;
	senderPort = static_cast<unsigned short>(std::stoi(serv));
	return recvd;
}

void DatagramSocket::close() {
	if (sockfd != INVALID_SOCKET) {
		CloseSocket(sockfd);
		sockfd = INVALID_SOCKET;
	}
}

void DatagramSocket::setNonBlocking(bool nonBlocking) {
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

void DatagramSocket::setTimeout(int millis) {
#ifdef _WIN32
	int timeout = millis;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR ||
	    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#else
	timeval tv{ millis / 1000, (millis % 1000) * 1000 };
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR ||
	    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == SOCKET_ERROR)
		throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
#endif
}

void DatagramSocket::setOption(int level, int optname, int value) {
    if (setsockopt(sockfd, level, optname, reinterpret_cast<const char*>(&value), sizeof(value)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
}

int DatagramSocket::getOption(int level, int optname) const {
    int value = 0;
    socklen_t len = sizeof(value);
    if (getsockopt(sockfd, level, optname, reinterpret_cast<char*>(&value), &len) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), SocketErrorMessage(GetSocketError()));
    return value;
}

std::string DatagramSocket::getLocalSocketAddress() const {
	char host[INET6_ADDRSTRLEN] = {0};
	char serv[6] = {0};
	sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	if (getsockname(sockfd, (sockaddr*)&addr, &len) == 0) {
		getnameinfo((sockaddr*)&addr, len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
		return std::string(host) + ":" + serv;
	}
	return "null";
}

#ifdef _WIN32
// Windows 10+ AF_UNIX support for UnixSocket
#include <sys/types.h>
#include <sys/un.h>

using namespace sock;

bool is_windows10_or_greater() {
    // Windows 10 version 1803 (build 17134) or later
    return IsWindows10OrGreater();
}

UnixSocket::UnixSocket() = default;
UnixSocket::UnixSocket(const std::string& path) { connect(path); }
UnixSocket::~UnixSocket() noexcept { close(); }

void UnixSocket::bind(const std::string& path_) {
    if (!is_windows10_or_greater())
        throw socket_exception(-1, "AF_UNIX not supported on this Windows version");
    path = path_;
    sockfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), "Failed to create AF_UNIX socket");
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), "Failed to bind AF_UNIX socket");
    is_server = true;
}
void UnixSocket::listen() {
    if (::listen(sockfd, SOMAXCONN) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), "Failed to listen on AF_UNIX socket");
}
UnixSocket UnixSocket::accept() {
    sockaddr_un addr{};
    int len = sizeof(addr);
    SOCKET client = ::accept(sockfd, reinterpret_cast<sockaddr*>(&addr), &len);
    if (client == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), "Failed to accept AF_UNIX connection");
    UnixSocket s;
    s.sockfd = client;
    return s;
}
void UnixSocket::connect(const std::string& path_) {
    if (!is_windows10_or_greater())
        throw socket_exception(-1, "AF_UNIX not supported on this Windows version");
    path = path_;
    sockfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET)
        throw socket_exception(GetSocketError(), "Failed to create AF_UNIX socket");
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), "Failed to connect AF_UNIX socket");
}
int UnixSocket::write(const std::string& msg) {
    int n = ::send(sockfd, msg.data(), static_cast<int>(msg.size()), 0);
    if (n == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), "AF_UNIX send failed");
    return n;
}
std::string UnixSocket::read() {
    char buf[512];
    int n = ::recv(sockfd, buf, sizeof(buf), 0);
    if (n == SOCKET_ERROR)
        throw socket_exception(GetSocketError(), "AF_UNIX recv failed");
    return std::string(buf, n);
}
void UnixSocket::close() noexcept {
    if (sockfd != INVALID_SOCKET) {
        CloseSocket(sockfd);
        sockfd = INVALID_SOCKET;
    }
    if (is_server && !path.empty()) {
        ::remove(path.c_str());
    }
}
#if defined(_WIN32)
bool UnixSocket::isValid() const { return sockfd != INVALID_SOCKET; }
#else
bool UnixSocket::isValid() const { return sockfd != -1; }
#endif
#endif

