// Modern libsocket server test: C++17, comments, and feature coverage
#include "libsocket/socket.hpp"
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

using namespace std;
using namespace libsocket;

/**
 * @brief Test TCP server functionality: accept, receive, send, close.
 */
void test_tcp(int port)
{
    cout << "[TCP] Starting server on port " << port << endl;
    ServerSocket serverSocket(port);
    serverSocket.bind();
    serverSocket.listen();
    cout << "[TCP] Waiting for client..." << endl;
    Socket conn = serverSocket.accept();
    cout << "[TCP] Client connected from: " << conn.getRemoteSocketAddress() << endl;
    string msg = conn.read<string>();
    cout << "[TCP] Client says: " << msg << endl;
    conn.write("Hello client! (TCP)");
    conn.close();
    serverSocket.close();
}

/**
 * @brief Test UDP server functionality: receive, send, close.
 */
void test_udp(int port)
{
    cout << "[UDP] Starting UDP server on port " << port << endl;
    DatagramSocket udp(port);
    udp.setTimeout(5000);
    udp.setNonBlocking(false);
    vector<char> buf(512);
    string sender;
    unsigned short senderPort;
    int n = udp.recvFrom(buf.data(), buf.size(), sender, senderPort);
    cout << "[UDP] Got " << n << " bytes from " << sender << ": " << string(buf.data(), n) << endl;
    string reply = "Hello client! (UDP)";
    udp.sendTo(reply.data(), reply.size(), sender, senderPort);
    udp.close();
}

#ifndef _WIN32
/**
 * @brief Test Unix domain socket server functionality (POSIX only).
 */
void test_unix(const string& path)
{
    cout << "[UNIX] Starting Unix domain socket server at " << path << endl;
    UnixSocket usock;
    usock.bind(path);
    usock.listen();
    cout << "[UNIX] Waiting for client..." << endl;
    UnixSocket client = usock.accept();
    string msg = client.read();
    cout << "[UNIX] Client says: " << msg << endl;
    client.write("Hello client! (UNIX)");
    client.close();
    usock.close();
    unlink(path.c_str());
}
#endif

/**
 * @brief Test error handling by attempting to bind to an invalid port.
 */
void test_error_handling()
{
    cout << "[ERROR] Testing error handling..." << endl;
    try
    {
        ServerSocket bad(0);
        bad.bind();
        bad.listen();
    }
    catch (const socket_exception& se)
    {
        cout << "[ERROR] Caught expected: " << se.what() << endl;
    }
}

int main()
{
    SocketInitializer sockInit;
    int port;
    cout << "Type a port to start listening at: ";
    while (true)
    {
        string input;
        getline(cin, input);
        stringstream myStream(input);
        if (myStream >> port && port >= 0 && port <= 65535)
            break;
        cout << "Error: Invalid port number. Port must be between 0 and 65535." << endl;
    }
    try
    {
        test_tcp(port);
        test_udp(port + 1); // UDP server on port+1
#ifndef _WIN32
        test_unix("/tmp/libsocket_test.sock");
#endif
        test_error_handling();
    }
    catch (const socket_exception& se)
    {
        cerr << "[FATAL] Error code: " << se.get_error_code() << endl;
        cerr << "[FATAL] Error message: " << se.what() << endl;
        return 1;
    }
    cout << "All tests completed successfully." << endl;
    return 0;
}