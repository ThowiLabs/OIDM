# Tarea 01 — Fundación, interfaz y motor de transferencias

Estado: en proceso

## Objetivo

Entregar una aplicación nativa C++20/Qt 6 que permita agregar enlaces HTTP/HTTPS, seleccionar destino, descargar de forma secuencial o paralela según las capacidades reales del servidor, mostrar progreso y pausar, reanudar o cancelar sin corromper archivos.

## Stack obligatorio

- C++20.
- Qt 6 Widgets, Network y SQL.
- SQLite mediante QSQLITE.
- CMake + Ninja.
- No introducir otro runtime o framework de escritorio.

## Alcance

- Modelo de descarga y estados persistentes.
- UI principal, categorías iniciales, tabla, barra de herramientas y diálogo para agregar enlaces.
- Repositorio SQLite y recuperación de transferencias interrumpidas.
- Sondeo HTTP Range `bytes=0-0` para descubrir tamaño, rangos y validadores.
- Planificación de segmentos contiguos y no solapados con límite por descarga.
- Validación estricta de HTTP 206, Content-Range, Content-Length, ETag y Last-Modified.
- Archivos temporales independientes por segmento y manifiesto atómico para reanudación.
- Ensamblado atómico del archivo final, solo después de verificar todos los segmentos.
- Fallback secuencial cuando el servidor no admite rangos o responde de forma inconsistente.
- Pausa, cancelación, recuperación y limpieza segura de temporales.
- Cola con límite global de descargas simultáneas.
- Pruebas locales de red, protocolo, persistencia y concurrencia.
- Build Linux y workflow de build Windows con MSVC/Qt.

## Criterios de aceptación

- CMake configura y compila en Linux sin advertencias relevantes.
- La aplicación abre una ventana Qt real.
- SQLite conserva historial y recupera estados interrumpidos como pausados.
- Una respuesta 200 se descarga secuencialmente.
- Una respuesta 206 se valida antes de escribir o reanudar un segmento.
- El planificador produce rangos contiguos, completos y sin solapamiento.
- Cada segmento escribe en su propio temporal; nunca se comparten offsets de escritura.
- Un segmento fallido no se ensambla como si estuviera completo.
- Un servidor que ignora Range provoca fallback secuencial desde cero, nunca concatenación incorrecta.
- La reanudación exige que URL, tamaño y validadores persistidos sigan coincidiendo.
- El archivo final no reemplaza un destino existente y solo aparece al completar.
- Pausar conserva temporales válidos; cancelar limpia temporales del trabajo cancelado.
- La cola respeta el límite global de transferencias activas.
- Los tests no necesitan Internet y cubren los casos de éxito y fallo.
- No se incorpora código ni recursos propietarios de IDM.

## Estado actual

- La aplicación C++/Qt 6 compila en Debian 13 y arranca en modo offscreen sin fallos.
- Las pruebas CTest pasan: SQLite, HTTP Range, fallback, reanudación, recuperación de fragmentos, límites de cola y reconstrucción paralela byte por byte.
- El motor divide archivos aptos en hasta ocho segmentos HTTP simultáneos, con manifiesto temporal y validación de rangos.
- Los segmentos solo se reutilizan entre sesiones cuando el servidor aporta ETag fuerte o Last-Modified coherentes.
- La arquitectura del gestor pasó a red asíncrona Qt sin QThread por descarga, resolviendo una carrera de cierre.
- La publicación de archivo final no sustituye un destino existente (Linux: renameat2 no-replace con fallback de enlace exclusivo; Windows: MoveFileExW sin reemplazo).
- Se agregaron pruebas de pausa, cancelación, colisión de destino creada a mitad de descarga y limpieza segura de enlaces simbólicos.
- Se corrigió un uso de memoria liberada en el servidor local de pruebas, detectado mediante AddressSanitizer.
- Cinco ejecuciones repetidas de CTest y una ejecución adicional con ASan/UBSan pasaron en Linux.
- Aún faltan pruebas de estrés, rendimiento real frente a servidor remoto, I/O asíncrona para ensamblados grandes y compilación Windows.

## Fuera de alcance

- Extensiones de navegador y Native Messaging.
- FTP.
- HLS/DASH y captura multimedia.
- Grabber, programador avanzado, sincronización de sitios y autenticación avanzada.
- Instalador firmado y actualizador automático.

## Próximos pasos

1. Optimizar reconstrucción de archivos grandes en un trabajador de E/S independiente y ampliar pruebas de estrés/fallos.
2. Validar la extensión Chromium MV3 en navegador real y realizar la primera compilación Windows con Qt 6.
3. Validar paquetes portables de Linux y Windows con plugins Qt y el host nativo.
4. Evaluar más controles de seguridad de rutas temporales y ampliar la matriz de sistemas de archivos.
5. Pulir la interfaz nativa de Qt después de cerrar los puntos de fiabilidad.
