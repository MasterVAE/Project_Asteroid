
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Ibackend/include
CPPFLAGS += $(shell pkg-config --cflags openssl sqlite3)
LDLIBS += $(shell pkg-config --libs openssl sqlite3) -pthread

# Homebrew keeps Boost outside the compiler's default search paths.
ifneq ($(wildcard /opt/homebrew/include/boost),)
CPPFLAGS += -I/opt/homebrew/include
endif

SRC = $(wildcard backend/src/*.cpp)
TARGET = build/asteroid
WORLD_TEST = build/world_test
SCRIPTS_TEST = build/scripts_test
PROTOCOL_TEST = build/protocol_test
NODE ?= node

all: $(TARGET)

$(TARGET): $(SRC) $(wildcard backend/include/*.hpp)
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDLIBS)

clean:
	rm -rf build

re: clean all

run: all
	./$(TARGET)

$(WORLD_TEST): tests/world.cpp backend/src/game_server.cpp backend/src/connection.cpp backend/src/script_runtime.cpp $(wildcard backend/include/*.hpp)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/world.cpp backend/src/game_server.cpp backend/src/connection.cpp backend/src/script_runtime.cpp -o $@ -pthread

$(SCRIPTS_TEST): tests/scripts.cpp tests/fake_docker.py backend/src/script_runtime.cpp backend/include/script_runtime.hpp backend/include/protocol.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/scripts.cpp backend/src/script_runtime.cpp -o $@ -pthread

sandbox-image:
	docker build -t asteroid-python:local backend/scripts

test-sandbox: $(SCRIPTS_TEST)
	./$(SCRIPTS_TEST) --docker

$(PROTOCOL_TEST): tests/protocol.cpp backend/include/protocol.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/protocol.cpp -o $(PROTOCOL_TEST)

test-protocol: $(PROTOCOL_TEST)
	./$(PROTOCOL_TEST)
	$(NODE) tests/protocol.mjs
	$(NODE) tests/app.mjs

test-core: $(PROTOCOL_TEST) $(WORLD_TEST) $(SCRIPTS_TEST)
	./$(PROTOCOL_TEST)
	./$(WORLD_TEST)
	./$(SCRIPTS_TEST)

test-integration: all
	python3 tests/websocket_smoke.py

test: test-core test-protocol test-integration

.PHONY: all clean re run test test-core test-protocol test-integration sandbox-image test-sandbox
