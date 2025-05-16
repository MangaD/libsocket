// Modern libsocket client test: C++17, comments, and feature coverage
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>
#include "socket.hpp"

using namespace std;
using namespace sock;

/**
 * @brief Test TCP client functionality: connect, send, receive, close.
 */
void test_tcp(const string& ip, int port) {
    cout << "[TCP] Connecting to " << ip << ":" << port << endl;
    Socket conn(ip, port);
    conn.setTimeout(2000); // 2s timeout
    conn.setNonBlocking(false);
    conn.connect();
    conn.write("Hello server! (TCP)");
    string response = conn.read<string>();
    cout << "[TCP] Server says: " << response << endl;
    conn.close();
}

/**
 * @brief Test UDP client functionality: send, receive, close.
 */
void test_udp(const string& ip, int port) {
    cout << "[UDP] Sending to " << ip << ":" << port << endl;
    DatagramSocket udp;
    udp.setTimeout(2000);
    udp.setNonBlocking(false);
    string msg = "Hello server! (UDP)";
    udp.sendTo(msg.data(), msg.size(), ip, port);
    string sender;
    unsigned short senderPort;
    vector<char> buf(512);
    int n = udp.recvFrom(buf.data(), buf.size(), sender, senderPort);
    cout << "[UDP] Got " << n << " bytes from " << sender << ": " << string(buf.data(), n) << endl;
    udp.close();
}

#ifndef _WIN32
/**
 * @brief Test Unix domain socket client functionality (POSIX only).
 */
void test_unix(const string& path) {
    cout << "[UNIX] Connecting to " << path << endl;
    UnixSocket usock;
    usock.connect(path);
    usock.write("Hello server! (UNIX)");
    string response = usock.read();
    cout << "[UNIX] Server says: " << response << endl;
    usock.close();
}
#endif

/**
 * @brief Test error handling by attempting to connect to an invalid address.
 */
void test_error_handling() {
    cout << "[ERROR] Testing error handling..." << endl;
    try {
        Socket bad("256.256.256.256", 12345);
        bad.connect();
    } catch (const socket_exception& se) {
        cout << "[ERROR] Caught expected: " << se.what() << endl;
    }
}

int main() {
    SocketInitializer sockInit;
    string ip;
    int port;
    cout << "Type the IP to connect to (127.0.0.1 for this machine): ";
    getline(cin, ip);
    cout << "Type the port to connect to: ";
    while (true) {
        string input;
        getline(cin, input);
        stringstream myStream(input);
        if (myStream >> port && port >= 0 && port <= 65535) break;
        cout << "Error: Invalid port number. Port must be between 0 and 65535." << endl;
    }
    try {
        test_tcp(ip, port);
        test_udp(ip, port + 1); // Assume UDP server is on port+1
#ifndef _WIN32
        test_unix("/tmp/libsocket_test.sock");
#endif
        test_error_handling();
    } catch (const socket_exception& se) {
        cerr << "[FATAL] Error code: " << se.get_error_code() << endl;
        cerr << "[FATAL] Error message: " << se.what() << endl;
        return 1;
    }
    cout << "All tests completed successfully." << endl;
    return 0;
}