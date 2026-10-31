# OIDM — Open Internet Download Manager

OIDM is an original, open-source download manager targeting Windows and Linux.

## Mandatory technology rule

The desktop application is developed exclusively in **C++20 and Qt 6 Widgets**, with **CMake** as its build system and **SQLite through Qt SQL** for persistence. Qt Network is the current networking backend. Do not introduce Tauri, Electron, Rust, React, TypeScript, Node.js, or a web-based desktop shell. The binding engineering requirements are in [AGENTS.md](AGENTS.md).

## Current implementation status

Implemented:
- Native Qt Widgets **dark classic IDM-inspired** desktop window (original icons/assets, not an IDM copy), vertical category tree with working filters, action toolbar, dark download table, and dedicated download progress dialog with real per-segment percentages. Browser-accepted downloads are selected and brought to the foreground automatically.
- SQLite-backed persistent download queue.
- Nonblocking HTTP/HTTPS requests through Qt Network and a bounded 3-download queue.
- Up to 32 configurable HTTP byte-range segments per eligible file (6 by default), with independent temporary files and byte-accurate reconstruction.
- Pausing, cancellation, legacy single-stream partial resume, and recovery of validated segment manifests.
- Strict response range validation and fallback to a fresh transfer when a server ignores Range with `200 OK`.
- Collision-safe publication of completed files without replacing an existing destination, including files created mid-transfer; atomic no-replace on supported Linux filesystems, exclusive publication fallback, and Windows no-replace move.
- Guarded cleanup of temporary artifacts without following directory symlinks.
- Local integration tests for multi-segment reconstruction, segmented resume without re-downloading completed chunks, pause/cancel, name collisions and symlink-safe cleanup.
- Repeated CTest runs and an AddressSanitizer/UndefinedBehaviorSanitizer test build on Linux.
- Startup recovery that marks interrupted active records as paused.
- A C++ Native Messaging host and a Chromium MV3 extension (Chrome/Edge/Chromium) with automatic HTTP(S) download interception, a manual context menu, and native-bridge connectivity checks.
- User-level Native Messaging registration through `oidm-native-host --register-chromium EXTENSION_ID`.

Not implemented yet:
- Adaptive connection tuning and benchmarked acceleration (actual speed-up depends on the server and connection).
- FTP support, advanced proxy/SOCKS controls, authentication manager, scheduled queues, and speed limits.
- Firefox integration and interception of downloads that never produce Chrome's standard `downloads.onCreated` event.
- HLS/DASH parsing, video detection, Link Grabber, and signed Windows installer releases (an unsigned NSIS preview installer and portable Windows build now exist).
- GUI-independent disk assembly for huge downloads; segmented assembly currently runs synchronously in the Qt event loop.

OIDM now has an initial working parallel segmented engine, verified against local HTTP test fixtures. This is **not yet full IDM feature parity**, and the actual speed increase depends on server limits, latency and available bandwidth. Large-file assembly and disk I/O performance still need optimization and stress tests.

## Download visibility and multiple instances

When the browser Native Messaging host accepts a download, OIDM persists the item in SQLite, switches the category view to **Todas las descargas**, selects and scrolls to the new record, shows a native progress/details window and attempts to raise the main window. The host replies `ok=true` only after the record is persisted; transfer failures after acceptance remain visible in the list with **Error** status. A second instance of OIDM no longer silently shows an empty window while the first instance owns the browser IPC socket: it requests activation of the first instance and shows a warning to close old versions before testing.

The `--ui-self-test` executable test checks that a browser-enqueued URL appears in the visible table, is selected, has a progress dialog and remains persisted in SQLite. On Linux, run all tests with `ctest` (including the UI smoke test). Native Windows cross-builds can run the same test under Wine/Xvfb, in addition to Native Messaging end-to-end checks.

The classic toolbar, per-download details and progress dialog are implemented. Speed limiting, completion actions, scheduling and Firefox integration are **not** implemented; their tabs are labeled unavailable instead of pretending to work.

## Build on Linux

