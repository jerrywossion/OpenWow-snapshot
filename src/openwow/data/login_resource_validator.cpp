#include "openwow/data/login_resource_validator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openwow/data/startup_archive_mount.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::data {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

namespace {

constexpr const char* kDefaultStartupLocale = "enUS";

constexpr const char* kSplitLayoutLocaleToken = "----";

std::optional<std::filesystem::path> ResolveExistingRelativePathCaseInsensitive(
    const std::filesystem::path& root,
    const std::string_view relative_path) {
  return openwow::platform::filesystem::
      ResolveExistingRelativePathCaseInsensitive(root, relative_path);
}

std::filesystem::path ResolveConfiguredArchiveDataDir(
    const std::filesystem::path& client_root) {
  const auto& state = GetStartupFileSystemState();
  if (!state.executable_base_path.empty() && !state.archive_data_path.empty()) {
    std::string archive_data_path = state.archive_data_path;
    std::replace(archive_data_path.begin(), archive_data_path.end(), '\\', '/');
    return (client_root / std::filesystem::path(archive_data_path))
        .lexically_normal();
  }
  return ResolveDataDir(client_root);
}

bool IsKnownLocale(const std::string& locale) {
  for (const char* l : GetStartupLocaleRing()) {
    if (locale == l) return true;
  }
  return false;
}

std::string NormalizeConfiguredLocale(const std::string& configured_locale) {
  if (configured_locale.empty() || configured_locale == "****") {
    return kDefaultStartupLocale;
  }
  return configured_locale;
}

std::string NormalizeStartupProbeLocale(const std::string& configured_locale) {
  std::string normalized = NormalizeConfiguredLocale(configured_locale);
  if (normalized.size() > 4) {
    normalized.resize(4);
  }
  return normalized;
}

bool AnyExists(const openwow::vfs::VirtualFileSystem& vfs,
               const std::vector<std::string>& candidates) {
  for (const auto& c : candidates)
    if (vfs.Exists(c)) return true;
  return false;
}

}

std::string DetectLocale(const std::string& game_data_root,
                         const std::string& preferred_locale) {
  namespace fs = std::filesystem;

  if (!preferred_locale.empty() && IsKnownLocale(preferred_locale)) {
    Log(LogLevel::kInfo, "[Locale] Using configured locale: " + preferred_locale);
    return preferred_locale;
  }

  const auto data_dir = ResolveExistingDataDir(fs::path(game_data_root));
  if (data_dir.empty() || !fs::is_directory(data_dir)) {
    Log(LogLevel::kWarn, "[Locale] Cannot resolve Data directory from: " + game_data_root);
    return {};
  }

  for (const char* loc : GetStartupLocaleRing()) {
    const std::string relative =
        std::string(loc) + "/locale-" + std::string(loc) + ".MPQ";
    auto probe = ResolveExistingRelativePathCaseInsensitive(data_dir, relative);
    if (probe.has_value() &&
        openwow::platform::filesystem::PathIsRegularFile(*probe)) {
      Log(LogLevel::kInfo, "[Locale] Detected locale: " + std::string(loc)
                               + " (found " + probe->string() + ")");
      return std::string(loc);
    }
  }

  Log(LogLevel::kWarn, "[Locale] No locale-{L}.MPQ found under " + data_dir.string());
  return {};
}

openwow::vfs::VirtualFileSystem BuildLoginVfs(const std::string& game_data_root,
                                              const std::string& enhanced_assets_root,
                                              const std::string& locale,
                                              const std::string& retail_install_root) {
  return BuildLoginVfs(game_data_root, {}, enhanced_assets_root, locale, retail_install_root);
}

