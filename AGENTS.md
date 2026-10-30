# OIDM — Mandatory engineering rules

These rules apply to every implementation, refactor, generated file, dependency decision, and build workflow in this repository.

## Technology is fixed
- Implement the desktop application in **C++20 and Qt 6**.
- Use **Qt Widgets** for the native desktop UI and **Qt Network** for application networking.
- Use **CMake** as the only native build-system entry point.
- Use **SQLite** for local persistence, accessed through Qt SQL (`QSQLITE`).
- Keep the core download engine independent of widgets and presentation code.
- Target Windows 10/11 and modern Linux distributions. Preserve a path to portable Windows and Linux packaging.

## Explicitly prohibited for the desktop application
- Do not introduce Tauri, Electron, React, TypeScript, JavaScript, Node.js, Rust, or a webview-based desktop shell.
- Do not keep a second implementation or duplicate the download engine in another language.
- Browser extensions are the only planned JavaScript/TypeScript exception; they must communicate with the C++ application through a documented Native Messaging protocol.
- Do not add kernel drivers or require administrator privileges for normal downloads or browser integration.

## Engineering standards
- Prefer small, cohesive C++ classes, RAII, Qt parent ownership, and explicit state transitions.
- No detached threads, unbounded queues, blocking network operations on the GUI thread, or raw owning pointers.
- Every network operation needs timeouts, cancellation, bounded retries, redirect policy, and clear error reporting.
- Treat URLs, filenames, HTTP headers, browser messages, and downloaded metadata as untrusted input.
- Never concatenate untrusted input into SQL. Use prepared statements.
- Never overwrite an existing destination until a completed temporary download has been validated.
- Resume only when the server's range response is validated. Never append a full `200 OK` response to a partial file.
- Concurrent segments must be disjoint, bounded, independently resumable, and verified before assembly.
- Do not claim acceleration, video detection, browser integration, or cross-platform support until implemented and tested.
- Add tests for parsing, state transitions, range validation, persistence, and failure recovery.
- Keep `README.md`, architecture docs, and build instructions synchronized with the actual code.
- Do not copy proprietary IDM code, assets, strings, icons, or extension source. Reimplement behavior independently and use clean-room-compatible interfaces.

## Quality gates
- Configure and build with CMake before claiming a milestone complete.
- Run available automated tests and at least one headless application smoke test on Linux.
- Keep warnings visible; fix warnings introduced by OIDM rather than suppressing them globally.
- Build Windows from the same CMake source tree using a documented Qt 6 toolchain.