Requirements: CMake 3.25+, a C++20 compiler, Qt 6.6+ development packages (Core, Gui, Widgets, Network, Sql), and the Qt SQLite driver.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/oidm
```

## Build on Windows

Use Qt 6.6+ with the MSVC or MinGW kit matching the compiler, CMake, and Ninja. Configure and build from the same CMake source tree. Windows packaging is available as **portable ZIP** or a **per-user NSIS installer**; see the installation section below.

## Browser extension (preview)

**On Windows**, extract the portable ZIP and run `OIDM.exe` first. It registers the bundled Chromium Native Messaging host for the current user (Chrome/Edge, no administrator privileges required). Open `chrome://extensions` or `edge://extensions`, enable Developer mode, remove the previous OIDM extension if one is present, and load unpacked **`extension/chromium` from the NEW ZIP**. Click the extension's icon: it should show an `OK` badge and a connectivity notification. If registration fails, use **Ayuda → Reparar integración Chrome / Edge** inside OIDM and paste the exact extension ID visible on the browser extensions page. Alternatively run `CONFIGURAR-INTEGRACION.cmd`, which registers and diagnoses the host. Reload the extension after either repair. Do not use Windows administrator mode: the registration is per current user.

When Chrome/Edge starts an HTTP(S) file transfer, including the CDN URL generated by a MediaFire download button, the extension pauses it, queues it in OIDM and cancels Chrome's copy **only after OIDM confirms acceptance**. If the native host is missing or rejects the transfer, Chrome resumes the original download. Right-click **Descargar con OIDM** remains available for explicitly sending a link.

MediaFire share-page URLs are HTML landing pages, not downloadable file URLs. Use the page's **Download** button. Some private/session-protected links depend on cookies not available to OIDM; those remain outside the supported beta behavior. The extension cannot bypass DRM, authentication or browser security restrictions. The Chromium event bridge has automated mocks and a real Native Messaging + Qt desktop transfer was tested under Wine; an actual MediaFire website session on Windows still needs user validation.

See [`extension/README.md`](extension/README.md) for installation and troubleshooting. **On Linux**, manually register the extension ID `ciipklijloaknncdmnlnnoimjfcllohi` with `oidm-native-host --register-chromium <id>`.

## Portable Windows cross-build (experimental)

The portable Windows x64 binary can also be cross-compiled from Linux with MinGW-w64 using the official Qt 6.8.3 `win64_mingw` SDK and Linux host Qt tools (`moc`, `uic`, `rcc`). These must be configured as host tools when CMake cross-compiles; Windows `.exe` tools cannot run on Linux. After building `oidm.exe` and `oidm-native-host.exe`, generate the runtime-complete ZIP with:

```bash
python3 scripts/package_windows_cross.py \
  --qt /path/to/Qt/6.8.3/mingw_64 \
  --build build-windows-cross \
  --output dist
```

The packaging script verifies imported PE dependencies and includes the Qt Windows platform plugin, SQLite driver, Schannel TLS plugin, MinGW runtimes, and the Chromium extension. Native Windows compilation (MSVC) remains the preferred release workflow.

### Diagnóstico de errores de host nativo (Windows)

Si la extensión muestra **"Specified native messaging host not found"**, el error ocurre *antes* de que MediaFire pueda enviarse al programa. Utiliza **Ayuda → Reparar integración Chrome / Edge...** dentro de OIDM y pega el ID actual (32 letras) de tu extensión instalada. Alternativamente ejecuta `CONFIGURAR-INTEGRACION.cmd`, que ofrece registro y verificación automática. El comando `oidm-native-host.exe --diagnose-chromium <id>` comprueba el manifiesto, el ejecutable, el ID autorizado y las dos claves del Registro HKCU. Los IDs que el usuario registró manualmente se conservan en ejecuciones posteriores. **No ejecutes la reparación como administrador** si normalmente abres Chrome con tu cuenta habitual.

## Instalación guiada de Chrome y Edge (0.3.0)

En Windows utiliza el archivo `OIDM-Windows-x64-v0.4.1-Setup.exe`: el instalador NSIS se ejecuta **sin privilegios de administrador**, instala el ejecutable, la extensión y las dependencias de Qt bajo `%LOCALAPPDATA%\Programs\OIDM`, registra el Native Messaging host en HKCU y añade accesos al menú Inicio. Al finalizar, abre OIDM con el asistente de conexión. El primer inicio del paquete portable también ofrece el asistente. En cualquier momento está disponible en **Ayuda → Instalar / conectar extensión Chrome y Edge**.