openwow::vfs::VirtualFileSystem BuildLoginVfs(const std::string& game_data_root,
                                              MountProgressFn progress,
                                              const std::string& enhanced_assets_root,
                                              const std::string& locale,
                                              const std::string& retail_install_root) {
  namespace fs = std::filesystem;
  openwow::vfs::VirtualFileSystem vfs;

  const auto game_root = ResolveStartupClientRoot(fs::path(game_data_root));
  if (!game_root.empty()) {
    const auto resolved_retail_root =
        ResolveRetailInstallRoot(fs::path(retail_install_root));

    vfs.Mount({
        .id = "base-game-data",
        .kind = openwow::vfs::MountKind::kFilesystem,
        .source_root = game_root,
        .priority = kLooseClientRootPriority,
        .enabled = true,
    });

    const auto extracted_data_dir = ResolveConfiguredArchiveDataDir(game_root);
    if (!extracted_data_dir.empty() && fs::is_directory(extracted_data_dir)) {
      vfs.Mount({
          .id = "base-game-data-data-subdir",
          .kind = openwow::vfs::MountKind::kFilesystem,
          .source_root = extracted_data_dir,
          .priority = kLooseDataDirectoryPriority,
          .enabled = true,
      });

      if (IsDataDirectory(game_root)) {
        const auto parent = game_root.parent_path();
        if (!parent.empty() && fs::is_directory(parent)) {
          vfs.Mount({
              .id = "base-game-data-parent",
              .kind = openwow::vfs::MountKind::kFilesystem,
              .source_root = parent,
              .priority = kLooseParentRootPriority,
              .enabled = true,
            });
        }
      }
    }

    const bool has_common_archive =
        HasCommonArchiveLayout(game_root, resolved_retail_root);
    MountStartupArchives(
        &vfs,
        {
            .game_root = game_root,
            .retail_install_root = resolved_retail_root,
            .locale_token =
                has_common_archive
                    ? DetectLocaleRing(NormalizeConfiguredLocale(locale),
                                       game_root.string(),
                                       resolved_retail_root.string())
                    : kSplitLayoutLocaleToken,

            .layout_flags = has_common_archive ? kArchiveLayoutFlagsCommon
                                               : kArchiveLayoutFlagsSplit,

            .online_mode = IsOnlineModeActive(),
            .streaming_ready = IsStreamingInitialized(),
            .progress = std::move(progress),
        });
  }

  const auto runtime_override_root =
      ResolveStartupWritablePath("ContentOverrides");
  std::error_code runtime_override_ec;
  if (fs::is_directory(runtime_override_root, runtime_override_ec) &&
      !runtime_override_ec) {
    vfs.Mount({
        .id = "runtime-override",
        .kind = openwow::vfs::MountKind::kEnhancedOverride,
        .source_root = runtime_override_root,
        .priority = kRuntimeOverridePriority,
        .enabled = true,
    });
  }

  if (!enhanced_assets_root.empty()) {
    vfs.Mount({
        .id = "enhanced-override",
        .kind = openwow::vfs::MountKind::kEnhancedOverride,
        .source_root = enhanced_assets_root,
        .priority = kEnhancedOverridePriority,
        .enabled = true,
    });
  }

  vfs.PrewarmMpqArchives();

  vfs.PrewarmFileEnumeration("/Interface/AddOns", true,
                             openwow::vfs::MountKind::kMpqArchive);

  return vfs;
}

std::uint8_t DetermineStartupExpansionLevel(
    const openwow::vfs::VirtualFileSystem& vfs) {

  if (vfs.CanOpenMpqMount("mpq:lichking.mpq")) {
    return 2;
  }
  if (vfs.CanOpenMpqMount("mpq:expansion.mpq")) {
    return 1;
  }
  return 0;
}

LoginResourceValidationResult ValidateLoginResources(const openwow::vfs::VirtualFileSystem& vfs) {
  LoginResourceValidationResult result;

  if (!AnyExists(vfs,
                 {
                     "/Interface/GlueXML/GlueParent.xml",
                     "/Interface/GlueXML/AccountLogin.xml",
                     "/Interface/GlueXML/RealmList.xml",
                     "/Interface/GlueXML/CharacterSelect.xml",
                 })) {
    result.missing_paths.push_back("/Interface/GlueXML/*.xml");
  }

  if (!AnyExists(vfs,
                 {
                     "/Interface/GlueXML/GlueParent.lua",
                     "/Interface/GlueXML/GlueStrings.lua",
                 })) {
    result.missing_paths.push_back("/Interface/GlueXML/*.lua");
  }

  result.ok = result.missing_paths.empty();
  return result;
}

LoginResourceValidationResult ValidateLoginResources(const std::string& game_data_root) {
  const auto vfs = BuildLoginVfs(game_data_root);
  return ValidateLoginResources(vfs);
}

std::string DetectLocaleRing(const std::string& preferred_locale,
                             const std::string& game_data_root,
                             const std::string& retail_install_root) {
  namespace fs = std::filesystem;
  const auto game_root = ResolveStartupClientRoot(fs::path(game_data_root));
  const auto resolved_retail_root =
      ResolveRetailInstallRoot(fs::path(retail_install_root));
  const auto& locale_ring = GetStartupLocaleRing();
  const std::string normalized_preferred =
      NormalizeStartupProbeLocale(preferred_locale);

  if (!HasCommonArchiveLayout(game_root, resolved_retail_root)) {
    StartupLocaleAvailability all_available{};
    all_available.fill(true);
    SetStartupLocaleAvailability(all_available);
    return normalized_preferred;
  }

  StartupLocaleAvailability available{};
  const auto probe_roots =
      ResolveArchiveProbeNativeRoots(game_root, resolved_retail_root);
  for (std::size_t i = 0; i < locale_ring.size(); ++i) {
    const char* lc = locale_ring[i];
    for (int root_index = 0; root_index < 3; ++root_index) {
      if (ResolveArchiveProbeFileNative(root_index, probe_roots,
                                        "****\\locale-****.MPQ", lc)
              .has_value()) {
        available[i] = true;
        break;
      }
    }
  }
  SetStartupLocaleAvailability(available);

  const int start =
      FindStartupLocaleRingIndexOrEnUSFallback(normalized_preferred);

  for (std::size_t k = 0; k < locale_ring.size(); ++k) {
    const std::size_t idx =
        (static_cast<std::size_t>(start) + k) % locale_ring.size();
    if (available[idx]) {
      Log(LogLevel::kInfo,
          "[Locale] Ring-detected locale: " + std::string(locale_ring[idx]));
      return std::string(locale_ring[idx]);
    }
  }

  Log(LogLevel::kWarn,
      "[Locale] No locale-{L}.MPQ found in any ring slot; falling back to enUS.");
  return "enUS";
}

}
