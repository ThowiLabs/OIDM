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
