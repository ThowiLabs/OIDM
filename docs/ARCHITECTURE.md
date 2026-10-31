# Arquitectura de OIDM

## Decisión tecnológica

La aplicación de escritorio usa C++20, Qt 6 Widgets, Qt Network y Qt SQL. SQLite persiste el historial y los metadatos de reanudación. CMake/Ninja construyen el mismo árbol de fuentes para Linux y Windows.

## Componentes

- `src/core/download.*`: entidad de descarga, estados y formato de tamaños/progreso.
- `src/core/download_repository.*`: almacenamiento SQLite, migraciones y recuperación tras cierre.
- `src/core/download_task.*`: trabajador de red asíncrono; sondeo de capacidades, transferencias segmentadas, fallback secuencial, manifiesto, pausa y cancelación.
- `src/core/download_manager.*`: ciclo de vida de trabajadores, cola y límite global de concurrencia.
- `src/ui/`: ventana Qt, diálogo de alta, modelo de tabla y delegado de progreso.
- `src/integration/native_host.cpp`: ejecutable nativo C++ independiente que recibe mensajes WebExtensions (tramas JSON de longitud little-endian de 32 bits).
- `src/integration/native_bridge.*`: servidor local Qt (QLocalServer, socket exclusivo del usuario) entre el host y la interfaz gráfica.
- `extension/chromium/`: extensión Manifest V3 con menú contextual para enviar enlaces HTTP/HTTPS seleccionados expresamente por el usuario.
- Firefox y detección automática de vídeos: pendientes.
- `src/platform/` (futuro): integración con Windows/Linux y empaquetado.

El motor utiliza llamadas asíncronas a `QNetworkAccessManager` en el bucle de eventos de Qt; no crea hilos adicionales por transferencia. Las operaciones de red no bloquean la interfaz. **Limitación conocida**: la reconstrucción de ficheros y algunas escrituras a disco se realizan sin un trabajador de E/S dedicado y deberán migrarse a una tarea de disco antes de manejar ficheros enormes en producción. Las señales de Qt mantienen actualizado el modelo de descargas. El ensamblado segmentado usa `QTemporaryFile` y hace publicación exclusiva, pero todavía queda pendiente mover la copia de segmentos a un trabajador de disco.

## Protocolo de transferencia

1. Validar URL HTTP/HTTPS, sin credenciales incrustadas.
2. Sondear con `Range: bytes=0-0` y `Accept-Encoding: identity`.
3. Activar paralelismo tras una respuesta 206 coherente y tamaño conocido. Un ETag fuerte o Last-Modified es obligatorio para **reutilizar** segmentos de una sesión anterior; sin validador, se redescargan de cero.
4. Dividir el recurso en rangos contiguos, sin solapamientos y con límites de tamaño y concurrencia.
5. Enviar `If-Range` con ETag fuerte o Last-Modified en solicitudes segmentadas cuando está disponible. No reutilizar fragmentos de sesiones previas sin validador estable.
6. Validar cada respuesta: código 206, inicio/fin/tamaño exactos, longitud del cuerpo y validador estable.
7. Escribir cada segmento en su propio archivo temporal y persistir el manifiesto mediante reemplazo atómico.
8. Ensamblar en orden mediante un archivo temporal aleatorio del mismo directorio; verificar tamaño total y publicar solo al completar. En Linux se utiliza `renameat2(RENAME_NOREPLACE)` con alternativa de enlace duro exclusivo; en Windows `MoveFileExW` sin `MOVEFILE_REPLACE_EXISTING`. No basta con comprobar la existencia del destino antes de publicar.
9. Si el servidor ignora Range o la identidad del recurso cambia, cancelar todos los segmentos, descartar ese conjunto y reiniciar secuencialmente desde cero.
10. Si no hay soporte Range, descargar secuencialmente.

No se permite concatenar una respuesta 200 completa sobre un archivo parcial. No se permite ensamblar segmentos incompletos o solapados. No se permite reanudar un manifiesto si URL, longitud o validadores no coinciden.

## Persistencia y recuperación

SQLite conserva la entidad y el estado visible. El manifiesto de segmentos conserva la distribución de rangos, validadores y bytes confirmados de cada temporal. Ambos se escriben de forma segura. En el arranque, cualquier transferencia que quedó activa se marca como pausada; solo se reanuda después de volver a comprobar la identidad del recurso.

## Seguridad y fiabilidad

- HTTP/HTTPS únicamente en la primera fase.
- Redirecciones sin downgrade HTTPS a HTTP.
- No incluir credenciales en registros ni diagnósticos. URLs con userinfo rechazadas por el motor y puente nativo; protección adicional de logs e historial queda por auditar.
- No sobrescribir destinos existentes, incluso cuando aparezcan durante la transferencia; existen pruebas de colisión de destino y de publicación exclusiva.
- Al limpiar los temporales, no seguir enlaces simbólicos de carpetas a archivos ajenos.
- Pendiente: endurecer la creación de archivos temporales frente a enlaces simbólicos y carreras de ruta.
- Mantener temporales dentro de la carpeta de destino elegida.
- Timeouts de transferencia y límites de concurrencia explícitos.
- Los errores conservan temporales reanudables, salvo cancelación explícita o manifiesto inválido.

