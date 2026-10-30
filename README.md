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
- Local integration tests for multi-segment reconstruction and segmented resume without re-downloading completed chunks.
- Startup recovery that marks interrupted active records as paused.

Not implemented yet:
- Adaptive connection tuning and benchmarked acceleration (actual speed-up depends on the server and connection).
- FTP support, advanced proxy/SOCKS controls, authentication manager, scheduled queues, and speed limits.
- Browser extensions and the C++ Native Messaging host.
- HLS/DASH parsing, video detection, Link Grabber, and platform packaging.

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
