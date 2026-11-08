CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
CPPFLAGS = -Ibackend/include -lboost_json

# Homebrew by default; override with make BOOST_PREFIX=/path/to/boost.
BOOST_PREFIX ?= $(shell brew --prefix boost 2>/dev/null)
ifneq ($(strip $(BOOST_PREFIX)),)
CPPFLAGS += -I"$(BOOST_PREFIX)/include"
LDFLAGS += -L"$(BOOST_PREFIX)/lib" -lboost_json
endif

# Asio, Beast and Boost.System in this Boost version are header-only.

BUILD_DIR = build
TARGET = $(BUILD_DIR)/app
SOURCES = $(wildcard backend/src/*.cpp)
HEADERS = $(wildcard backend/include/*.h backend/include/*.hpp)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