El asistente comprueba la integración, detecta Chrome/Edge instalados, abre directamente `chrome://extensions` / `edge://extensions`, ofrece mostrar la carpeta de la extensión y copia su ruta al portapapeles. El usuario solamente activa modo desarrollador, pulsa *Cargar descomprimida* y selecciona la carpeta mostrada. No necesita copiar el ID de la extensión, modificar el registro ni ejecutar CMD. Los registros preexistentes pueden repararse por separado.

**Restricción de Chrome/Edge**: en Windows, las extensiones distribuidas fuera de la Chrome Web Store / Edge Add-ons no pueden instalarse silenciosamente desde una aplicación normal. Una verdadera instalación de un clic requerirá publicar la extensión en la tienda y pedir al usuario que confirme su instalación. OIDM no modifica las políticas empresariales del navegador ni altera preferencias de Chrome para eludir esta protección. <https://developer.chrome.com/docs/extensions/how-to/distribute>

La desinstalación elimina los archivos y accesos instalados y desregistra el host **solo si ese host pertenece a esta instalación**; conserva los datos de descargas y el historial del usuario. La extensión puede seguir apareciendo en Chrome y debe retirarse desde `chrome://extensions` si el usuario desea quitarla.

Para generar ambos entregables desde Linux se necesita `makensis` además de Qt/MinGW. La orden `python3 scripts/package_windows_cross.py --qt ... --build ... --output dist` produce el ZIP y el instalador NSIS cuando `makensis` está disponible.

## Descargas con confirmación y ajustes de conexiones (0.4.0)

**Configuración inicial: seis conexiones por archivo.** En **Opciones → Conexiones** se permite ajustar entre 1 y 32, aplicado a las próximas descargas y persistido en QSettings. Se usa una sola conexión para archivos inferiores a 768 KiB o servidores que no soporten rangos HTTP (`206 Partial Content`). Un servidor que omite `ETag` o `Last-Modified` **sí puede usar seis segmentos en la sesión actual**, pero sus fragmentos no se reutilizan para reanudar en otra sesión por motivos de integridad. El límite también depende del tamaño útil de segmento. Se conserva la validación de rangos y publicación segura. En **Opciones → Descargas** se cambia la carpeta de guardado predeterminada. Las pestañas General, Conexiones, Descargas y Navegadores forman la primera versión real del panel, **no la totalidad de las opciones del IDM comercial**.

El cuadro previo a la transferencia enseña nombre, URL, tamaño anunciado por Chrome o por una petición HEAD asíncrona, tipo y destino editable, con botones **Iniciar descarga** y **Cancelar**. No empieza a transferir bytes en OIDM hasta confirmar. Una descarga de Chrome/Edge utiliza un protocolo de *tres fases*: `enqueue(browserOwned=true)` reserva la entrada en SQLite sin iniciarla; la extensión cancela/borra su propia descarga; `released(downloadId)` activa el cuadro de confirmación. Si Chrome no puede cancelar, `abandon(downloadId)` descarta la entrada pendiente y Chrome reanuda. No se almacenan cookies o tokens privados de sesión.

**Límite de la API Chrome**: `downloads.onCreated` ocurre cuando el navegador ya ha iniciado una descarga. OIDM pausa el evento tan pronto como llega, sin esperar una consulta al almacenamiento. Puede haber archivos diminutos que terminen antes de que Chrome permita pausarlos: esos permanecen bajo Chrome y no se envían a OIDM. No hay instalación de un controlador ni interceptación de tráfico previa a la solicitud del navegador.

## Pausar capturas del navegador (0.4.0)

OIDM incorpora un control independiente de las descargas activas: **Pausar capturas** en la barra de herramientas y el menú Herramientas / Descargas. Cuando está activado, las **nuevas** descargas HTTP/HTTPS se quedan en Chrome/Edge: la extensión pausa brevemente la descarga mientras consulta el estado compartido; si las capturas están deshabilitadas, la reanuda de inmediato y no crea registros en OIDM. Haz clic de nuevo para reanudar. Las descargas que ya estaban activas dentro de OIDM continúan normalmente.

