#include "openwow/data/startup_archive_mount.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/platform/filesystem/filesystem.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace openwow::data {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;
using openwow::text::EqualsIgnoreCaseAscii;
using openwow::text::ToLowerAscii;

namespace {

std::filesystem::path StartupStatePathToNative(std::string path) {
#if !defined(_WIN32)
  std::replace(path.begin(), path.end(), '\\',
               std::filesystem::path::preferred_separator);
#endif
  return std::filesystem::path(path);
}

std::filesystem::path ResolveStartupClientDirectory(
    const std::filesystem::path& startup_root) {
  if (!startup_root.empty() && startup_root.filename().empty()) {
    return startup_root.parent_path();
  }
  return startup_root;
}

std::filesystem::path ResolveSiblingDataDir(
    const std::filesystem::path& game_root) {
  const auto data_dir = ResolveDataDir(game_root);
  if (data_dir.empty()) return {};
  const auto data_parent = data_dir.parent_path();
  if (data_parent.empty()) return {};
  const auto upper_root = data_parent.parent_path();
  if (upper_root.empty()) return {};
  return upper_root / "Data";
}

constexpr int kArchiveProbeRootCount = 3;

std::optional<std::filesystem::path> FindArchiveAcrossRoots(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root,
    const std::string& archive_name) {
  const auto native_roots =
      ResolveArchiveProbeNativeRoots(game_root, retail_install_root);
  for (int root_index = 0; root_index < kArchiveProbeRootCount; ++root_index) {
    if (auto resolved = ResolveArchiveProbeFileNative(
            root_index, native_roots, archive_name, "");
        resolved.has_value()) {
      return resolved;
    }
  }
  return std::nullopt;
}

struct StartupPatchTableEntry {

  int pass;

  bool secondary;

  bool retail_install_prefixed;
  const char* directory_pattern;
  const char* file_pattern;
};

constexpr std::array<StartupPatchTableEntry, 12> kStartupPatchTable = {{
    {0, false, false, "Data\\", "patch-?.MPQ"},
    {0, false, false, "Data\\%s\\", "patch-%s-?.MPQ"},
    {1, false, false, "Data\\", "patch.MPQ"},
    {1, false, false, "Data\\%s\\", "patch-%s.MPQ"},
    {1, true, false, "Data\\", "patch-4.MPQ"},
    {1, true, false, "Data\\%s\\", "patch-%s-4.MPQ"},
    {1, true, false, "Data\\", "patch-3.MPQ"},
    {1, true, false, "Data\\%s\\", "patch-%s-3.MPQ"},
    {1, true, false, "..\\Data\\", "patch-2.MPQ"},
    {1, true, false, "..\\Data\\%s\\", "patch-%s-2.MPQ"},
    {1, true, false, "..\\Data\\", "patch.MPQ"},
    {1, true, false, "..\\Data\\%s\\", "patch-%s.MPQ"},
}};

constexpr std::size_t kGlobalWildcardPatchRow = 0;
constexpr std::size_t kLocaleWildcardPatchRow = 1;
constexpr std::size_t kGlobalLiteralPatchRow = 2;
constexpr std::size_t kLocaleLiteralPatchRow = 3;

std::string FormatStockPatchPattern(const std::string_view pattern,
                                    const std::string& locale_token) {
  const auto placeholder = pattern.find("%s");
  if (placeholder == std::string_view::npos) {
    return std::string(pattern);
  }
  std::string result(pattern.substr(0, placeholder));
  result += locale_token;
  result += pattern.substr(placeholder + 2);
  return result;
}

constexpr char kStockSingleCharacterWildcard = '?';
constexpr char kWidenedPatchWildcard = '*';

std::string WidenStockPatchWildcard(std::string pattern) {
  std::replace(pattern.begin(), pattern.end(), kStockSingleCharacterWildcard,
               kWidenedPatchWildcard);
  return pattern;
}

constexpr std::size_t kNumericPatchSuffixKeyWidth = 20;

constexpr std::size_t kMpqExtensionSize = 4;

struct StartupPatchCandidate {
  std::filesystem::path native_path;

  std::string storm_relative_path;

