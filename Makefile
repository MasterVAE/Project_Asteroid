
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Ibackend/include

SRC = $(wildcard backend/src/*.cpp)
TARGET = build/asteroid

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -rf build

re: clean all

run: all
	./$(TARGET)

.PHONY: all clean re run