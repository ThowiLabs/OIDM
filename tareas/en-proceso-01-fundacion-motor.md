# Tarea 01 — Fundación, interfaz inicial y motor de descargas

Estado: en proceso

## Objetivo
Crear una aplicación C++20/Qt 6 que pueda añadir una URL HTTP/HTTPS, elegir destino, descargar mediante segmentos cuando el servidor lo permita, mostrar progreso y permitir pausar, reanudar y cancelar.

## Alcance
- Modelo de descarga.
- Planificación de segmentos.
- Solicitudes Range con libcurl.
- Validación de HTTP 206 y Content-Range.
- Escritura temporal por segmento y ensamblado atómico.
- Pausa/reanudación conservando segmentos parciales.
- Persistencia SQLite de metadatos.
- UI principal con categorías, tabla, barra de herramientas y diálogo de nueva descarga.
- Build y pruebas Linux.
- Workflow para compilar y empaquetar Windows.

## Criterios de aceptación
- El proyecto configura y compila con CMake.
- El ejecutable abre una ventana real.
- Se puede añadir una URL HTTP/HTTPS válida.
- Se valida el protocolo y no se permiten esquemas locales como file://.
- El motor usa varias conexiones solo cuando el servidor confirma rangos.
- Si el servidor no soporta rangos, usa descarga secuencial.
- Las partes se escriben en archivos temporales y el destino final solo se publica al completar.
- Pausa y cancelación no dejan un archivo final truncado.
- Las pruebas cubren división de segmentos y parseo de Content-Range.
- No se incorporan recursos ni código propietario de IDM.

## Fuera de alcance de esta tarea
- Extensiones de navegador.
- Captura de vídeo HLS/DASH.
- FTP.
- Programador, colas avanzadas, Grabber y sincronización de sitios.
- Instalador firmado de Windows.
