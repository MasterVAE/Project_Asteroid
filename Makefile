CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra

# Автоматически узнаем пути к SFML и Boost из Homebrew
SFML_PREFIX := $(shell brew --prefix sfml)
BOOST_PREFIX := $(shell brew --prefix boost)

# Добавляем пути к заголовочным файлам SFML, Boost и локальным папкам
CPPFLAGS = -Iinclude -Ibackend/include -I$(SFML_PREFIX)/include -I$(BOOST_PREFIX)/include

# Указываем пути к скомпилированным библиотекам и модули SFML + Boost
LDFLAGS = -L$(SFML_PREFIX)/lib -L$(BOOST_PREFIX)/lib
LDLIBS = -lsfml-graphics -lsfml-window -lsfml-system -lboost_filesystem -lboost_json -lboost_process

TARGET = app
DEBUG_TARGET = app-debug
DEBUG_FLAGS = -g -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address,undefined -fno-sanitize-recover=all
SOURCES = $(wildcard backend/src/*.cpp)
HEADERS = $(wildcard backend/include/*.h backend/include/*.hpp include/*.h include/*.hpp)

.PHONY: all run debug run-debug clean

all: $(TARGET)

run: all
	./$(TARGET)

debug: $(DEBUG_TARGET)

run-debug: debug
	UBSAN_OPTIONS=print_stacktrace=1 ./$(DEBUG_TARGET)

$(TARGET): $(SOURCES) $(HEADERS) Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(DEBUG_TARGET): $(SOURCES) $(HEADERS) Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEBUG_FLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET) $(DEBUG_TARGET)
	rm -rf $(DEBUG_TARGET).dSYM
