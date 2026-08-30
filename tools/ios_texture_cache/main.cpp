#include "openwow/data/blp/blp_texture_loader.h"
#include "openwow/data/derived_texture_cache.h"
#include "openwow/data/login_resource_validator.h"

#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kMaxTextureDimension = 128u;
constexpr std::string_view kGeneratorVersion = "openwow-ios-astc-v1";
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

struct Options {
  fs::path game_root;
  fs::path output;
  fs::path enhanced_assets_root;
  std::string locale{"zhCN"};
};

struct Counts {
  std::size_t encoded{0u};
  std::size_t current{0u};
  std::size_t ignored{0u};
  std::size_t failed{0u};
};

void HashByte(std::uint64_t& hash, const std::uint8_t value) noexcept {
  hash = (hash ^ value) * kFnvPrime;
}

void HashText(std::uint64_t& hash, const std::string_view text) noexcept {
  for (unsigned char value : text) {
    if (value >= 'A' && value <= 'Z') {
      value = static_cast<unsigned char>(value + ('a' - 'A'));
    }
    if (value == '\\') {
      value = '/';
    }
    HashByte(hash, value);
  }
  HashByte(hash, 0u);
}

void HashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
  for (unsigned index = 0u; index < 8u; ++index) {
    HashByte(hash, static_cast<std::uint8_t>(value));
    value >>= 8u;
  }
}

std::optional<Options> ParseOptions(const int argc, char** const argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (index + 1 >= argc) {
      std::cerr << "missing value for " << argument << '\n';
      return std::nullopt;
    }
    const std::string value(argv[++index]);
    if (argument == "--game-root") {
      options.game_root = value;
    } else if (argument == "--output") {
      options.output = value;
    } else if (argument == "--locale") {
      options.locale = value;
    } else if (argument == "--enhanced-assets-root") {
      options.enhanced_assets_root = value;
    } else {
      std::cerr << "unknown argument: " << argument << '\n';
      return std::nullopt;
    }
  }
  if (options.game_root.empty() || options.output.empty()) {
    std::cerr << "usage: openwow-ios-texture-cache --game-root PATH "
                 "--output PATH [--locale zhCN] "
                 "[--enhanced-assets-root PATH]\n";
    return std::nullopt;
  }
  return options;
}

bool IsWithin(const fs::path& candidate, const fs::path& root) {
  const fs::path relative = candidate.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}

void AppendTreeSignatureEntries(
    const fs::path& root, const fs::path& excluded_root,
    std::vector<std::string>& entries) {
  std::error_code ec;
  if (!fs::is_directory(root, ec) || ec) {
    return;
  }
  fs::recursive_directory_iterator iterator(
      root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  while (!ec && iterator != end) {
    const fs::directory_entry entry = *iterator;
    if (!excluded_root.empty() && IsWithin(entry.path(), excluded_root)) {
      if (entry.is_directory(ec) && !ec) {
        iterator.disable_recursion_pending();
      }
      iterator.increment(ec);
      continue;
    }
    if (entry.is_regular_file(ec) && !ec) {
      const fs::path relative = entry.path().lexically_relative(root);
      const auto size = entry.file_size(ec);
      if (!ec) {
        const auto modified = entry.last_write_time(ec);
        if (!ec) {
          entries.push_back(
              relative.generic_string() + "\n" + std::to_string(size) +
              "\n" +
              std::to_string(static_cast<std::int64_t>(
                  modified.time_since_epoch().count())));
        }
      }
    }
    ec.clear();
    iterator.increment(ec);
  }
}

std::string BuildInputSignature(const Options& options) {
  std::vector<std::string> entries;
  AppendTreeSignatureEntries(options.game_root / "Data",
                             options.output.parent_path().parent_path(),
                             entries);
  AppendTreeSignatureEntries(options.enhanced_assets_root, {}, entries);
  std::sort(entries.begin(), entries.end());

  std::uint64_t hash = kFnvOffset;
  HashText(hash, kGeneratorVersion);
  HashText(hash, options.locale);
  HashU64(hash, kMaxTextureDimension);
  for (const auto& entry : entries) {
    HashText(hash, entry);
  }
  std::ostringstream result;
  result << std::hex << std::setfill('0') << std::setw(16) << hash;
  return result.str();
}

std::size_t CountCacheFiles(const fs::path& output) {
  std::size_t count = 0u;
  std::error_code ec;
  for (fs::directory_iterator iterator(output, ec), end;
       !ec && iterator != end; iterator.increment(ec)) {
    if (iterator->is_regular_file(ec) && !ec &&
        iterator->path().extension() == ".owtx") {
      ++count;
    }
  }
  return count;
}

bool ManifestIsCurrent(const fs::path& manifest,
                       const std::string& signature,
                       const fs::path& output) {
  std::ifstream stream(manifest);
  if (!stream) {
    return false;
  }
  std::string stored_signature;
  std::size_t stored_count = 0u;
  stream >> stored_signature >> stored_count;
  return stream.good() && stored_signature == signature &&
         stored_count == CountCacheFiles(output);
}

bool WriteBytesAtomically(const fs::path& destination,
                          const std::vector<std::uint8_t>& bytes) {
  const fs::path temporary = destination.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
      return false;
    }
  }
  std::error_code ec;
  fs::rename(temporary, destination, ec);
  if (!ec) {
    return true;
  }
  fs::remove(destination, ec);
  ec.clear();
  fs::rename(temporary, destination, ec);
  return !ec;
}