  std::string sort_key;
};

bool IsAllAsciiDigits(const std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

std::string BuildPatchSortKey(const std::string& storm_relative_path,
                              const std::string_view suffix,
                              const std::size_t suffix_offset) {

  if (!IsAllAsciiDigits(suffix) ||
      suffix.size() >= kNumericPatchSuffixKeyWidth) {
    return storm_relative_path;
  }

  std::string key = storm_relative_path.substr(0, suffix_offset);
  key.append(kNumericPatchSuffixKeyWidth - suffix.size(), '0');
  key.append(storm_relative_path, suffix_offset, std::string::npos);
  return key;
}

bool HasMpqExtension(const std::filesystem::path& path) {
  return EqualsIgnoreCaseAscii(path.extension().string(), ".MPQ");
}

void CollectStartupPatchMatches(const std::filesystem::path& native_dir,
                                const std::string& storm_dir_prefix,
                                const std::string& file_pattern,
                                const std::size_t suffix_prefix_size,
                                std::vector<StartupPatchCandidate>* out) {
  if (native_dir.empty() || out == nullptr) {
    return;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(native_dir, ec) || ec) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(native_dir, ec)) {
    if (ec) {
      break;
    }
    if (!std::filesystem::is_regular_file(entry, ec) || ec) {
      continue;
    }

    const auto name = entry.path().filename().string();
    if (!HasMpqExtension(entry.path())) {
      continue;
    }
    if (!openwow::core::SStrWildcardMatch(name.c_str(), file_pattern.c_str())) {
      continue;
    }

    const std::string storm_relative_path = storm_dir_prefix + name;
    std::string_view suffix;
    std::size_t suffix_offset = storm_relative_path.size();
    if (suffix_prefix_size != std::string::npos) {
      if (name.size() <= suffix_prefix_size + kMpqExtensionSize) {

        continue;
      }
      suffix_offset = storm_dir_prefix.size() + suffix_prefix_size;
      suffix = std::string_view(storm_relative_path)
                   .substr(suffix_offset,
                           name.size() - suffix_prefix_size - kMpqExtensionSize);
    }

    out->push_back(StartupPatchCandidate{
        .native_path = entry.path(),
        .storm_relative_path = storm_relative_path,
        .sort_key =
            BuildPatchSortKey(storm_relative_path, suffix, suffix_offset),
    });
  }
}

std::optional<std::filesystem::path> ResolveLocaleDataSubdirectory(
    const std::filesystem::path& data_dir,
    const std::string& locale_token) {
  const auto exact = data_dir / locale_token;
  std::error_code ec;
  if (std::filesystem::is_directory(exact, ec) && !ec) {
    return exact;
  }

  auto resolved =
      openwow::platform::filesystem::ResolveExistingRelativePathCaseInsensitive(
          data_dir, locale_token);
  if (!resolved.has_value() || !std::filesystem::is_directory(*resolved, ec) || ec) {
    return std::nullopt;
  }
  return resolved;
}

constexpr std::size_t kLocaleCodeLength = 4;

bool TryMountArchiveByName(openwow::vfs::VirtualFileSystem* vfs,
                           const std::filesystem::path& game_root,
                           const std::filesystem::path& retail_install_root,
                           const std::string& archive_name,
                           int priority) {
  const auto archive_path =
      FindArchiveAcrossRoots(game_root, retail_install_root, archive_name);
  if (!archive_path.has_value()) {
    return false;
  }

  Log(LogLevel::kInfo,
      "[MPQ] Registered archive (Storm priority " + std::to_string(priority) +
          "): " + archive_path->string());

  vfs->Mount({
      .id = "mpq:" + ToLowerAscii(archive_name),
      .kind = openwow::vfs::MountKind::kMpqArchive,
      .source_root = *archive_path,
      .priority = kStormArchivePriorityBias + priority,
      .enabled = true,
  });
  return true;
}

void MountStartupPatchArchive(openwow::vfs::VirtualFileSystem* vfs,
                              const std::filesystem::path& patch_path,
                              int priority) {
  Log(LogLevel::kInfo,
      "[MPQ] Registered patch archive (Storm priority " +
          std::to_string(priority) + "): " + patch_path.string());
  vfs->Mount({
      .id = "mpq:patch:" + ToLowerAscii(patch_path.filename().string()) + ":" +
            std::to_string(priority),
      .kind = openwow::vfs::MountKind::kMpqArchive,
      .source_root = patch_path,
      .priority = kStormArchivePriorityBias + priority,
      .enabled = true,
  });
}

}

