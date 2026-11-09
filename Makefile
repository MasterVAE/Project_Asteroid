CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
CPPFLAGS = -Ibackend/include
LDLIBS = -lboost_json -lcrypto -lsqlite3
CRYPTO_CFLAGS := $(shell pkg-config --cflags openssl sqlite3 2>/dev/null)
CRYPTO_LDFLAGS := $(shell pkg-config --libs-only-L openssl sqlite3 2>/dev/null)
CPPFLAGS += $(CRYPTO_CFLAGS)
LDFLAGS += $(CRYPTO_LDFLAGS)

# Поддержка Homebrew на macOS; на Linux переменная останется пустой, если путь не задан вручную
BOOST_PREFIX ?= $(shell brew --prefix boost 2>/dev/null)
ifneq ($(strip $(BOOST_PREFIX)),)
CPPFLAGS += -I"$(BOOST_PREFIX)/include"
LDFLAGS += -L"$(BOOST_PREFIX)/lib"
endif

BUILD_DIR = build
TARGET = $(BUILD_DIR)/app
SOURCES = $(wildcard backend/src/*.cpp)
HEADERS = $(wildcard backend/include/*.h backend/include/*.hpp)

.PHONY: all clean test-auth-ids test-principals

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(SOURCES) $(LDLIBS) -o $@

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/test_auth_ids: tests/auth_ids.cpp backend/src/auth.cpp backend/src/principal.cpp backend/include/auth.hpp backend/include/principal.hpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) tests/auth_ids.cpp backend/src/auth.cpp backend/src/principal.cpp $(LDLIBS) -o $@

test-auth-ids: $(BUILD_DIR)/test_auth_ids
	./$(BUILD_DIR)/test_auth_ids

PRINCIPAL_TEST_SOURCES = tests/principals.cpp backend/src/auth.cpp backend/src/principal.cpp backend/src/world.cpp backend/src/modules.cpp backend/src/api.cpp backend/src/webrequests.cpp

$(BUILD_DIR)/test_principals: $(PRINCIPAL_TEST_SOURCES) $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(PRINCIPAL_TEST_SOURCES) $(LDLIBS) -o $@

test-principals: $(BUILD_DIR)/test_principals
	./$(BUILD_DIR)/test_principals

clean:
	rm -rf $(BUILD_DIR)
