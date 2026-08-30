#include "openwow/render/world/terrain/terrain_mesh.h"
#include "openwow/render/world/terrain/terrain_material_compositor.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace openwow::render {

using namespace data::terrain;

static void InitializeBounds(float bounds_min[3], float bounds_max[3]) {
  std::fill_n(bounds_min, 3, std::numeric_limits<float>::max());
  std::fill_n(bounds_max, 3, std::numeric_limits<float>::lowest());
}

static void ExpandBounds(float bounds_min[3], float bounds_max[3], const float wx, const float wy,
                         const float wz) {
  bounds_min[0] = std::min(bounds_min[0], wx);
  bounds_min[1] = std::min(bounds_min[1], wy);
  bounds_min[2] = std::min(bounds_min[2], wz);
  bounds_max[0] = std::max(bounds_max[0], wx);
  bounds_max[1] = std::max(bounds_max[1], wy);
  bounds_max[2] = std::max(bounds_max[2], wz);
}

static constexpr int OuterIndex(int r, int c) {
  return r * 17 + c;
}

static constexpr int InnerIndex(int r, int c) {
  return r * 17 + 9 + c;
}

static bool IsHole(uint32_t holes, int row, int col) {
  const int hole_row = row / 2;
  const int hole_col = col / 2;
  const int bit = hole_row * 4 + hole_col;
  return (holes & (1u << bit)) != 0;
}

static void AppendChunkVertices(std::vector<TerrainVertex> &vertices, float bounds_min[3],
                                float bounds_max[3], const TerrainChunk &chunk,
                                const std::uint32_t chunk_x, const std::uint32_t chunk_y,
                                const std::uint32_t alpha_map_dimension,
                                const std::uint32_t alpha_atlas_dimension) {
  const std::size_t vertex_base = vertices.size();
  vertices.resize(vertex_base + kVerticesPerChunk);

  const float base_x = chunk.header.position_x;
  const float base_y = chunk.header.position_y;
  const float base_z = chunk.header.position_z;

  const bool has_colors = chunk.vertex_colors.size() == kVerticesPerChunk;

  auto EmitVertex = [&](int local_idx, float wx, float wy, float wz, float u, float v) {
    TerrainVertex &vert = vertices[vertex_base + static_cast<std::size_t>(local_idx)];
    vert.position[0] = wx;
    vert.position[1] = wy;
    vert.position[2] = wz;

    const auto normal =
        data::terrain::UnpackNormal(chunk.normals[static_cast<std::size_t>(local_idx)]);
    vert.normal[0] = normal.x;
    vert.normal[1] = normal.y;
    vert.normal[2] = normal.z;
    vert.texcoord[0] = u;
    vert.texcoord[1] = v;
    const float atlas_scale = static_cast<float>(alpha_map_dimension - 1u) /
                              static_cast<float>(alpha_atlas_dimension);
    const float atlas_offset_x =
        (static_cast<float>(chunk_x * alpha_map_dimension) + 0.5f) /
        static_cast<float>(alpha_atlas_dimension);
    const float atlas_offset_y =
        (static_cast<float>(chunk_y * alpha_map_dimension) + 0.5f) /
        static_cast<float>(alpha_atlas_dimension);
    vert.alpha_texcoord[0] = u * atlas_scale + atlas_offset_x;
    vert.alpha_texcoord[1] = v * atlas_scale + atlas_offset_y;

    if (has_colors) {
      const auto &vc = chunk.vertex_colors[static_cast<std::size_t>(local_idx)];

      vert.color = static_cast<uint32_t>(vc.b)
                   | (static_cast<uint32_t>(vc.g) << 8)
                   | (static_cast<uint32_t>(vc.r) << 16)
                   | (static_cast<uint32_t>(vc.a) << 24);
    } else {
      vert.color = 0xFF7F7F7Fu;
    }
    ExpandBounds(bounds_min, bounds_max, wx, wy, wz);
  };

  for (int r = 0; r < kOuterGrid; ++r) {
    for (int c = 0; c < kOuterGrid; ++c) {
      const int local_idx = OuterIndex(r, c);
      const float wx = base_x - static_cast<float>(r) * kUnitSize;
      const float wy = base_y - static_cast<float>(c) * kUnitSize;
      const float wz = base_z + chunk.heights[static_cast<std::size_t>(local_idx)];
      const float u = static_cast<float>(c) / 8.0f;
      const float v = static_cast<float>(r) / 8.0f;
      EmitVertex(local_idx, wx, wy, wz, u, v);
    }
  }

  for (int r = 0; r < kInnerGrid; ++r) {
    for (int c = 0; c < kInnerGrid; ++c) {
      const int local_idx = InnerIndex(r, c);
      const float wx = base_x - (static_cast<float>(r) + 0.5f) * kUnitSize;
      const float wy = base_y - (static_cast<float>(c) + 0.5f) * kUnitSize;
      const float wz = base_z + chunk.heights[static_cast<std::size_t>(local_idx)];
      const float u = (static_cast<float>(c) + 0.5f) / 8.0f;
      const float v = (static_cast<float>(r) + 0.5f) / 8.0f;
      EmitVertex(local_idx, wx, wy, wz, u, v);
    }
  }
}

