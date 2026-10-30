# Fecha
2026-10-30

# Objetivo
Crear OIDM, un gestor de descargas de código abierto para Windows y Linux, con interfaz de escritorio y motor propio de transferencias segmentadas. Es un producto independiente; no está afiliado a Tonec ni reutiliza código, recursos gráficos o binarios propietarios de Internet Download Manager.

# Decisiones tomadas
- Lenguaje principal: C++20.
- Interfaz: Qt 6 Widgets.
- Sistema de build: CMake + Ninja.
- HTTP/HTTPS: libcurl.
- Persistencia: SQLite.
- Concurrencia: std::thread / atomics y límites explícitos.
- Primera entrega: aplicación de escritorio funcional con alta de URL, lista de descargas, pausa/reanudación/cancelación y descarga segmentada cuando el servidor admite HTTP Range.
- Extensiones de navegador se desarrollarán como componente separado en una fase posterior.
- La compilación Windows se hará en runner Windows con MSVC 2022 y Qt; no se simulará un .exe con una build Linux.

# Arquitectura actual
- src/core: modelo de descarga, motor de transferencias y persistencia SQLite.
- src/ui: ventana principal y diálogos Qt.
- tests: pruebas unitarias del planificador de segmentos y parser Content-Range.
- packaging/windows: scripts de empaquetado.
- .github/workflows: compilación automatizada Windows/Linux.

# Librerías usadas
- Qt 6 Widgets.
- libcurl.
- SQLite3.
- C++20 standard library.

# Archivos importantes modificados
- Proyecto recién inicializado; ver CMakeLists.txt y carpetas src/.

# Problemas encontrados
- El sandbox REA es Debian 13 y inicialmente no tenía CMake, Qt ni cabeceras de libcurl.
- Se instalaron CMake 3.31.6, Ninja 1.12.1, Qt 6.8.2, libcurl 8.14.1 y SQLite 3.46.1 desde repositorios Debian.
- La compilación Windows nativa no está disponible en este contenedor Linux; se utilizará GitHub Actions en Windows para generar el paquete real.

# Soluciones implementadas
- Toolchain Linux instalado.
- Repositorio Git local y rama main inicializados.
- Repositorio público ThowiLabs/OIDM creado para alojar el proyecto y ejecutar CI de Windows.

# Pendientes
- Implementar el motor de descargas y persistencia.
- Implementar la interfaz inicial.
- Añadir pruebas de integración de descarga real.
- Ejecutar build Linux y build Windows en CI.
- Añadir extensión WebExtensions y host Native Messaging.
- Completar funciones avanzadas de IDM de forma independiente.

# Próximos pasos
1. Implementar descarga HTTP/HTTPS segmentada con verificación de respuestas 206/Content-Range.
2. Añadir persistencia y estados recuperables.
3. Crear UI de escritorio.
4. Compilar y probar Linux.
5. Publicar build Windows de prueba mediante GitHub Actions.
