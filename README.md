# 4K120 Splitter

A client-server video streaming program to clients, transmits hardware accelerated av1 video.

## Overview

The server captures a video source (screen or file), splits each 4K frame into four 1080p quadrants, encodes each quadrant independently using available hardware encoders (e.g., NVENC, QSV, AMF, VAAPI, libx264), and sends them over UDP to 4 clients.

The client receives these packets, reassembles them, decodes them and renders the output using SDL3.

## Dependencies

### Linux
- `pkg-config`
- `g++` (C++17)
- **FFmpeg libraries**: `libavcodec`, `libavdevice`, `libavformat`, `libavutil`, `libswscale`
- **SDL3**: Required for the client application.

### Windows (Cross-compilation from Linux)
- `x86_64-w64-mingw32-g++`
- `x86_64-w64-mingw32-pkg-config`
- FFmpeg libraries compiled for MinGW (btbn's mingw libs)
- SDL3 compiled for MinGW
- Windows Sockets 2 (`ws2_32`)

## Building

```bash
# Build both client and server natively
make all

# Build only server or only client
make server
make client

# Run unit tests (Catch2)
make test

# Cross-compile for Windows
make server_win
make client_win

# Package Windows binaries along with missing DLLs
make package_win
```

## Usage

### Server

By default, the server captures your primary screen (`:0.0` on Linux, `desktop` on Windows) and streams the 4 quadrants to `127.0.0.1` on ports `5000` to `5003`.

```bash
./server [options] [ip1] [ip2] [ip3] [ip4]
```

**Options:**
- `-h`, `--help`: Show help message
- `-f <file>`: Use a video file as input (overrides `-t` and `-i`)
- `-t <format>`: Input format (default: `x11grab`/`gdigrab`). e.g., `v4l2`, `x11grab`
- `-c:v <encoder>`: Set target encoder (e.g., `hevc_nvenc`, `libx264`)
- `-i <source>`: Input source (default: `:0.0`). e.g., `/dev/video0`, `:99.0`, `desktop`

**Examples:**
```bash
# Capture Xvfb display and stream all 4 quadrants to localhost
./server -t x11grab -i :99.0 127.0.0.1

# Stream a local video file to a specific IP address (all 4 quadrants)
./server -f test.mkv 192.168.1.100

# Stream each quadrant to a different client machine
./server -i :0.0 192.168.1.100 192.168.1.101 192.168.1.102 192.168.1.103
```

### Client

The client listens on a specified UDP port (default: 5000) for a quadrant stream and renders it using SDL3.

```bash
./client [port]
```

**Example (Local Testing):**
If you ran the server streaming to `127.0.0.1`, it outputs the 4 quadrants to ports `5000`, `5001`, `5002`, and `5003`. You can open 4 terminals and run:
```bash
./client 5000
./client 5001
./client 5002
./client 5003
```
