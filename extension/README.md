# OIDM — integración con Chrome / Edge

**Recomendado en Windows:** instala `OIDM-Windows-x64-v0.4.0-Setup.exe`. El instalador coloca los archivos de la extensión, registra el host nativo y abre un asistente que te lleva a la página de extensiones del navegador. Chrome/Edge exige una autorización final para cargar la extensión local. No hay que copiar IDs ni ejecutar comandos.


## Método portable alternativo para Windows

1. Extrae **todo** el archivo ZIP de OIDM a una carpeta. No ejecutes desde dentro del ZIP.
2. Abre `OIDM.exe`. Desde esta versión el programa registra automáticamente en el usuario actual su host nativo para Chrome y Edge.
3. Abre `chrome://extensions` (Chrome) o `edge://extensions` (Edge), activa **Modo de desarrollador** y selecciona **Cargar descomprimida**.
4. Selecciona **la carpeta `extension/chromium` del nuevo ZIP**. Si había una versión anterior de OIDM, elimínala primero y vuelve a cargar la nueva extensión.
5. Haz clic en el icono de OIDM en la barra de extensiones: debe mostrar **OIDM conectado** y la insignia `OK`.
6. En MediaFire, abre una URL compartida y pulsa el botón de descargar normalmente. Cuando Chrome inicia una transferencia real, la extensión la pausa y se la entrega a OIDM. Si el programa no responde, Chrome la reanuda.

### Si no conecta

- Cierra las instancias antiguas de OIDM y vuelve a abrir `OIDM.exe` desde la nueva carpeta.
- En OIDM elige **Ayuda → Reparar integración Chrome / Edge**, pega exactamente el ID mostrado por tu navegador y confirma. O ejecuta `CONFIGURAR-INTEGRACION.cmd` dentro de la carpeta portable, donde verás el registro y diagnóstico. No requiere ni debes usar Administrador: el registro es por usuario.
- En `chrome://extensions`, asegúrate de que solo tienes la nueva extensión OIDM. Pulsa su botón de **Recargar**.
- Haz clic en su icono. Si aparece `!`, abre la notificación y revisa si el host nativo no está registrado. **"Specified native messaging host not found"** o **"No native messaging host"** indica registro o ID faltante, no un error de MediaFire.

ID fijo de la extensión: `ciipklijloaknncdmnlnnoimjfcllohi`.

### Comportamiento y limitaciones

La extensión intercepta automáticamente descargas HTTP/HTTPS iniciadas en el navegador (incluido MediaFire) cuando pueden pausarse. También ofrece **Descargar con OIDM** en el menú contextual. Nunca transmite contraseñas, cookies ni cabeceras de autenticación; algunos archivos privados o protegidos por sesión podrían fallar, en cuyo caso debe usarse la descarga normal del navegador. Los enlaces de página compartida de MediaFire son páginas HTML: lo que OIDM debe descargar es la URL de archivo que se genera al pulsar su botón de descarga. Los contenidos DRM no se interceptan ni se descifran.

No es necesario instalar Qt ni ejecutar el host de forma permanente. Puede deshabilitarse la interceptación automática cambiando `automaticInterception` a `false` en el almacenamiento local de la extensión.

## Instalación mediante asistente (Windows, 0.3.0)

Descarga y ejecuta `OIDM-Windows-x64-v0.4.0-Setup.exe` con tu cuenta normal de Windows (no requiere Administrador). El instalador coloca OIDM y su extensión en una ruta estable, configura los accesos de inicio y registra el host nativo. En la última pantalla deja seleccionada la opción **Abrir OIDM y conectar Chrome / Edge**.

En el asistente de OIDM pulsa **Preparar y comprobar conexión**, después **Abrir Chrome** o **Abrir Edge**, habilita el modo de desarrollador y usa **Cargar descomprimida** para seleccionar la carpeta de extensión que OIDM abre o copia automáticamente al portapapeles. Es el único paso manual requerido por las restricciones de las extensiones no publicadas en Chrome Web Store / Microsoft Edge Add-ons. Al hacer clic en el icono de la extensión instalada debería responder **OIDM conectado**.

Si necesitas abrir de nuevo el asistente: **Ayuda → Instalar / conectar extensión Chrome y Edge**. Para instalar con un único clic desde la tienda, primero habría que publicar allí la extensión, algo que requiere una cuenta de desarrollador y aprobación de la tienda.

## Pausar temporalmente las capturas

**En OIDM:** pulsa **Pausar capturas** en la barra o en Herramientas. Activado significa que Chrome/Edge descarga sus archivos por sí mismo. Desmárcalo para volver a interceptar.

**En Chrome/Edge:** haz clic en el icono de la extensión: la insignia **OFF** indica capturas pausadas y **ON** indica capturas activas. Al cambiarlo desde la extensión, OIDM leerá el mismo estado guardado. Puedes cerrar OIDM; la pausa se mantiene al volver a abrirlo. Las descargas de OIDM iniciadas antes de pulsar OFF no se detienen.

Si Chrome no logra comunicarse con el host nativo, la transferencia no se interrumpe y se mantiene en Chrome.