También puedes hacer clic en el icono de la extensión: **OFF** pausa capturas y **ON** las reactiva; ambos controles usan `QSettings` del usuario a través de Native Messaging, sin modificar las opciones globales de Chrome. Si el host nativo falla, la extensión mantiene la descarga original del navegador. Un cambio de estado durante la transferencia provoca que el receptor de OIDM rechace nuevas inserciones y la extensión reanude Chrome.

El flujo de entrega ahora reserva una entrada `Queued` en OIDM, cancela la descarga de Chrome y **solo después** abre la confirmación de Iniciar/Cancelar. Si falla la cancelación del navegador, la entrada reservada se abandona y Chrome continúa, evitando iniciar dos descargas de manera simultánea. Chrome puede comenzar a descargar algunos bytes antes de que MV3 entregue el evento `downloads.onCreated` a la extensión; no se promete interceptar contenido que el navegador ya terminó.

## Corrección de paralelismo 0.4.1

El motor de la versión 0.4.0 exigía equivocadamente `ETag` fuerte o `Last-Modified` incluso para descargar en paralelo dentro de la **misma** transferencia. Esta condición provocaba que algunos CDNs compatibles con `Range` acabaran usando una única conexión. Ahora: (1) el número predeterminado sigue siendo **6** sin preguntas adicionales; (2) para archivos desde 768 KiB se intenta dividir en segmentos (mínimo útil de 128 KiB); (3) se comprueban status `206`, `Content-Range` (inicio, fin y tamaño total), longitud, URL efectiva y, cuando existen, validadores; (4) **sin validador estable, nunca se reutilizan fragmentos guardados en otra sesión**; (5) el diálogo de progreso explica por qué se eligió una conexión o seis segmentos.

La prueba `parallelWithoutEtags` crea un servidor HTTP que devuelve `206 Partial Content` pero omite `ETag`/`Last-Modified`. Comprueba seis solicitudes de rangos superpuestas, reconstrución byte a byte y limpieza de temporales. Sigue existiendo el límite técnico: no podemos forzar rangos si el origen ignora `Range`, si la URL ya expiró o si un servidor responde HTTP 200.

Para evitar confundir varios rangos HTTP/2 multiplexados con una sola conexión TCP, las solicitudes de segmentos se envían con HTTP/2 deshabilitado, de modo que Qt puede utilizar sockets HTTP/1.1 independientes. El número efectivo puede ser menor si el servidor impone límites; OIDM no puede forzar servidores incompatibles.

## Corrección del instalador Windows NSIS (0.4.1, empaquetado corregido)

Se detectó que el instalador generado previamente con `SetCompressor /SOLID lzma` podía contener un **CRC32 NSIS inválido** pese a que `7z t` extrajera sus ficheros sin errores. Esto provocaba el mensaje **"Installer integrity check has failed"** al ejecutarlo en Windows. La causa se reprodujo leyendo el `firstheader` y comparando el CRC almacenado con el CRC real.

Ahora se genera con `SetCompressor /SOLID zlib` y `CRCCheck force`; el empaquetador `scripts/package_windows_cross.py` inspecciona el encabezado NSIS, rechaza el instalador cuando el CRC no coincide y entrega el `.exe` junto con un `.zip` alternativo que contiene el mismo instalador. El test de regresión `python3 -m unittest discover -s tests -p test_windows_packaging.py` valida que una alteración de un solo byte provoque un fallo. **No se deshabilita la verificación de integridad ni se pide usar `/NCRC`.**

Se verificó el instalador corregido mediante CRC NSIS, `7z t`, instalación silenciosa bajo Wine, comparación byte a byte del `OIDM.exe` instalado con el compilado y prueba `--ui-self-test` exitosa. La versión de aplicación sigue siendo 0.4.1; cambió únicamente el empaquetado del instalador, que es más grande debido a la compresión Zlib. Para detectar una descarga modificada también conviene comparar el SHA256 del archivo recibido con el publicado junto al enlace.
