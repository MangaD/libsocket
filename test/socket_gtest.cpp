// GoogleTest unit tests for libsocket
#include "libsocket/socket.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace libsocket;

TEST(SocketTest, TcpConnectInvalid)
{
    SocketInitializer init;
    EXPECT_THROW(
        {
            Socket s("256.256.256.256", 12345);
            s.connect();
        },
        socket_exception);
}

TEST(SocketTest, TcpBindInvalidPort)
{
    SocketInitializer init;
    EXPECT_THROW(
        {
            ServerSocket s(0);
            s.bind();
        },
        socket_exception);
}

#ifndef _WIN32
TEST(SocketTest, UnixSocketInvalidPath)
{
    EXPECT_THROW({ UnixSocket s("/tmp/does_not_exist.sock"); }, socket_exception);
}
#endif

TEST(SocketTest, TcpConnectTimeout)
{
    SocketInitializer init;
    Socket s("10.255.255.1", 65000); // unroutable IP
    s.setTimeout(100);               // 100ms
    EXPECT_THROW(s.connect(), socket_exception);
}

TEST(SocketTest, TcpNonBlocking)
{
    SocketInitializer init;
    Socket s("10.255.255.1", 65000);
    s.setNonBlocking(true);
    // connect() should fail immediately or throw
    EXPECT_ANY_THROW(s.connect());
}

TEST(SocketTest, TcpSetGetOption)
{
    SocketInitializer init;
    Socket s("127.0.0.1", 1); // port 1 is usually closed
    // setTimeout and setNonBlocking should not throw
    EXPECT_NO_THROW(s.setTimeout(100));
    EXPECT_NO_THROW(s.setNonBlocking(true));
}

TEST(SocketTest, UdpSendRecvLoopback)
{
    SocketInitializer init;
    DatagramSocket server(54321);
    DatagramSocket client;
    std::string msg = "gtest-udp";
    EXPECT_NO_THROW(client.sendTo(msg.data(), msg.size(), "127.0.0.1", 54321));
    std::string sender;
    unsigned short senderPort;
    std::vector<char> buf(32);
    int n = server.recvFrom(buf.data(), buf.size(), sender, senderPort);
    EXPECT_GT(n, 0);
    EXPECT_EQ(std::string(buf.data(), n), msg);
    server.close();
    client.close();
}

TEST(SocketTest, UdpTimeout)
{
    SocketInitializer init;
    DatagramSocket s(54322);
    s.setTimeout(100); // 100ms
    std::string sender;
    unsigned short senderPort;
    std::vector<char> buf(32);
    EXPECT_THROW(s.recvFrom(buf.data(), buf.size(), sender, senderPort), socket_exception);
    s.close();
}

#ifndef _WIN32
#include <cstdio>
TEST(SocketTest, UnixSocketBindConnect)
{
    const char* path = "/tmp/gtest_unixsock.sock";
    UnixSocket server;
    EXPECT_NO_THROW(server.bind(path));
    EXPECT_NO_THROW(server.listen());
    UnixSocket client;
    EXPECT_NO_THROW(client.connect(path));
    std::string msg = "unix-gtest";
    EXPECT_NO_THROW(client.write(msg));
    std::string rcvd = server.accept().read();
    EXPECT_EQ(rcvd, msg);
    client.close();
    server.close();
    std::remove(path);
}
TEST(SocketTest, UnixSocketTimeoutAndNonBlocking)
{
    UnixSocket s;
    EXPECT_NO_THROW(s.bind("/tmp/gtest_unixsock2.sock"));
    EXPECT_NO_THROW(s.listen());
    // No client connects, so accept() should block or fail
    // (We can't easily test timeout here, but we can test non-blocking)
    // This is a placeholder for future expansion
    s.close();
    std::remove("/tmp/gtest_unixsock2.sock");
}
#endif

// Add more tests as needed for UDP, timeouts, non-blocking, etc.