static void AppendChunkIndices(std::vector<uint16_t> &indices, const uint16_t vertex_base,
                               const TerrainChunk &chunk) {

  indices.reserve(indices.size() + 768u);
  for (int r = 0; r < kInnerGrid; ++r) {
    for (int c = 0; c < kInnerGrid; ++c) {
      if (IsHole(chunk.holes, r, c)) {
        continue;
      }

      const auto tl = static_cast<uint16_t>(vertex_base + OuterIndex(r, c));
      const auto tr = static_cast<uint16_t>(vertex_base + OuterIndex(r, c + 1));
      const auto bl = static_cast<uint16_t>(vertex_base + OuterIndex(r + 1, c));
      const auto br = static_cast<uint16_t>(vertex_base + OuterIndex(r + 1, c + 1));
      const auto ct = static_cast<uint16_t>(vertex_base + InnerIndex(r, c));

      indices.push_back(bl);
      indices.push_back(ct);
      indices.push_back(tl);

      indices.push_back(ct);
      indices.push_back(tr);
      indices.push_back(tl);

      indices.push_back(ct);
      indices.push_back(bl);
      indices.push_back(br);

      indices.push_back(ct);
      indices.push_back(br);
      indices.push_back(tr);
    }
  }
}

template <typename MaterialT>
void PopulateChunkMaterial(const AdtFile &adt, const TerrainChunk &chunk, MaterialT &material) {
  material.layer_count = std::min(static_cast<int>(chunk.layers.size()), kMaxTerrainLayers);
  for (int layer = 0; layer < material.layer_count; ++layer) {
    const auto &source = chunk.layers[static_cast<std::size_t>(layer)];
    material.layer_flags[layer] = source.flags;
    if (source.texture_id < adt.textures.size()) {
      material.texture_paths[layer] = adt.textures[source.texture_id];
    }
    if (source.texture_id < adt.texture_flags.size()) {
      material.texture_flags[layer] = adt.texture_flags[source.texture_id];
    }
  }
}

static bool IsRgbaAlphaViewValid(const std::size_t rgba_size, const std::size_t pixel_stride,
                                 const std::size_t row_stride) {
  constexpr std::size_t kLastCoordinate = kAlphaMapSize - 1u;
  if (rgba_size < 4u || pixel_stride < 4u || row_stride == 0u ||
      pixel_stride > row_stride / kAlphaMapSize) {
    return false;
  }
  const std::size_t last_pixel_start = rgba_size - 4u;
  if (kLastCoordinate > last_pixel_start / row_stride) {
    return false;
  }
  const std::size_t last_row = kLastCoordinate * row_stride;
  return kLastCoordinate <= (last_pixel_start - last_row) / pixel_stride;
}

