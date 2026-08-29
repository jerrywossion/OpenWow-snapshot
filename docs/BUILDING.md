# Building OpenWoW

The client builds with CMake and Ninja on macOS, Linux and Windows. Native iOS
device and simulator applications use CMake's Xcode generator.
Dependencies come from a **vcpkg manifest** (`vcpkg.json`, baseline pinned in
`vcpkg-configuration.json`, overlay ports in `vcpkg-overlay-ports/`), so you do
not install them by hand.

You need a C++20 toolchain and CMake 3.24 or newer.

## 1. vcpkg bootstrap

The presets expect a vcpkg checkout at `.vcpkg` (git-ignored) at the repository
root. Use the baseline commit the manifest is pinned to, so you get the same
dependency versions the project was built against:

```sh
BASELINE=$(python3 -c 'import json;print(json.load(open("vcpkg-configuration.json"))["default-registry"]["baseline"])')
git clone https://github.com/microsoft/vcpkg .vcpkg
git -C .vcpkg checkout "$BASELINE"
./.vcpkg/bootstrap-vcpkg.sh -disableMetrics      # bootstrap-vcpkg.bat on Windows
```

The first configure builds every dependency (FFmpeg, Boost, bgfx, SDL2, ...) —
**expect 30–60 minutes cold**. Enable vcpkg binary caching
(`VCPKG_BINARY_SOURCES`) if you build on more than one machine.

## 2. Build

```sh
cmake --preset release
cmake --build --preset release --target openwow-client -j 4
# the client is build/release/apps/client/openwow-client
```

Available desktop configure presets (Ninja, build tree `build/<preset>`):

| Preset | Purpose |
| --- | --- |
| `debug` | Debug build |
| `release` | Optimised build |
| `release-lto` | Release + ThinLTO (`OPENWOW_ENABLE_THINLTO=ON`) |
| `release-pgo-generate` | Release, instrumented for profile generation |
| `release-pgo-use` | Release + ThinLTO using the generated profile |

Build presets of the same names exist for each. The PGO lanes are used in
sequence: build with `release-pgo-generate`, exercise the client, then build
`release-pgo-use`, which reads the profile from
`build/release-pgo-generate/pgo/openwow.profdata`.

Native iOS presets use Xcode, target arm64 and require an Apple Silicon host:

| Preset | Purpose |
| --- | --- |
| `ios-simulator` | self-contained iOS Simulator `.app`, including local `Data/` |
| `ios-device` | self-contained device `.app`, including local `Data/` |
| `ios-simulator-code-only` | compile/link validation without copying `Data/` |
| `ios-device-code-only` | unsigned-device compile/link validation without copying `Data/` |

The standard iOS presets intentionally embed `../LocalData/335a/Data`. The
code-only variants keep iteration fast, but their result cannot start a game
session because iOS has no adjacent game-install directory to discover.

### Options

| Option | Default | Meaning |
| --- | --- | --- |
| `OPENWOW_BUILD_CLIENT` | ON | build `openwow-client` |
| `OPENWOW_ENABLE_MPQ_VFS` | ON | StormLib-backed MPQ VFS (required to read a game install) |
| `OPENWOW_LOCAL_CONTENT_ROOT` | `../LocalData/335a` | local build-12340 content root containing `Data/`; compiled in only when present |
| `OPENWOW_EMBED_GAME_DATA` | OFF globally; ON in standard iOS presets | copy the local `Data/` into an Apple `.app` for path-free startup |
| `OPENWOW_IOS_BUNDLE_IDENTIFIER` | `org.openwow.client` | bundle identifier used by the native iOS target |
| `OPENWOW_IOS_DEVELOPMENT_TEAM` | empty | Apple team identifier used for automatic iOS signing |
| `OPENWOW_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |
| `OPENWOW_ENABLE_CLANG_TIDY` | OFF | run clang-tidy while compiling |
| `OPENWOW_ENABLE_THINLTO` | OFF | ThinLTO (clang) / LTCG (MSVC) / `-flto=auto` (GCC) |

## 3. Per-OS notes

**macOS** — Xcode command line tools, plus
`brew install cmake ninja nasm autoconf automake libtool` (nasm and the
autotools are needed by the FFmpeg port). Set `CMAKE_OSX_DEPLOYMENT_TARGET` (or
`MACOSX_DEPLOYMENT_TARGET` in the environment) if the binary must run on an
older macOS than the build host; `packaging/macos/Info.plist.in`'s
`LSMinimumSystemVersion` mirrors whatever the executable was linked for.

**iOS** — a full Xcode installation with the iOS SDK is required. The target
uses iOS 15.0 as its deployment baseline, Metal for rendering, and SDL's UIKit
application entry point. Configure a simulator build with:

```sh
cmake --preset ios-simulator
cmake --build --preset ios-simulator --target openwow-client -j 4
```

For a device build, use a unique bundle identifier and the ten-character team
identifier shown by the Apple Developer account in Xcode:

```sh
cmake --preset ios-device \
  -DOPENWOW_IOS_BUNDLE_IDENTIFIER=com.example.openwow \
  -DOPENWOW_IOS_DEVELOPMENT_TEAM=ABCDE12345
