# libsocket: Modern C++17 Cross-Platform Socket Library

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

libsocket is a modern, header-only C++17 library for cross-platform TCP, UDP, and Unix domain socket programming. It provides a clean, object-oriented API for both client and server sockets, supporting IPv4, IPv6, and local IPC, with robust error handling and full portability between Windows and Linux (and POSIX systems).

## Features

- **Modern C++17**: Uses `std::string_view`, move semantics, RAII, and type-safe APIs.
- **Cross-platform**: Works on Windows and Linux (and other POSIX systems).
- **TCP & UDP**: Easy-to-use classes for both stream and datagram sockets.
- **Unix domain sockets**: Local IPC support on POSIX systems via `UnixSocket`.
- **IPv4 & IPv6**: Transparent support for both address families.
- **Timeouts & Non-blocking**: Simple APIs for timeouts and non-blocking I/O.
- **Exception-based error handling**: All errors throw `socket_exception` with detailed messages.
- **Header-only API**: Just include and use (except for a small implementation file).
- **Well-documented**: Doxygen comments throughout the codebase.

## Directory Structure

```
libsocket/
├── CMakeLists.txt
├── Doxyfile
├── README.md
├── src/
│   ├── socket.hpp      # Main header (public API)
│   └── socket.cpp      # Implementation (for building static/shared lib)
├── test/
│   ├── client.cpp      # Comprehensive feature test client
│   ├── server.cpp      # Comprehensive feature test server
│   └── Makefile        # Test build rules
└── build/              # (Generated) Build artifacts
```

## Building

### With CMake (Recommended)

```sh
mkdir build
cd build
cmake ..
make
```

This will build `libsocket.a` (static library) in the root directory.

### Manual Build

You can also build manually:

```sh
g++ -std=c++17 -O2 -Wall -I./src -c src/socket.cpp -o socket.o
ar rcs libsocket.a socket.o
```

## Linking

- Link against `libsocket.a` (or the shared library if you build one).
- On **Windows**, also link against `ws2_32.lib` and `iphlpapi.lib`.
- On **Linux/POSIX**, no extra libraries are needed.

## Usage Example

```cpp
#include "socket.hpp"

int main() {
    sock::SocketInitializer init; // RAII for WSAStartup/WSACleanup on Windows
    sock::Socket client("example.com", 80);
    client.connect();
    client.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    std::string response = client.read<std::string>();
    std::cout << response << std::endl;
}
```

See the `test/` directory for more complete TCP, UDP, and Unix domain socket client/server examples.

## Running Tests

To run the comprehensive feature tests:

1. Build the project (see above).
2. In one terminal, run the server:
   ```sh
   ./build/socket_test
   ```
3. In another terminal, run the client:
   ```sh
   ./build/socket_test
   ```
   (Follow prompts for IP/port.)

## Documentation

- The code is fully documented with Doxygen comments.
- To generate HTML docs:

```sh
doxygen Doxyfile
```

## Contributing

Pull requests and issues are welcome! Please use modern C++17 and follow the style of the existing code.

## License

MIT License. See [LICENSE](LICENSE) for details.