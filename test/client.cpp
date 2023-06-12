#include <iostream>
#include <sstream>//stringstream
#include "socket.hpp"

using namespace std;
using namespace sock;///Use namespace sock

int main() {
	///Get ip and port from user input
	string ip;
	int port;
	cout << "Type the IP to connect to (127.0.0.1 for this machine): ";
	getline(std::cin, ip);
	cout << "Type the port to connect to: ";
	while(true) {
		string input;
		getline(std::cin, input);
		stringstream myStream(input);
		if(myStream >> port && port >= 0 && port <= 65535) {
			break;
		}
		cout << "Error: Invalid port number. Port must be between 0 and 65535." << endl;
	}
	///Create Socket
	Socket conn(ip, port);
	try {
		///Connect to server
		conn.connect();
		///Write to server
		conn.write("Hello server!");
		///Read from server
		cout << "Server says: " << conn.read() << endl;
	}
	catch (socket_exception &se) {
		cerr << "Error code: " << se.get_error_code() << endl;
		cerr << "Error message: " << se.what() << endl;
		exit(0);
	}
	
	return 0;
}