# OIDM — Integración Chrome y Edge, versión 0.1.1

## Instalación portable para Windows

1. Extrae **todo** el archivo ZIP de OIDM a una carpeta. No ejecutes desde dentro del ZIP.
2. Abre `OIDM.exe`. Desde esta versión el programa registra automáticamente en el usuario actual su host nativo para Chrome y Edge.
3. Abre `chrome://extensions` (Chrome) o `edge://extensions` (Edge), activa **Modo de desarrollador** y selecciona **Cargar descomprimida**.
4. Selecciona **la carpeta `extension/chromium` del nuevo ZIP**. Si había una versión anterior de OIDM, elimínala primero y vuelve a cargar la nueva extensión.
5. Haz clic en el icono de OIDM en la barra de extensiones: debe mostrar **OIDM conectado** y la insignia `OK`.
6. En MediaFire, abre una URL compartida y pulsa el botón de descargar normalmente. Cuando Chrome inicia una transferencia real, la extensión la pausa y se la entrega a OIDM. Si el programa no responde, Chrome la reanuda.

### Si no conecta

- Cierra las instancias antiguas de OIDM y vuelve a abrir `OIDM.exe` desde la nueva carpeta.
- Ejecuta `CONFIGURAR-INTEGRACION.cmd` dentro de la carpeta portable (no requiere Administrador).
- En `chrome://extensions`, asegúrate de que solo tienes la nueva extensión OIDM. Pulsa su botón de **Recargar**.
- Haz clic en su icono. Si aparece `!`, abre la notificación y revisa si el host nativo no está registrado.

ID fijo de la extensión: `ciipklijloaknncdmnlnnoimjfcllohi`.

### Comportamiento y limitaciones

La extensión intercepta automáticamente descargas HTTP/HTTPS iniciadas en el navegador (incluido MediaFire) cuando pueden pausarse. También ofrece **Descargar con OIDM** en el menú contextual. Nunca transmite contraseñas, cookies ni cabeceras de autenticación; algunos archivos privados o protegidos por sesión podrían fallar, en cuyo caso debe usarse la descarga normal del navegador. Los enlaces de página compartida de MediaFire son páginas HTML: lo que OIDM debe descargar es la URL de archivo que se genera al pulsar su botón de descarga. Los contenidos DRM no se interceptan ni se descifran.

No es necesario instalar Qt ni ejecutar el host de forma permanente. Puede deshabilitarse la interceptación automática cambiando `automaticInterception` a `false` en el almacenamiento local de la extensión.
