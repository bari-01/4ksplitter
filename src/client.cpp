extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
}

#include <SDL3/SDL.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#include "../include/protocol.h"

#define PORT 5000

int init_udp_socket(int port) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed.\n";
    return -1;
  }
#endif

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

  int rcvbuf = 1048576 * 10; // 10MB
  setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&rcvbuf,
             sizeof(rcvbuf));

#ifdef _WIN32
  u_long mode = 1; // non-blocking socket
  ioctlsocket(sock, FIONBIO, &mode);
#endif

  if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return -1;
  }
  return sock;
}

AVCodecContext *init_decoder(AVBufferRef **hw_device_ctx) {
  if (av_hwdevice_ctx_create(hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr,
                             nullptr, 0) < 0) {
    std::cerr << "Warning: Failed to create CUDA hardware context. Falling "
                 "back to software decoding.\n";
    *hw_device_ctx = nullptr;
  }

  const AVCodec *codec = avcodec_find_decoder_by_name("av1");
  if (!codec) {
    std::cerr << "AV1 decoder not found.\n";
    return nullptr;
  }

  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  if (!ctx)
    return nullptr;

  ctx->hw_device_ctx = *hw_device_ctx ? av_buffer_ref(*hw_device_ctx) : nullptr;

  if (avcodec_open2(ctx, codec, nullptr) < 0) {
    std::cerr << "Failed to open AV1 decoder\n";
    avcodec_free_context(&ctx);
    return nullptr;
  }
  return ctx;
}

bool init_sdl_window(SDL_Window **window, SDL_Renderer **renderer) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init Error: " << SDL_GetError() << "\n";
    return false;
  }

  *window = SDL_CreateWindow("4K120 Multiplexer Stream", 1920, 1080,
                             SDL_WINDOW_RESIZABLE);
  if (!*window) {
    std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << "\n";
    return false;
  }

  *renderer = SDL_CreateRenderer(*window, nullptr);
  if (!*renderer) {
    std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << "\n";
    return false;
  }
  return true;
}

int main(int argc, char *argv[]) {
  int port = PORT;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  int sock = init_udp_socket(port);
  if (sock < 0)
    return 1;

  AVBufferRef *hw_device_ctx = nullptr;
  AVCodecContext *ctx = init_decoder(&hw_device_ctx);
  if (!ctx)
    return 1;

  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  if (!init_sdl_window(&window, &renderer))
    return 1;

  SDL_Texture *texture = nullptr;

  AVFrame *frame = av_frame_alloc();
  AVFrame *sw_frame = av_frame_alloc();
  AVPacket *pkt = av_packet_alloc();

  std::vector<uint8_t> extradata_buffer;
  std::map<uint32_t, FrameBuffer> frames;

  std::cout << "Client listening on port " << port << " for 4K stream...\n";

  bool quit = false;
  bool first_recv = true;

  // Performance tracking
  uint32_t expected_frame_id = 0;
  uint32_t dropped_frames = 0;
  uint32_t frames_rendered = 0;
  auto stat_start_time = std::chrono::steady_clock::now();

  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        quit = true;
      }
    }

    uint8_t buf[MAX_UDP_PAYLOAD + 12];
    sockaddr_in sender{};
    socklen_t sender_len = sizeof(sender);

    // Non-blocking socket
#ifdef _WIN32
    ssize_t n = recvfrom(sock, (char *)buf, sizeof(buf), 0, (sockaddr *)&sender,
                         &sender_len);
#else
    ssize_t n = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT,
                         (sockaddr *)&sender, &sender_len);