static void DecodeChunkAlphaMap(const TerrainChunk &chunk, const int layer_count,
                                const bool big_alpha, std::uint8_t *rgba,
                                const std::size_t rgba_size, const std::size_t pixel_stride,
                                const std::size_t row_stride) {
  if (rgba == nullptr || layer_count <= 0 ||
      !IsRgbaAlphaViewValid(rgba_size, pixel_stride, row_stride)) {
    return;
  }

  for (int layer = 1; layer < layer_count; ++layer) {
    const auto &ly = chunk.layers[static_cast<std::size_t>(layer)];
    if ((ly.flags & AlphaMapFlags::kHasAlpha) == 0u) {
      continue;
    }
    const uint32_t alpha_offset = ly.alpha_map_offset;
    if (alpha_offset >= chunk.alpha_data.size()) {
      continue;
    }

    const std::size_t channel = static_cast<std::size_t>(layer - 1);
    if (channel >= rgba_size) {
      continue;
    }
    static_cast<void>(DecompressAlphaMapInto(
        chunk.alpha_data.data() + alpha_offset, chunk.alpha_data.size() - alpha_offset, ly.flags,
        big_alpha,
        (chunk.header.flags & data::terrain::McnkFlags::kDoNotFixAlphaMap) == 0u,
        rgba + channel, rgba_size - channel, pixel_stride, row_stride));
  }

  constexpr std::size_t kShadowBytesPerRow = kAlphaMapSize / 8u;
  const bool has_shadow = chunk.shadow_map.size() >= kAlphaMapSize * kShadowBytesPerRow;
  for (std::size_t row = 0u; row < kAlphaMapSize; ++row) {
    for (std::size_t column = 0u; column < kAlphaMapSize; ++column) {
      auto *const pixel = rgba + row * row_stride + column * pixel_stride;
      pixel[3] = has_shadow ? ResolveRetailTerrainShadowVisibilityByte(
                                  chunk.shadow_map[row * kShadowBytesPerRow + column / 8u],
                                  static_cast<std::uint8_t>(column & 7u))
                            : 255u;
    }
  }
}

static void DownsampleChunkAlphaMap(
    const std::array<std::uint8_t, kAlphaMapSize * kAlphaMapSize * 4u> &source,
    const std::uint32_t output_dimension, std::uint8_t *const destination,
    const std::size_t destination_size, const std::size_t destination_row_stride) {
  if (destination == nullptr || output_dimension == 0u ||
      output_dimension > static_cast<std::uint32_t>(kAlphaMapSize) ||
      destination_row_stride < static_cast<std::size_t>(output_dimension) * 4u ||
      destination_size < destination_row_stride * output_dimension) {
    return;
  }

  for (std::uint32_t output_y = 0u; output_y < output_dimension; ++output_y) {
    const std::uint32_t source_y_begin =
        output_y * static_cast<std::uint32_t>(kAlphaMapSize) / output_dimension;
    const std::uint32_t source_y_end = std::max(
        source_y_begin + 1u,
        (output_y + 1u) * static_cast<std::uint32_t>(kAlphaMapSize) /
            output_dimension);
    for (std::uint32_t output_x = 0u; output_x < output_dimension; ++output_x) {
      const std::uint32_t source_x_begin =
          output_x * static_cast<std::uint32_t>(kAlphaMapSize) / output_dimension;
      const std::uint32_t source_x_end = std::max(
          source_x_begin + 1u,
          (output_x + 1u) * static_cast<std::uint32_t>(kAlphaMapSize) /
              output_dimension);
      const std::uint32_t sample_count =
          (source_x_end - source_x_begin) * (source_y_end - source_y_begin);
      auto *const output = destination + output_y * destination_row_stride +
                           static_cast<std::size_t>(output_x) * 4u;
      for (std::size_t channel = 0u; channel < 4u; ++channel) {
        std::uint32_t sum = 0u;
        for (std::uint32_t source_y = source_y_begin; source_y < source_y_end;
             ++source_y) {
          for (std::uint32_t source_x = source_x_begin;
               source_x < source_x_end; ++source_x) {
            sum += source[(static_cast<std::size_t>(source_y) * kAlphaMapSize +
                           source_x) *
                              4u +
                          channel];
          }
        }
        output[channel] = static_cast<std::uint8_t>(
            (sum + sample_count / 2u) / sample_count);
      }
    }
  }
}

