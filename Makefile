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
SOURCES = $(wildcard backend/src/*.cpp)
HEADERS = $(wildcard backend/include/*.h backend/include/*.hpp include/*.h include/*.hpp)

.PHONY: all run clean

all: $(TARGET)

run: all
	./$(TARGET)

$(TARGET): $(SOURCES) $(HEADERS) Makefile
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)