bool IsDataDirectory(const std::filesystem::path& path) {
  return EqualsIgnoreCaseAscii(path.filename().string(), "Data");
}

std::filesystem::path ResolveStartupClientRoot(
    const std::filesystem::path& game_root) {
  const auto& state = GetStartupFileSystemState();
  if (!state.executable_base_path.empty()) {
    return StartupStatePathToNative(state.executable_base_path);
  }
  return game_root;
}

std::filesystem::path ResolveRetailInstallRoot(
    const std::filesystem::path& retail_install_root) {
  if (!retail_install_root.empty()) {
    return retail_install_root;
  }

  const auto& state = GetStartupFileSystemState();
  if (state.retail_install_path_cache.empty()) {
    return {};
  }
  return StartupStatePathToNative(state.retail_install_path_cache);
}

std::filesystem::path ResolveDataDir(const std::filesystem::path& game_root) {
  if (IsDataDirectory(game_root)) return game_root;
  return game_root / "Data";
}

std::filesystem::path ResolveExistingDataDir(
    const std::filesystem::path& game_root) {
  const auto exact = ResolveDataDir(game_root);
  std::error_code ec;
  if (std::filesystem::is_directory(exact, ec) && !ec) {
    return exact;
  }
  if (IsDataDirectory(game_root)) {
    return {};
  }
  auto resolved =
      openwow::platform::filesystem::ResolveExistingRelativePathCaseInsensitive(
          game_root, "Data");
  if (resolved.has_value() && std::filesystem::is_directory(*resolved, ec) && !ec) {
    return *resolved;
  }
  return {};
}

ArchiveProbeNativeRoots ResolveArchiveProbeNativeRoots(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root) {
  const auto startup_root = ResolveStartupClientRoot(game_root);
  ArchiveProbeNativeRoots roots;
  roots.data_root = ResolveDataDir(startup_root);
  if (!GetStartupFileSystemState().executable_base_path.empty()) {
    const auto upper_root =
        ResolveStartupClientDirectory(startup_root).parent_path();
    if (!upper_root.empty()) {
      roots.parent_data_root = ResolveDataDir(upper_root);
    }
  } else {
    roots.parent_data_root = ResolveSiblingDataDir(startup_root);
  }
  if (!retail_install_root.empty()) {
    roots.retail_data_root = ResolveDataDir(retail_install_root);
  }
  return roots;
}

bool HasCommonArchiveLayout(const std::filesystem::path& game_root,
                            const std::filesystem::path& retail_install_root) {
  return ProbeCommonArchiveLayout(game_root, retail_install_root);
}

std::vector<std::filesystem::path> BuildStartupPatchChain(
    const std::filesystem::path& data_dir,
    const std::string& locale_token) {
  const std::string stock_locale_prefix = FormatStockPatchPattern(
      kStartupPatchTable[kLocaleWildcardPatchRow].directory_pattern,
      locale_token);
  const std::string stock_data_prefix =
      kStartupPatchTable[kGlobalWildcardPatchRow].directory_pattern;

  std::optional<std::filesystem::path> locale_dir;
  if (locale_token.size() >= kLocaleCodeLength) {
    locale_dir = ResolveLocaleDataSubdirectory(data_dir, locale_token);
  }

  const std::string_view global_pattern =
      kStartupPatchTable[kGlobalWildcardPatchRow].file_pattern;
  std::vector<StartupPatchCandidate> wildcard_matches;
  CollectStartupPatchMatches(
      data_dir, stock_data_prefix,
      WidenStockPatchWildcard(std::string(global_pattern)),
      global_pattern.find(kStockSingleCharacterWildcard), &wildcard_matches);
  if (locale_dir.has_value()) {
    const std::string locale_pattern = WidenStockPatchWildcard(
        FormatStockPatchPattern(
            kStartupPatchTable[kLocaleWildcardPatchRow].file_pattern,
            locale_token));
    CollectStartupPatchMatches(*locale_dir, stock_locale_prefix, locale_pattern,
                               locale_pattern.find(kWidenedPatchWildcard),
                               &wildcard_matches);
  }

  std::sort(wildcard_matches.begin(), wildcard_matches.end(),
            [](const StartupPatchCandidate& lhs,
               const StartupPatchCandidate& rhs) {
              const int order = openwow::core::SStrCmpNoCase(
                  lhs.sort_key.c_str(), rhs.sort_key.c_str(), 0x7FFFFFFFu);
              if (order != 0) {
                return order < 0;
              }

              return lhs.storm_relative_path < rhs.storm_relative_path;
            });

  std::vector<StartupPatchCandidate> literal_matches;
  if (locale_dir.has_value()) {
    CollectStartupPatchMatches(
        *locale_dir, stock_locale_prefix,
        FormatStockPatchPattern(
            kStartupPatchTable[kLocaleLiteralPatchRow].file_pattern,
            locale_token),
        std::string::npos, &literal_matches);
  }
  CollectStartupPatchMatches(
      data_dir, stock_data_prefix,
      kStartupPatchTable[kGlobalLiteralPatchRow].file_pattern,
      std::string::npos, &literal_matches);

  std::vector<std::filesystem::path> patch_chain;
  patch_chain.reserve(literal_matches.size() + wildcard_matches.size());
  for (auto& candidate : literal_matches) {
    patch_chain.push_back(std::move(candidate.native_path));
  }
  for (auto& candidate : wildcard_matches) {
    patch_chain.push_back(std::move(candidate.native_path));
  }
  return patch_chain;
}

