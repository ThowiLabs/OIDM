# Arquitectura de OIDM

## Decisión tecnológica

La aplicación de escritorio usa C++20, Qt 6 Widgets, Qt Network y Qt SQL. SQLite persiste el historial y los metadatos de reanudación. CMake/Ninja construyen el mismo árbol de fuentes para Linux y Windows.

## Componentes

- `src/core/download.*`: entidad de descarga, estados y formato de tamaños/progreso.
- `src/core/download_repository.*`: almacenamiento SQLite, migraciones y recuperación tras cierre.
- `src/core/download_task.*`: trabajador de red asíncrono; sondeo de capacidades, transferencias segmentadas, fallback secuencial, manifiesto, pausa y cancelación.
- `src/core/download_manager.*`: ciclo de vida de trabajadores, cola y límite global de concurrencia.
- `src/ui/`: ventana Qt, diálogo de alta, modelo de tabla y delegado de progreso.
- `src/browser/` (futuro): host C++ Native Messaging para extensiones Chromium y Firefox.
- `src/platform/` (futuro): integración con Windows/Linux y empaquetado.

El motor utiliza llamadas asíncronas a `QNetworkAccessManager` en el bucle de eventos de Qt; no crea hilos adicionales por transferencia. Las operaciones de red no bloquean la interfaz. **Limitación conocida**: la reconstrucción de ficheros y algunas escrituras a disco se realizan sin un trabajador de E/S dedicado y deberán migrarse a una tarea de disco antes de manejar ficheros enormes en producción. Las señales de Qt mantienen actualizado el modelo de descargas.

## Protocolo de transferencia

1. Validar URL HTTP/HTTPS, sin credenciales incrustadas.
2. Sondear con `Range: bytes=0-0` y `Accept-Encoding: identity`.
3. Activar paralelismo tras una respuesta 206 coherente y tamaño conocido. Un ETag fuerte o Last-Modified es obligatorio para **reutilizar** segmentos de una sesión anterior; sin validador, se redescargan de cero.
4. Dividir el recurso en rangos contiguos, sin solapamientos y con límites de tamaño y concurrencia.
5. Enviar `If-Range` con ETag fuerte o Last-Modified en solicitudes segmentadas cuando está disponible. No reutilizar fragmentos de sesiones previas sin validador estable.
6. Validar cada respuesta: código 206, inicio/fin/tamaño exactos, longitud del cuerpo y validador estable.
7. Escribir cada segmento en su propio archivo temporal y persistir el manifiesto mediante reemplazo atómico.
8. Ensamblar en orden mediante un archivo temporal de destino; verificar tamaño total y publicar solo al completar.
9. Si el servidor ignora Range o la identidad del recurso cambia, cancelar todos los segmentos, descartar ese conjunto y reiniciar secuencialmente desde cero.
10. Si no hay soporte Range, descargar secuencialmente.

No se permite concatenar una respuesta 200 completa sobre un archivo parcial. No se permite ensamblar segmentos incompletos o solapados. No se permite reanudar un manifiesto si URL, longitud o validadores no coinciden.

## Persistencia y recuperación

SQLite conserva la entidad y el estado visible. El manifiesto de segmentos conserva la distribución de rangos, validadores y bytes confirmados de cada temporal. Ambos se escriben de forma segura. En el arranque, cualquier transferencia que quedó activa se marca como pausada; solo se reanuda después de volver a comprobar la identidad del recurso.

## Seguridad y fiabilidad

- HTTP/HTTPS únicamente en la primera fase.
- Redirecciones sin downgrade HTTPS a HTTP.
- No incluir credenciales en registros ni diagnósticos. Pendiente: rechazo explícito y gestión segura de `userinfo` incrustado en URLs.
- No sobrescribir destinos existentes.
- Pendiente: endurecer la creación de archivos temporales frente a enlaces simbólicos y carreras de ruta.
- Mantener temporales dentro de la carpeta de destino elegida.
- Timeouts de transferencia y límites de concurrencia explícitos.
- Los errores conservan temporales reanudables, salvo cancelación explícita o manifiesto inválido.

## Plataformas

- Windows 10/11 x64 primero; ARM64 se evaluará después.
- Linux x86_64 primero.
- AppImage y paquetes nativos se validarán después de resolver plugins y dependencias Qt.
- Las extensiones Chromium/Firefox se comunicarán con un host nativo C++ separado.