PreparedTerrainTile PrepareAdtTerrainTile(const AdtFile &adt, const uint32_t tile_x,
                                          const uint32_t tile_y, const bool big_alpha,
                                          const std::uint32_t alpha_map_dimension) {

  (void)tile_x;
  (void)tile_y;

  PreparedTerrainTile prepared;
  const std::uint32_t resolved_alpha_dimension =
      std::clamp(alpha_map_dimension, 1u,
                 static_cast<std::uint32_t>(kAlphaMapSize));
  const std::uint32_t atlas_dimension =
      resolved_alpha_dimension *
      static_cast<std::uint32_t>(kTerrainAlphaAtlasChunksPerAxis);
  prepared.alpha_map_dimension =
      static_cast<std::uint16_t>(resolved_alpha_dimension);
  prepared.alpha_atlas_dimension =
      static_cast<std::uint16_t>(atlas_dimension);
  prepared.vertices.reserve(static_cast<std::size_t>(kTotalChunks * kVerticesPerChunk));
  const std::size_t holed_chunk_count = static_cast<std::size_t>(
      std::count_if(adt.chunks.begin(), adt.chunks.end(),
                    [](const TerrainChunk &chunk) { return chunk.holes != 0u; }));
  prepared.hole_indices.reserve(holed_chunk_count * 768u);
  prepared.alpha_atlas_rgba.assign(
      static_cast<std::size_t>(atlas_dimension) * atlas_dimension * 4u, 0u);
  for (std::size_t alpha = 3u; alpha < prepared.alpha_atlas_rgba.size(); alpha += 4u) {
    prepared.alpha_atlas_rgba[alpha] = 255u;
  }

  const std::size_t atlas_row_stride =
      static_cast<std::size_t>(atlas_dimension) * 4u;
  for (int y = 0; y < kChunksPerSide; ++y) {
    for (int x = 0; x < kChunksPerSide; ++x) {
      const std::size_t chunk_index = static_cast<std::size_t>(y * kChunksPerSide + x);
      const auto &source = adt.chunks[chunk_index];
      auto &chunk = prepared.chunks[chunk_index];
      chunk.chunk_x = static_cast<uint32_t>(x);
      chunk.chunk_y = static_cast<uint32_t>(y);
      PopulateChunkMaterial(adt, source, chunk);
      prepared.has_alpha_layers = prepared.has_alpha_layers || chunk.layer_count > 0;

      const std::size_t alpha_offset =
          (static_cast<std::size_t>(y) * resolved_alpha_dimension * atlas_dimension +
           static_cast<std::size_t>(x) * resolved_alpha_dimension) *
          4u;
      if (resolved_alpha_dimension ==
          static_cast<std::uint32_t>(kAlphaMapSize)) {
        DecodeChunkAlphaMap(source, chunk.layer_count, big_alpha,
                            prepared.alpha_atlas_rgba.data() + alpha_offset,
                            prepared.alpha_atlas_rgba.size() - alpha_offset, 4u,
                            atlas_row_stride);
      } else {
        std::array<std::uint8_t, kAlphaMapSize * kAlphaMapSize * 4u>
            full_resolution_alpha{};
        for (std::size_t alpha = 3u; alpha < full_resolution_alpha.size();
             alpha += 4u) {
          full_resolution_alpha[alpha] = 255u;
        }
        DecodeChunkAlphaMap(source, chunk.layer_count, big_alpha,
                            full_resolution_alpha.data(),
                            full_resolution_alpha.size(), 4u,
                            static_cast<std::size_t>(kAlphaMapSize) * 4u);
        DownsampleChunkAlphaMap(
            full_resolution_alpha, resolved_alpha_dimension,
            prepared.alpha_atlas_rgba.data() + alpha_offset,
            prepared.alpha_atlas_rgba.size() - alpha_offset,
            atlas_row_stride);
      }

      if (source.holes != 0u) {
        chunk.hole_index_start = static_cast<uint32_t>(prepared.hole_indices.size());
        AppendChunkIndices(prepared.hole_indices, 0u, source);
        chunk.hole_index_count =
            static_cast<uint32_t>(prepared.hole_indices.size() - chunk.hole_index_start);
        if (chunk.hole_index_count == 0u) {
          continue;
        }
      }
      InitializeBounds(chunk.bounds_min, chunk.bounds_max);
      chunk.vertex_start = static_cast<uint32_t>(prepared.vertices.size());
      AppendChunkVertices(prepared.vertices, chunk.bounds_min, chunk.bounds_max, source,
                          chunk.chunk_x, chunk.chunk_y,
                          resolved_alpha_dimension, atlas_dimension);
      chunk.vertex_count = kVerticesPerChunk;
      chunk.valid = true;
    }
  }
  return prepared;
}

}
