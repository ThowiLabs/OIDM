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
