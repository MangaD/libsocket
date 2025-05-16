# src/Makefile - Build the libsocket library (static and shared)

CXX ?= g++
AR = ar
ARFLAGS = rcvs
CXXFLAGS ?= -std=c++17 -O2 -Wall -I.
SRC = socket.cpp
LIB_STATIC = libsocket.a
LIB_SHARED = libsocket.so

ifeq ($(OS),Windows_NT)
LIB_SHARED = libsocket.dll
LDFLAGS = -lws2_32 -liphlpapi
else
LDFLAGS =
endif

all: $(LIB_STATIC)

$(LIB_STATIC): $(SRC)
	$(CXX) $(CXXFLAGS) -c socket.cpp -o socket.o
	$(AR) $(ARFLAGS) $@ socket.o

shared: $(SRC)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $(LIB_SHARED) $(SRC) $(LDFLAGS)

clean:
	rm -f *.o *.a *.so *.dll