cmake --build --preset ios-device --target openwow-client -j 4
```

The output is
`build/ios-device/apps/client/Release-iphoneos/OpenWoW.app`. Install it through
Xcode's Devices and Simulators window or the organization's normal signed-app
deployment flow. Automatic signing still requires a valid certificate and
provisioning profile; those credentials are never stored in this repository.

These presets suppress Xcode's automatic CMake regeneration so its SDK
environment cannot accidentally rebuild host-side vcpkg tools for iOS. Re-run
the `cmake --preset ...` configure command after changing CMake files, adding
resources, or changing the contents of a resource tree.

The initial iOS configure cross-builds the dependency graph and can take as
long as the first desktop configure. The standard build then copies roughly
the full size of the original `Data/` directory into the `.app`; ensure there
is enough free disk space for both the source data and the application bundle.
Because the result contains user-supplied original game assets and is very
large, this workflow targets personal/development deployment, not App Store
distribution.

Keyboard, mouse/trackpad and existing controller input paths are available on
iOS. Dedicated touch gestures and a touch-first interface are outside the
current scope, so practical use currently requires external input hardware.

**Linux (Debian/Ubuntu)** — what the vcpkg ports build against:

```
build-essential ninja-build pkg-config autoconf autoconf-archive automake libtool libtool-bin libltdl-dev nasm yasm python3
libx11-dev libxext-dev libxft-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
libxss-dev libxxf86vm-dev libxkbcommon-dev libwayland-dev wayland-protocols
libegl1-mesa-dev libgl-dev libglu1-mesa-dev libibus-1.0-dev
```

plus `libfuse2t64 file desktop-file-utils zip` for the AppImage step. On aarch64
hosts vcpkg needs `VCPKG_FORCE_SYSTEM_BINARIES=1`.

**Windows** — Visual Studio 2022 with the C++ workload. Configure from a
Developer Command Prompt (`vcvarsall x64`) so Ninja finds `cl.exe`.
`SDL2::SDL2main` is linked automatically and the executable is a GUI-subsystem
app, so it opens without a console window.

## 4. Content roots and running

The client needs the data files from your own copy of the game. The workspace
default is `../LocalData/335a/Data`, outside this Git repository. When that
directory exists at configure time, local builds find it automatically; no
folder picker or launch argument is needed. `--game-data <path>` and
`OPENWOW_GAME_DATA` remain available as explicit overrides.

Project-owned replacement files belong under `assets/overrides` using their
client logical paths. They are mounted above the original archives and are
included in macOS and iOS application bundles. Runtime downloads and generated
content are kept separately under the platform user-data directory. See
`docs/CONTENT_ROOTS.md` for precedence, writable paths, and bundle layout.

## 5. Packaging

`cmake/Packaging.cmake` drives CPack for the `client` install component: ZIP on
macOS and Windows, TGZ on Linux. The `packaging/` directory holds the platform
scaffolding — the macOS and iOS `Info.plist` templates and assets, the Windows
manifest and resource templates, and the Linux AppImage runner and desktop
entry.

```sh
cmake --build build/release --target package
```

To create a self-contained macOS application bundle, configure with
`-DOPENWOW_EMBED_GAME_DATA=ON` before packaging. This copies the local `Data/`
into `OpenWoW.app/Contents/Resources/GameRoot/Data`; it does not add those files
to Git.

The standard iOS presets perform the corresponding local-only packaging at
`OpenWoW.app/GameRoot/Data`. Project overrides are always copied to
`OpenWoW.app/OpenWoWOverrides`. Neither tree is committed as part of a build.