std::vector<std::uint8_t> ReadBytes(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return {};
  }
  const auto extent = stream.tellg();
  if (extent <= 0 ||
      static_cast<std::uint64_t>(extent) >
          std::numeric_limits<std::size_t>::max()) {
    return {};
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(extent));
  stream.seekg(0, std::ios::beg);
  stream.read(reinterpret_cast<char*>(bytes.data()), extent);
  return stream ? std::move(bytes) : std::vector<std::uint8_t>{};
}

std::uint32_t AstcMipSize(const std::uint32_t width,
                          const std::uint32_t height) noexcept {
  return ((width + 3u) / 4u) * ((height + 3u) / 4u) * 16u;
}

std::optional<openwow::data::DerivedTextureCacheImage> EncodeTexture(
    const openwow::data::BLPTextureData& blp, bx::AllocatorI* const allocator,
    std::string& error) {
  std::size_t first_mip = 0u;
  while (first_mip + 1u < blp.mips.size() &&
         (blp.mips[first_mip].width > kMaxTextureDimension ||
          blp.mips[first_mip].height > kMaxTextureDimension)) {
    ++first_mip;
  }
  if (first_mip >= blp.mips.size()) {
    error = "no usable source mip";
    return std::nullopt;
  }

  openwow::data::DerivedTextureCacheImage image{
      .format = openwow::data::DerivedTextureFormat::kAstc4x4,
      .width = static_cast<std::uint16_t>(blp.mips[first_mip].width),
      .height = static_cast<std::uint16_t>(blp.mips[first_mip].height),
      .is_opaque =
          blp.header.alphaDepth == openwow::data::BLPTexAlphaDepth::NoAlpha,
  };
  const std::size_t remaining_mips =
      std::min<std::size_t>(16u, blp.mips.size() - first_mip);
  for (std::size_t index = 0u; index < remaining_mips; ++index) {
    const auto& source_mip = blp.mips[first_mip + index];
    auto rgba = openwow::data::BLPTextureLoader::DecompressMip(
        blp, static_cast<std::uint8_t>(first_mip + index));
    const std::uint64_t rgba_extent =
        static_cast<std::uint64_t>(source_mip.width) *
        source_mip.height * 4u;
    if (rgba_extent == 0u || rgba.size() != rgba_extent) {
      error = "BLP mip decode failed at level " + std::to_string(index);
      return std::nullopt;
    }

    const std::uint32_t encoded_size =
        AstcMipSize(source_mip.width, source_mip.height);
    const std::size_t offset = image.payload.size();
    image.payload.resize(offset + encoded_size);
    bx::Error encode_error;
    bimg::imageEncodeFromRgba8(
        allocator, image.payload.data() + offset, rgba.data(),
        source_mip.width, source_mip.height, 1u,
        bimg::TextureFormat::ASTC4x4, bimg::Quality::Fastest,
        &encode_error);
    if (!encode_error.isOk()) {
      error = std::string("ASTC encoder failed: ") +
              encode_error.getMessage().getCPtr();
      return std::nullopt;
    }
    ++image.mip_count;
  }
  image.complete_mip_chain = image.mip_count > 1u &&
                             blp.mips[first_mip + image.mip_count - 1u].width ==
                                 1u &&
                             blp.mips[first_mip + image.mip_count - 1u].height ==
                                 1u;
  if (!image.complete_mip_chain && image.mip_count > 1u) {
    image.payload.resize(AstcMipSize(image.width, image.height));
    image.mip_count = 1u;
  }
  return image;
}