void MountStartupArchives(openwow::vfs::VirtualFileSystem* vfs,
                          const StartupArchiveMountOptions& options) {
  if (vfs == nullptr) {
    return;
  }

  const auto& game_root = options.game_root;
  const auto& retail_root = options.retail_install_root;
  const auto patch_chain = BuildStartupPatchChain(
      ResolveExistingDataDir(game_root), options.locale_token);

  const auto expand_archive_name = [&](const char* archive_name) {
    return ReplaceArchiveLocalePlaceholdersExact(archive_name,
                                                 options.locale_token.c_str());
  };
  const auto archive_is_enabled = [&](const StartupArchiveTableEntry& entry) {

    if ((entry.layout_mask & options.layout_flags) == 0) {
      return false;
    }
    return !options.online_mode || entry.type != kArchiveTypeOptional;
  };

  int total = static_cast<int>(patch_chain.size());
  for (const auto& entry : kStartupArchiveTable) {
    if (entry.type == kArchiveTypeStreaming) {
      total += options.streaming_ready ? 1 : 0;
    } else if (entry.type == kArchiveTypeAlternate || archive_is_enabled(entry)) {
      ++total;
    }
  }
  int cur = 0;

  const auto fire_progress = [&](const std::string& name) {
    if (options.progress) options.progress(name, ++cur, total);
  };

  int patch_priority = kStockPatchArchiveBasePriority;
  for (const auto& patch_path : patch_chain) {
    fire_progress(patch_path.filename().string());
    MountStartupPatchArchive(vfs, patch_path, patch_priority++);
  }

  for (const auto& entry : kStartupArchiveTable) {
    if (entry.type != kArchiveTypeAlternate) {
      continue;
    }
    const std::string expanded = expand_archive_name(entry.name);
    fire_progress(expanded);
    if (TryMountArchiveByName(vfs, game_root, retail_root, expanded,
                              patch_priority)) {
      ++patch_priority;
    }
  }

  int base_priority = kStockBaseArchiveTopPriority;
  for (const auto& entry : kStartupArchiveTable) {
    if (entry.type == kArchiveTypeAlternate ||
        entry.type == kArchiveTypeStreaming) {
      continue;
    }
    if (archive_is_enabled(entry)) {
      const std::string expanded = expand_archive_name(entry.name);
      fire_progress(expanded);
      if (!TryMountArchiveByName(vfs, game_root, retail_root, expanded,
                                 base_priority) &&
          entry.type == kArchiveTypeSplitLayout) {

        Log(LogLevel::kError,
            "[MPQ] Failed to open archive " + expanded +
                ". Missing or corrupted data");
      }
    }
    --base_priority;
  }

  if (options.streaming_ready) {
    const int streaming_priority = options.online_mode
                                       ? kStockPatchArchiveBasePriority
                                       : base_priority;
    for (const auto& entry : kStartupArchiveTable) {
      if (entry.type != kArchiveTypeStreaming) {
        continue;
      }
      const std::string expanded = expand_archive_name(entry.name);
      fire_progress(expanded);
      TryMountArchiveByName(vfs, game_root, retail_root, expanded,
                            streaming_priority);
    }
  }
}

}
