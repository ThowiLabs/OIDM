# Tarea 02 — Extensiones y puente nativo

Estado: en proceso

## Objetivo
Crear extensiones WebExtensions para Chromium y Firefox y un Native Messaging Host multiplataforma en C++.

## Alcance previsto
- Captura de descargas desde el navegador.
- Envío explícito de URL y metadatos necesarios.
- Registro/desregistro del host nativo.
- Permisos mínimos y mensajes JSON validados.
- Evitar registrar cookies o credenciales en logs.

## Avances comprobados

- Host de Native Messaging `oidm-native-host` escrito en C++ y compilado con Qt 6.
- Registro por usuario para Chrome/Chromium/Edge (Linux) verificado con configuración de prueba aislada.
- Puente `QLocalServer` al escritorio, con acceso restringido al usuario.
- Solicitud `enqueue` de extremo a extremo: respuesta con ID, archivo correcto y estado `completed` en SQLite.
- Extensión Manifest V3 con menú contextual para enlaces, imágenes, audio y vídeo, sin permisos de cookies ni intercepción pasiva.

## Pendiente de validación y desarrollo

- Pruebas manuales de la extensión dentro de Chrome y Edge reales.
- Compilación Windows de host Qt y verificación de claves de registro HKCU.
- Instalación/actualización y desregistro del host.
- Extensión Firefox con ID y registro Native Messaging específicos.
- Captura automática opcional de descargas y detección de multimedia (sin eludir DRM).

## Actualización 2026-10-31 — captura automática preliminar

- La extensión Chromium ahora intercepta `chrome.downloads.onCreated` para HTTP/HTTPS, pausa la transferencia, pide confirmación al host nativo y cancela la descarga del navegador solo después de la respuesta exitosa. Si el host falla, reanuda la descarga original.
- La extensión tiene ID estable y el ejecutable Qt registra automáticamente el host por usuario para Chrome/Edge en Windows. También se incluye `CONFIGURAR-INTEGRACION.cmd`.
- El icono de la extensión ejecuta `ping` al host para diagnosticar conexión.
- Se conserva el `Referer` informado por el navegador al guardar y solicitar enlaces HTTP/HTTPS, útil para algunos enlaces públicos de MediaFire.
- Las pruebas automatizadas de extensión y el recorrido host → OIDM → HTTP se validaron localmente; falta validación en Chrome/Edge real contra MediaFire y demás proveedores.
- **No considerar completada la tarea de navegadores** hasta verificar la instalación real y el manejo de enlaces autenticados.

## Corrección de integración de host (31-oct-2026)

- La beta 0.1.2 agrega interfaz **Ayuda → Reparar integración Chrome/Edge...** para introducir el ID real de extensión, además de `--diagnose-chromium <id>` que valida manifiesto, claves HKCU, host y origen permitido.
- Registrador Chrome/Edge ahora conserva IDs personalizados previamente registrados (si la ruta de host no cambia) y valida ambas claves, sin exigir admin.
- El script CMD portable solicita opcionalmente el ID real y ejecuta registro + diagnóstico. Pruebas Windows bajo Wine confirmaron persistencia de ID personalizado tras volver a registrar el ID estándar.
- Falta confirmación en una instancia real del navegador del usuario y enlace real de MediaFire.

## Captura visible y aplicación única (31-oct-2026)

- Descarga aceptada por el host nativo: guardada en SQLite, visible inmediatamente bajo Todas las descargas, seleccionada, con detalle de transferencia abierto; la aplicación se trae al frente cuando lo permite el SO.
- Estado de error de transferencia conservado en la tabla. Verificación automática `oidm --ui-self-test` valida persistencia, selección y diálogo.
- Si hay dos instancias, la segunda no permanece abierta con lista vacía: intenta activar la instancia dueña del socket y muestra advertencia de versiones anteriores.

## Asistente e instalador de usuario (31-oct-2026)

