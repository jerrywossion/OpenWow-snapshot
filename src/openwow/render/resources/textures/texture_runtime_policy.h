#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::render {

struct TextureRuntimePolicy {
  std::uint32_t max_uncompressed_blp_dimension{0u};
  std::size_t max_async_requests{256u};
  std::size_t demand_async_request_reserve{64u};
  std::uint64_t cache_memory_budget_bytes{256ull * 1024ull * 1024ull};
};

[[nodiscard]] constexpr TextureRuntimePolicy PlatformTextureRuntimePolicy()
    noexcept {
#if defined(OPENWOW_PLATFORM_IOS)
  // Metal on iOS cannot sample the retail client's BC texture payloads.
  // Bounding the RGBA fallback and decoded handoff queue prevents that format
  // expansion from consuming the device's memory allowance during world load.
  return {
      .max_uncompressed_blp_dimension = 256u,
      .max_async_requests = 32u,
      .demand_async_request_reserve = 8u,
      .cache_memory_budget_bytes = 128ull * 1024ull * 1024ull,
  };
#else
  return {};
#endif
}

}