## Plataformas

- Windows 10/11 x64 primero; ARM64 se evaluará después.
- Linux x86_64 primero.
- AppImage y paquetes nativos se validarán después de resolver plugins y dependencias Qt.
- Las extensiones Chromium/Firefox se comunicarán con un host nativo C++ separado.

## Integración con navegadores (fase inicial)

1. La extensión Chromium requiere permisos mínimos `contextMenus`, `nativeMessaging` y `notifications`. No solicita `cookies`, `webRequest`, `tabs` ni `host_permissions`.
2. El navegador invoca `com.thowilabs.oidm` mediante Native Messaging con un mensaje JSON de tamaño limitado.
3. El host C++ valida el tipo de operación y transmite la solicitud a la aplicación mediante `QLocalSocket` y `QLocalServer::UserAccessOption`.
4. La interfaz comprueba que el enlace sea HTTP/HTTPS sin credenciales, desinfecta el nombre, reserva un nombre libre en Descargas y añade la tarea a SQLite.
5. El host devuelve el ID de la tarea como confirmación.

La integración end-to-end se comprobó en Linux desde un cliente de prueba hasta el archivo y la fila de SQLite. La instalación en Chrome/Edge real y la ejecución Windows siguen pendientes de validación.

## GUI classic dark preview (0.2.0)

The Qt Widgets view contains a source-of-truth `DownloadTableModel` and a `QSortFilterProxyModel` for file-type/status categories. Browser enqueue persists synchronously into `DownloadRepository`, updates the source model, resets the category selection to All, maps source→proxy selection, and creates an optional modeless `DownloadProgressDialog`. Subsequent core progress updates (including per-segment percentages calculated from actual transfer byte counts) update both views. The UI holds `QPointer`s to modeless dialogs; no cleanup signals can access members during the parent destructor.

An existing `QLocalServer` listener owns each per-user bridge name. A second GUI instance requests activation through an IPC `activate` message then exits, preventing simultaneous misleading empty windows. The original IDM's proprietary images/code are not used.

## Browser installer / consent boundary (0.3.0)

A dedicated Qt Widgets `BrowserSetupDialog` detects Chrome/Edge, invokes the pre-existing C++ native-host registration/diagnostic commands and opens each browser's official extensions page. Extension assets are kept beside the desktop app in `extension/chromium`, with a fixed public ID. No private signing key is shipped. The Windows NSIS installer deploys the complete portable bundle to `%LOCALAPPDATA%/Programs/OIDM` (per-user) and registers the native bridge without admin privileges. Browser activation is explicitly left to the user through official Chrome/Edge UI, because non-Store local extension silent installation is prohibited. The host's unregistration routine compares its manifest executable path to the running native host before removing registry entries; other installs are not disturbed.

## Three-phase browser handoff (0.4.0)

Chrome download events are asynchronous; `onCreated` does not prevent the browser from creating its own temporary download. The MV3 worker immediately calls `chrome.downloads.pause`, then sends `enqueue` with `{browserOwned:true}` and waits for persistence ACK (including a generated record ID). The C++ app inserts status `Queued` but **does not display the Start button yet**, so no duplicate transfer can begin. The worker cancels and erases the original Chrome download; only then does it send `released(downloadId)` to the Native Messaging host, which forwards to the GUI and shows a modeless metadata/destination confirmation. If Chrome cancel fails, `abandon` safely removes the queued entry and the browser resumes the original. Explicit right-click downloads use `browserOwned:false` because no Chrome download event must be cancelled.

This handoff prevents *two simultaneously active transfers*, but cannot guarantee the browser transfers zero bytes before its `onCreated` event; this is a limitation of the Chromium extensions API. The desktop DownloadTask now reads `QSettings transfer/maxConnections` (default 6, bounded 1..32), preserves range validator rules and caps segments by file size. The settings dialog writes user preferences, the offer dialog HEAD-probes metadata asynchronously, and `DownloadRepository::updateQueuedDestination` accepts destination changes only while the record is queued.

## Paused browser capture state

`browser/capturePaused` in the per-user `QSettings` is authoritative. Desktop Qt toolbar toggles the setting; the native host accepts `capture-state` and `set-capture` without launching the GUI. The MV3 worker immediately pauses the browser download and then queries the host state; the desktop's local bridge rejects `enqueue` again when paused to close the state-change race. Active OIDM downloads are unaffected. Browser transfer ownership uses an acknowledge/cancel/released handshake; failed browser cancellation sends `abandon` to discard OIDM's inactive reservation. If capture is disabled, the worker resumes the browser download without handing it to OIDM.
