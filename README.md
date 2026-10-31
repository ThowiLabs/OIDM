# OIDM — Open Internet Download Manager

OIDM is an original, open-source download manager targeting Windows and Linux.

## Mandatory technology rule

The desktop application is developed exclusively in **C++20 and Qt 6 Widgets**, with **CMake** as its build system and **SQLite through Qt SQL** for persistence. Qt Network is the current networking backend. Do not introduce Tauri, Electron, Rust, React, TypeScript, Node.js, or a web-based desktop shell. The binding engineering requirements are in [AGENTS.md](AGENTS.md).

## Current implementation status

Implemented:
- Native Qt Widgets desktop window, toolbar, categories, download table, progress delegate, and add-download dialog.
- SQLite-backed persistent download queue.
- Nonblocking HTTP/HTTPS requests through Qt Network and a bounded 3-download queue.
- Up to 8 simultaneous byte-range segments per eligible file, with independent temporary files and byte-accurate reconstruction.
- Pausing, cancellation, legacy single-stream partial resume, and recovery of validated segment manifests.
- Strict response range validation and fallback to a fresh transfer when a server ignores Range with `200 OK`.
- Collision-safe publication of completed files without replacing an existing destination, including files created mid-transfer; atomic no-replace on supported Linux filesystems, exclusive publication fallback, and Windows no-replace move.
- Guarded cleanup of temporary artifacts without following directory symlinks.
- Local integration tests for multi-segment reconstruction, segmented resume without re-downloading completed chunks, pause/cancel, name collisions and symlink-safe cleanup.
- Repeated CTest runs and an AddressSanitizer/UndefinedBehaviorSanitizer test build on Linux.
- Startup recovery that marks interrupted active records as paused.
- A C++ Native Messaging host and a Chromium MV3 context-menu extension (Chrome/Edge/Chromium) for explicitly sending HTTP/HTTPS links to OIDM.
- User-level Native Messaging registration through `oidm-native-host --register-chromium EXTENSION_ID`.

Not implemented yet:
- Adaptive connection tuning and benchmarked acceleration (actual speed-up depends on the server and connection).
- FTP support, advanced proxy/SOCKS controls, authentication manager, scheduled queues, and speed limits.
- Automatic interception of browser downloads and Firefox integration (first Chromium extension and native host exist).
- HLS/DASH parsing, video detection, Link Grabber, and platform packaging.
- GUI-independent disk assembly for huge downloads; segmented assembly currently runs synchronously in the Qt event loop.

OIDM now has an initial working parallel segmented engine, verified against local HTTP test fixtures. This is **not yet full IDM feature parity**, and the actual speed increase depends on server limits, latency and available bandwidth. Large-file assembly and disk I/O performance still need optimization and stress tests.

## Build on Linux

Requirements: CMake 3.25+, a C++20 compiler, Qt 6.6+ development packages (Core, Gui, Widgets, Network, Sql), and the Qt SQLite driver.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/oidm
```

## Build on Windows

Use Qt 6.6+ with the MSVC or MinGW kit matching the compiler, CMake, and Ninja. Configure and build from the same CMake source tree. Windows deployment and portable packaging will be added after the core transfer and platform integration are validated.

## Browser extension (preview)

Build both executables and keep `oidm` and `oidm-native-host` in the same directory. Load `extension/chromium` unpacked in Chrome, Chromium, or Edge with Developer mode enabled. Copy the browser-assigned extension ID (32 lowercase letters `a` through `p`) and register it for the current user:

```bash
./build/oidm-native-host --register-chromium YOUR_EXTENSION_ID
```

Restart the browser if needed, then use **Descargar con OIDM** from the context menu of a link. See [`extension/README.md`](extension/README.md) for platform-specific details. The native-host protocol has been exercised end-to-end with the Linux Qt desktop application, a local test HTTP server, and SQLite. The extension itself has not yet been verified through a real browser installation.

**License:** GNU General Public License version 3; see `LICENSE`.

## Portable Windows cross-build (experimental)

The portable Windows x64 binary can also be cross-compiled from Linux with MinGW-w64 using the official Qt 6.8.3 `win64_mingw` SDK and Linux host Qt tools (`moc`, `uic`, `rcc`). These must be configured as host tools when CMake cross-compiles; Windows `.exe` tools cannot run on Linux. After building `oidm.exe` and `oidm-native-host.exe`, generate the runtime-complete ZIP with:

```bash
python3 scripts/package_windows_cross.py \
  --qt /path/to/Qt/6.8.3/mingw_64 \
  --build build-windows-cross \
  --output dist
```

The packaging script verifies imported PE dependencies and includes the Qt Windows platform plugin, SQLite driver, Schannel TLS plugin, MinGW runtimes, and the Chromium extension. Native Windows compilation (MSVC) remains the preferred release workflow.
