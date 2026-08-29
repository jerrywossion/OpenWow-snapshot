# Content and writable roots

OpenWoW keeps stock build-12340 content, project-owned replacements, runtime
downloads, and user state in separate roots. macOS and iOS application bundles
consume the same content layout without writing into signed application
resources.

## Workspace layout

From the `OpenWow-snapshot` source directory, the default local layout is:

```text
../LocalData/335a/
  Data/                         stock build-12340 MPQ data (not in Git)
assets/overrides/               project-owned files (tracked by Git)
  Interface/...
  DBFilesClient/...
```

Files in `assets/overrides` use client logical paths. Keep original MPQs and
other stock client files out of this directory; this root is for independently
maintained OpenWoW changes only.

## Read precedence

Content roots are selected in this order:

1. `--game-data <path>`
2. `OPENWOW_GAME_DATA`
3. bundled `GameRoot/Data`
4. a `Data/` beside the working directory or executable, including the legacy
   directory beside a macOS `.app`
5. `OPENWOW_LOCAL_CONTENT_ROOT` when its `Data/` existed at configure time

Within the VFS, project-owned `assets/overrides` has priority 1000, runtime
`ContentOverrides` has priority 900, loose stock files use the 280–300 band,
and stock MPQ archives remain below those loose layers. The original Data root
is therefore read-only from the application's perspective.

`OPENWOW_ENHANCED_ASSETS` can still replace the project override root for a
specific development run.

## Writable state

Relative writes no longer follow the stock content root. Configuration,
saved variables, cache data, logs, screenshots, crash reports, `SoundCache.MPQ`,
downloaded agreements, and scan-module updates use the platform user-data root.
On Apple platforms the default is:

```text
~/Library/Application Support/OpenWoW/
```

On iOS, `~` is the application's private data container, so configuration,
logs, cache data and downloaded content remain writable without modifying or
invalidating the signed `.app`.

Runtime-generated content that must participate in VFS lookup is stored under
`ContentOverrides/`. `OPENWOW_USER_DATA` is available for isolated development
or automation runs.

## Application bundles

macOS packaging always installs project-owned overrides at:

```text
OpenWoW.app/Contents/Resources/OpenWoWOverrides/
```

Configure with `-DOPENWOW_EMBED_GAME_DATA=ON` to also install:

```text
OpenWoW.app/Contents/Resources/GameRoot/Data/
```

iOS bundles use the shallow resource layout produced by Xcode:

```text
OpenWoW.app/GameRoot/Data/
OpenWoW.app/OpenWoWOverrides/
```

The standard `ios-simulator` and `ios-device` presets embed the local `Data/`
tree and therefore need no path picker, environment variable, or launch
argument. The signed bundle remains read-only; all runtime mutations go to the
private user-data root described above.