#endif

    if (n > 0) {
      PacketType type = (PacketType)buf[0];
      uint32_t frame_id = ntohl(*((uint32_t *)(buf + 1)));
      uint16_t packet_id = ntohs(*((uint16_t *)(buf + 5)));
      uint16_t total_packets = ntohs(*((uint16_t *)(buf + 7)));

      if (frame_id > expected_frame_id && type == FRAME) {
        dropped_frames += (frame_id - expected_frame_id);
        expected_frame_id = frame_id; // Sync to current
      }

      if (type == EXTRADATA) {
        if (extradata_buffer.size() < total_packets * MAX_UDP_PAYLOAD)
          extradata_buffer.resize(total_packets * MAX_UDP_PAYLOAD);
        memcpy(extradata_buffer.data() + packet_id * MAX_UDP_PAYLOAD, buf + 9,
               n - 9);

        if (packet_id + 1 == total_packets) {
          if (ctx->extradata)
            av_freep(&ctx->extradata);
          ctx->extradata_size = extradata_buffer.size();
          ctx->extradata = (uint8_t *)av_mallocz(ctx->extradata_size +
                                                 AV_INPUT_BUFFER_PADDING_SIZE);
          memcpy(ctx->extradata, extradata_buffer.data(), ctx->extradata_size);
          std::cout << "Extradata received (" << ctx->extradata_size
                    << " bytes)\n";
        }
        continue;
      }

      if (first_recv && type == FRAME) {
        std::cout << "First frame chunk received! Reassembling...\n";
        first_recv = false;
      }

      FrameBuffer &fb = frames[frame_id];
      if (fb.data.size() < total_packets * MAX_UDP_PAYLOAD) {
        fb.data.resize(total_packets * MAX_UDP_PAYLOAD);
      }
      memcpy(fb.data.data() + packet_id * MAX_UDP_PAYLOAD, buf + 9, n - 9);
      fb.total_chunks = total_packets;
      fb.received_chunks++;
      fb.actual_size += n - 9;
      fb.last_updated = std::chrono::steady_clock::now();

      if (fb.received_chunks == fb.total_chunks) {
        pkt->data = fb.data.data();
        pkt->size = fb.actual_size;

        if (avcodec_send_packet(ctx, pkt) == 0) {
          while (avcodec_receive_frame(ctx, frame) == 0) {
            AVFrame *target_frame = frame;

            if (frame->format == AV_PIX_FMT_CUDA) {
              if (av_hwframe_transfer_data(sw_frame, frame, 0) == 0) {
                target_frame = sw_frame;
              } else {
                std::cerr << "Hardware transfer failed\n";
                continue;
              }
            }

            // match texture size on first
            if (!texture) {
              texture = SDL_CreateTexture(
                  renderer, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING,
                  target_frame->width, target_frame->height);
            }

            if (texture && target_frame->format == AV_PIX_FMT_NV12) {
              // Directly upload NV12 to SDL3 (Y plane and UV plane)
              SDL_UpdateNVTexture(texture, nullptr, target_frame->data[0],
                                  target_frame->linesize[0],
                                  target_frame->data[1],
                                  target_frame->linesize[1]);

              SDL_RenderClear(renderer);
              SDL_RenderTexture(renderer, texture, nullptr, nullptr);
              SDL_RenderPresent(renderer);

              frames_rendered++;
              expected_frame_id = frame_id + 1; // Expect the next frame

              auto now = std::chrono::steady_clock::now();
              auto elapsed =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - stat_start_time)
                      .count();
              if (elapsed >= 1000) {
                double fps = frames_rendered * 1000.0 / elapsed;
                std::cout << "\r[FPS: " << static_cast<int>(fps)
                          << "] [Drops since start: " << dropped_frames
                          << "]   " << std::flush;
                frames_rendered = 0;
                stat_start_time = now;
              }
            }
          }
        }
        frames.erase(frame_id);
      }
    } else {
      // Avoid melting the CPU if packets are not coming
      SDL_Delay(1);
    }

    auto now = std::chrono::steady_clock::now();
    for (auto it = frames.begin(); it != frames.end();) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(
              now - it->second.last_updated)
              .count() > 500) {
        it = frames.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (texture)
    SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  av_frame_free(&sw_frame);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  if (hw_device_ctx)
    av_buffer_unref(&hw_device_ctx);
#ifdef _WIN32
  closesocket(sock);
  WSACleanup();
#else
  close(sock);
#endif
  return 0;
}
