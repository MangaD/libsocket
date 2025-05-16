# test/Makefile - Build test executables for libsocket

CXX ?= g++
CXXFLAGS ?= -std=c++17 -I../src
ifeq ($(OS),Windows_NT)
LDLIBS_STATIC = ../src/libsocket.a -lws2_32 -liphlpapi
LDLIBS_SHARED = ../src/libsocket.dll -lws2_32 -liphlpapi
else
LDLIBS_STATIC = ../src/libsocket.a
LDLIBS_SHARED = ../src/libsocket.so
endif

GTEST_FLAGS = -I$(GTEST_DIR)/include -L$(GTEST_DIR)/lib -lgtest -lgtest_main -lpthread
GTEST_DIR ?= /usr/local

all: client server socket_gtest

client: client.cpp ../src/libsocket.a
	$(CXX) $(CXXFLAGS) -o client client.cpp $(LDLIBS_STATIC)

server: server.cpp ../src/libsocket.a
	$(CXX) $(CXXFLAGS) -o server server.cpp $(LDLIBS_STATIC)

socket_gtest: socket_gtest.cpp ../src/libsocket.a
	$(CXX) $(CXXFLAGS) -o socket_gtest socket_gtest.cpp $(LDLIBS_STATIC) $(GTEST_FLAGS)

test: socket_gtest
	./socket_gtest

clean:
ifeq ($(OS),Windows_NT)
	cmd /C "del *.o *.a client server socket_gtest" 2>nul
else
	rm -f *.o *.a client server socket_gtest
endif