- Beta 0.3.0: asistente Qt `BrowserSetupDialog` con registro de host nativo, diagnóstico, detección de Chrome/Edge, navegación a `chrome://extensions`/`edge://extensions`, apertura de carpeta y copia automática de ruta. Se ofrece en el primer inicio o en Ayuda.
- Instalador NSIS de usuario (no administrador) en `%LOCALAPPDATA%/Programs/OIDM`: coloca aplicación, extensión y dependencias Qt; registra host, crea accesos directos y ejecuta asistente al terminar. También se mantiene ZIP portable.
- Desinstalación validada en Wine: eliminó archivos y registro HKCU del host instalado, manteniendo datos/historial. Rehúsa borrar registros ajenos de otras instalaciones.
- Chrome/Edge impiden la instalación oculta de extensiones locales en Windows. La activación manual *Cargar descomprimida* es requerida hasta publicación y aprobación en Chrome Web Store / Edge Add-ons; no se fuerza política empresarial ni se escribe en el perfil del navegador.
- Validación: pruebas Qt y compilación cruzada Windows satisfactorias; instalador y desinstalador probados en Wine, incluida verificación de archivos, registro y prueba de interfaz. Falta validación interactiva con Chrome real en Windows.

## Corrección de duplicados (31-oct-2026, beta 0.4.0)

- `downloads.onCreated` pausa inmediatamente, sin consultas previas a preferencias. `enqueue(browserOwned=true)` reserva un registro OIDM sin iniciarlo. Solo al confirmar `chrome.downloads.cancel()` y borrar su entrada la extensión envía `released(id)` y se abre ventana Iniciar/Cancelar. En caso de fallo se usa `abandon(id)` y Chrome reanuda.
- La ventana previa ofrece archivo/URL/tamaño anunciado/Tipo/carpeta, botones Iniciar y Cancelar; HEAD asíncrona para metadatos cuando se desconoce el tamaño.
- Menú Opciones con General, Conexiones (6 por defecto, 1..32), Descargas (carpeta predeterminada) y Navegadores.
- Limitaciones documentadas: Chrome ya comenzó a transferir cuando `onCreated` se dispara; archivo demasiado pequeño o transferencia no pausada se deja al navegador, sin crear duplicados.

## Intercepción pausada (31-oct-2026)

- Control **Pausar capturas** en OIDM y alternancia **OFF/ON** desde el icono de la extensión. Estado persistente compartido en QSettings; el host nativo lo lee/escribe sin lanzar la app.
- La extensión pausa provisionalmente la descarga y consulta el estado: cuando OIDM está pausado o no hay host accesible, reanuda la transferencia de Chrome; al cambiar durante la operación el receptor rechaza nuevos `enqueue`.
- Descargas existentes en OIDM no se pausan. Tests Node confirman pausa, reanudación, fallo de host y recuperación ante error de cancelación del navegador.

## Corrección CDN sin validadores (31-oct-2026, v0.4.1)

- Se elimina el requisito erróneo ETag / Last-Modified para iniciar seis rangos simultáneos dentro de una sesión. Se conserva el requisito para **reanudar fragmentos guardados** de otra sesión.
- El tamaño mínimo para dividir es 768 KiB; la ventana de progreso indica motivo del fallback.
- Prueba `parallelWithoutEtags`: seis respuestas `206` concurrentes sin validadores; reconstrucción exacta y limpieza.

## Integridad del instalador (31-oct-2026)

- Reproducido y corregido el error NSIS *Installer integrity check has failed*: el instalador con LZMA entregaba CRC interno inconsistente; Zlib solid produce CRC válido.
- CRCCheck force y prueba previa a compartir el instalador, con verificación Python independiente del CRC (la extracción 7z sola era insuficiente).
- Prueba de instalación Windows bajo Wine y `--ui-self-test` exitosa, ejecutable instalado idéntico al compilado.

## Extensión con panel + acciones clásicas (0.5.0)

- Popup MV3 con estado online/offline, pausa de capturas, detección automática, últimos envíos, sin copiar recursos de IDM.
- Comandos para abrir archivo/carpeta, copiar enlace, propiedades y limpiar historial; ordenación por columnas.
- Nuevo estado `awaiting_confirmation` evita que `startQueued()` o el botón Iniciar lancen una reserva de Chrome antes de su cancelación y la confirmación del usuario.
- Pendiente: publicación oficial de extensión para instalación con tienda, compatibilidad Firefox, funciones avanzadas de IDM y verificación con MediaFire real en un Chrome Windows.