bool HasBlpExtension(const fs::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return extension == ".blp";
}

int Run(const Options& options) {
  std::error_code ec;
  if (!fs::is_directory(options.game_root / "Data", ec) || ec) {
    std::cerr << "Data directory is missing under " << options.game_root
              << '\n';
    return 2;
  }
  fs::create_directories(options.output, ec);
  if (ec) {
    std::cerr << "cannot create cache directory " << options.output << ": "
              << ec.message() << '\n';
    return 2;
  }

  const fs::path manifest = options.output.parent_path() /
                            "texture-cache-v1.manifest";
  const std::string signature = BuildInputSignature(options);
  if (ManifestIsCurrent(manifest, signature, options.output)) {
    std::cout << "iOS ASTC cache is current (" << CountCacheFiles(options.output)
              << " textures)\n";
    return 0;
  }

  auto vfs = openwow::data::BuildLoginVfs(
      options.game_root.string(), options.enhanced_assets_root.string(),
      options.locale);
  auto files = vfs.EnumerateFiles("/", true);
  std::sort(files.begin(), files.end());

  Counts counts;
  bx::DefaultAllocator allocator;
  for (const auto& virtual_file : files) {
    if (!HasBlpExtension(virtual_file)) {
      continue;
    }
    std::string source_path = virtual_file.generic_string();
    while (!source_path.empty() &&
           (source_path.front() == '/' || source_path.front() == '\\')) {
      source_path.erase(source_path.begin());
    }
    if (source_path.starts_with("OpenWoWDerived/")) {
      continue;
    }

    auto source = vfs.ReadFileBytes(virtual_file.generic_string());
    if (!source.has_value() || source->empty()) {
      ++counts.failed;
      std::cerr << "source read failed: " << source_path << '\n';
      continue;
    }
    const std::string virtual_cache =
        openwow::data::MakeDerivedTextureCacheVirtualPath(source_path);
    const fs::path destination =
        options.output / fs::path(virtual_cache).filename();
    if (const auto existing = ReadBytes(destination); !existing.empty()) {
      openwow::data::DerivedTextureCacheImage image;
      if (openwow::data::ParseDerivedTextureCache(
              existing, source_path, *source, image) &&
          image.width <= kMaxTextureDimension &&
          image.height <= kMaxTextureDimension) {
        ++counts.current;
        continue;
      }
    }

    const auto blp = openwow::data::BLPTextureLoader::Load(*source);
    if (!blp.isValid || blp.mips.empty()) {
      ++counts.ignored;
      continue;
    }
    std::string encode_error;
    auto image = EncodeTexture(blp, &allocator, encode_error);
    if (!image.has_value()) {
      ++counts.failed;
      std::cerr << "encode failed: " << source_path << ": " << encode_error
                << '\n';
      continue;
    }
    auto cache = openwow::data::SerializeDerivedTextureCache(
        source_path, *source, *image);
    if (cache.empty() || !WriteBytesAtomically(destination, cache)) {
      ++counts.failed;
      std::cerr << "cache write failed: " << destination << '\n';
      continue;
    }
    ++counts.encoded;
    if ((counts.encoded + counts.current) % 250u == 0u) {
      std::cout << "iOS ASTC cache progress: encoded=" << counts.encoded
                << " current=" << counts.current << '\n';
    }
  }

  std::cout << "iOS ASTC cache complete: encoded=" << counts.encoded
            << " current=" << counts.current
            << " ignored=" << counts.ignored
            << " failed=" << counts.failed << '\n';
  if (counts.failed != 0u) {
    std::cerr << "cache generation had failures; the completion manifest was "
                 "not updated\n";
    return 2;
  }

  std::ofstream manifest_stream(manifest, std::ios::trunc);
  manifest_stream << signature << ' ' << CountCacheFiles(options.output)
                  << '\n';
  if (!manifest_stream) {
    std::cerr << "failed to write cache manifest " << manifest << '\n';
    return 2;
  }
  return 0;
}

}

int main(const int argc, char** const argv) {
  const auto options = ParseOptions(argc, argv);
  return options.has_value() ? Run(*options) : 2;
}
