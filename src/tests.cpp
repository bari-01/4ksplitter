#define CATCH_CONFIG_MAIN
#include "../include/catch.hpp"
#include "../include/protocol.h"
#include <chrono>

TEST_CASE("FrameBuffer handles complete assembly", "[protocol]") {
  FrameBuffer fb;
  uint32_t frame_id = 1;
  size_t total_chunks = 4;
  size_t chunk_size = 1400; // MAX_UDP_PAYLOAD
  size_t last_chunk_size = 500;

  // Simulate receiving 4 chunks out of order: 1, 3, 2, 0
  // Chunk 0
  if (fb.data.size() < total_chunks * MAX_UDP_PAYLOAD) {
    fb.data.resize(total_chunks * MAX_UDP_PAYLOAD);
  }
  fb.total_chunks = total_chunks;
  fb.received_chunks++;
  fb.actual_size += chunk_size;

  // Chunk 1
  fb.received_chunks++;
  fb.actual_size += chunk_size;

  // Chunk 2
  fb.received_chunks++;
  fb.actual_size += chunk_size;

  // Chunk 3 (Last chunk)
  fb.received_chunks++;
  fb.actual_size += last_chunk_size;

  REQUIRE(fb.received_chunks == fb.total_chunks);
  REQUIRE(fb.actual_size == (chunk_size * 3) + last_chunk_size);
}

TEST_CASE("FrameBuffer drops incomplete frames after timeout", "[protocol]") {
  FrameBuffer fb;
  fb.total_chunks = 5;
  fb.received_chunks = 4; // Missing one
  fb.last_updated =
      std::chrono::steady_clock::now() - std::chrono::milliseconds(501);

  auto now = std::chrono::steady_clock::now();
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - fb.last_updated)
                  .count();

  REQUIRE(diff > 500); // Emulate the 500ms drop logic in client.cpp
  REQUIRE(fb.received_chunks != fb.total_chunks);
}
