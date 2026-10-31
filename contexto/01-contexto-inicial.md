# OIDM — Contexto persistente

Fecha de actualización: 2026-10-30

## Objetivo

Crear OIDM, un gestor de descargas original y de código abierto para Windows y Linux. El producto debe ser independiente de Internet Download Manager: no reutiliza binarios, código, recursos gráficos ni marcas de Tonec. La meta funcional es ofrecer descargas fiables, reanudables y aceleración segmentada cuando el servidor HTTP lo permita.

## Decisiones técnicas vigentes

- Lenguaje: C++20.
- UI: Qt 6 Widgets.
- Red: Qt Network (`QNetworkAccessManager`).
- Persistencia: SQLite mediante el driver `QSQLITE` de Qt SQL.
- Build: CMake 3.25+ y Ninja.
- Plataformas: Windows 10/11 x64 y Linux x86_64 inicialmente.
- No usar Tauri, Electron, Rust, React, TypeScript, Node.js ni una shell web para la aplicación de escritorio.
- Las extensiones WebExtensions serán el único componente JavaScript futuro y hablarán con un host nativo C++ mediante Native Messaging.

## Arquitectura vigente

- `src/core/download.*`: modelo de dominio y estados de descarga.
- `src/core/download_repository.*`: persistencia SQLite y recuperación tras cierres inesperados.
- `src/core/download_task.*`: trabajador asíncrono de red por descarga; debe admitir sondeo de rangos, segmentos paralelos, fallback secuencial, pausa, cancelación y reanudación.
- `src/core/download_manager.*`: ciclo de vida, límite de descargas simultáneas y planificación de la cola.
- `src/ui/`: ventana principal, diálogo de alta, modelo de tabla y delegado de progreso.
- `tests/`: pruebas locales de protocolo, reanudación, fallback, estados y concurrencia. No depender de Internet para los tests.
- `docs/ARCHITECTURE.md`: política de transferencia y decisiones de arquitectura.

## Estado comprobado

- La aplicación Qt compila en Debian 13 con Qt 6.8.2.
- La inicialización de SQLite tiene una prueba smoke.
- El motor secuencial HTTP/HTTPS tiene pruebas locales para descarga completa, reanudación por `Range`, servidor que ignora `Range`, `Content-Range` incorrecto y respuesta interrumpida.
- Límite de tres descargas simultáneas y avance de cola: pruebas locales aprobadas.
- Motor inicial de descarga segmentada (hasta 8 rangos por archivo): pruebas locales con contenido de 5 MiB y recuperación de fragmentos aprobadas.
- Qt 6 Network corre asíncrono en el bucle de eventos; el ciclo de vida de QThread por descarga fue eliminado para evitar carreras de cierre.
- CTest incluye pruebas de SQLite, Range HTTP, fallback, respuesta incorrecta, truncamiento, cola, reconstrucción y recuperación de segmentos.
- Limitación: ensamblado de archivos grandes todavía puede bloquear transitoriamente la interfaz y no existe una build Windows validada.

## Reglas de transferencia y seguridad

- Aceptar solo HTTP/HTTPS en esta fase; rechazar URL inválidas y credenciales incrustadas.
- No sobrescribir destinos existentes.
- Guardar datos incompletos en temporales junto al destino; publicar el destino final solo al completar y verificar tamaños.
- Nunca concatenar una respuesta `200 OK` completa a un temporal de reanudación.
- Validar `206 Partial Content`, `Content-Range`, longitud y validadores del recurso antes de aceptar segmentos.
- Si el servidor no confirma rangos o responde de forma inconsistente, detener los segmentos y hacer fallback secuencial desde cero.
- Reanudar segmentos solo si la URL, tamaño y ETag/Last-Modified del manifiesto coinciden con el recurso actual.
- No seguir redirecciones que reduzcan la seguridad de HTTPS a HTTP.
- Mantener la red no bloqueante. Pendiente: descargar escrituras de reconstrucción grandes a un trabajador de E/S.

## Entorno de desarrollo

- Sandbox Debian 13.
- Qt 6.8.2 instalado.
- CMake y Ninja disponibles.
- La build Windows real se validará en un runner Windows/MSVC; no se presentará una build Linux como ejecutable Windows.

## Repositorio

- Remoto configurado: `https://github.com/ThowiLabs/OIDM.git`.
- Rama de trabajo: `main`.
- El historial inicial existente se conserva; no reinicializar Git ni reescribir commits.

## Hito de integración del navegador (30-oct-2026)

- Primer host nativo `oidm-native-host` implementado en C++20 y enlazado por sockets Qt con la interfaz.
- Extensión Chromium Manifest V3 con envío explícito desde menú contextual.
- Prueba Linux de extremo a extremo: mensaje binario JSON -> host -> aplicación Qt -> HTTP local -> archivo correcto -> SQLite completado.
- Registro Linux para Chrome, Chromium y Edge probado en rutas de prueba aisladas.
- Firefox, instalación Windows y prueba dentro de un navegador real todavía pendientes.

## Parche de integración navegador (31-oct-2026)

- Versión de extensión MV3 0.1.1, ID estable `ciipklijloaknncdmnlnnoimjfcllohi`.
- `OIDM.exe` registra al iniciar su host Native Messaging en HKCU Windows para Chrome y Edge, sin privilegios de administrador.
- Captura automática de descargas Chrome/Edge por `downloads.onCreated`; al aceptar OIDM, la extensión cancela la descarga de Chrome; si falla, la reanuda.
- El icono de la extensión permite prueba `ping` y hay un `CONFIGURAR-INTEGRACION.cmd` para reparación manual.
- Referrer HTTP/HTTPS validado y persistido en SQLite mediante migración de esquema, enviado como encabezado al descargar.
- Integración real Windows bajo Wine probada de extremo a extremo: Native Messaging enqueue, app Qt, descarga a Downloads y propagación de Referer; no se verificó el sitio real MediaFire.
- La extensión anterior era solo de menú contextual y sin registro automático; es imprescindible eliminarla y cargar la nueva incluida en el ZIP.

## Beta OIDM 0.3.0, instalación integrada 31-oct-2026

- UI oscura nativa clásica realizada en Qt Widgets, motor C++20.
- Asistente Qt de integración Chrome/Edge, sin scripts ni IDs manuales; detecta navegador, abre página de extensiones, ruta copiada al portapapeles.
- Instalador real Windows NSIS por usuario, portable ZIP, registro host nativo HKCU y desinstalación conservadora que mantiene datos del usuario.
- Chrome/Edge requieren confirmación explícita al cargar extensión sin publicar en Store. Queda pendiente publicación oficial para UX de instalación aún más simple.
- Pruebas en Wine de instalación, registro, ejecuciones de smoke UI, desinstalación y preservación de datos correctas.
