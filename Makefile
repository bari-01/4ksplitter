CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
PKG_CONFIG = pkg-config

# FFmpeg libraries
FFMPEG_CFLAGS = $(shell $(PKG_CONFIG) --cflags libavcodec libavutil libavformat libswscale)
FFMPEG_LIBS   = $(shell $(PKG_CONFIG) --libs libavcodec libavutil libavformat libswscale)

# Optional SDL3 for client
OPENCV_LIBS = -lSDL3

SRC_SERVER = src/server.cpp
SRC_CLIENT  = src/client.cpp

OBJ_SERVER = $(SRC_SERVER:.cpp=.o)
OBJ_CLIENT  = $(SRC_CLIENT:.cpp=.o)

TARGET_SERVER = server
TARGET_CLIENT  = client

# -----------------------
.PHONY: all clean client_win package_win

all: server client

server: src/server.o
	$(CXX) src/server.o -o server -lavformat -lavcodec -lswscale -lavutil -lavdevice -lpthread

client: src/client.o
	$(CXX) src/client.o -o client -lavformat -lavcodec -lswscale -lavutil -lSDL3 -lpthread

src/server.o: src/server.cpp
	$(CXX) $(CXXFLAGS) -c src/server.cpp -o src/server.o

src/client.o: src/client.cpp
	$(CXX) $(CXXFLAGS) -c src/client.cpp -o src/client.o

# --- Windows Cross-Compilation ---
CXX_WIN = x86_64-w64-mingw32-g++
PKG_CONFIG_WIN = x86_64-w64-mingw32-pkg-config

server_win: src/server.cpp
	$(CXX_WIN) -std=c++17 -Wall -Wextra -O2 src/server.cpp -o server.exe \
		-I$(HOME)/ffmpeg \
		-L$(HOME)/ffmpeg/libavcodec -L$(HOME)/ffmpeg/libavutil -L$(HOME)/ffmpeg/libswscale -L$(HOME)/ffmpeg/libavformat -L$(HOME)/ffmpeg/libavdevice \
		-lavdevice -lavformat -lavcodec -lswscale -lavutil -lws2_32 -static-libstdc++ -static-libgcc

client_win: src/client.cpp
	$(CXX_WIN) -std=c++17 -Wall -Wextra -O2 src/client.cpp -o client.exe \
		-I$(HOME)/ffmpeg \
		-L$(HOME)/ffmpeg/libavcodec -L$(HOME)/ffmpeg/libavutil \
		-lavcodec -lavutil $$($(PKG_CONFIG_WIN) --cflags --libs sdl3) -lws2_32 -static-libstdc++ -static-libgcc -mwindows

package_win: client_win server_win
	mkdir -p release_win
	cp client.exe server.exe release_win/
	cp $(HOME)/ffmpeg/libavcodec/*.dll release_win/
	cp $(HOME)/ffmpeg/libavformat/*.dll release_win/
	cp $(HOME)/ffmpeg/libavutil/*.dll release_win/
	cp $(HOME)/ffmpeg/libavdevice/*.dll release_win/
	cp $(HOME)/ffmpeg/libswscale/*.dll release_win/
	cp $(HOME)/ffmpeg/libswresample/*.dll release_win/ 2>/dev/null || true
	cp /usr/x86_64-w64-mingw32/bin/SDL3.dll release_win/
	@echo "Package created in release_win/ with all necessary DLLs!"

# --- Testing ---
test: src/tests.cpp
	$(CXX) -std=c++17 -Wall -Wextra src/tests.cpp -o unit_tests
	./unit_tests

clean:
	rm -f src/*.o server client client.exe server.exe unit_tests
	rm -rf release_win
