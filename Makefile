
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

all: $(TARGET)

$(TARGET): $(SRC) $(wildcard backend/include/*.hpp)
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDLIBS)

clean:
	rm -rf build

re: clean all

run: all
	./$(TARGET)

test: all
	python3 tests/websocket_smoke.py

.PHONY: all clean re run test
