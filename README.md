# OIDM

OIDM es un gestor de descargas de código abierto e independiente para Windows y Linux, desarrollado en C++20 y Qt 6.

No está afiliado a Tonec ni a Internet Download Manager. No contiene código, binarios, iconos ni recursos propietarios de otros productos.

## Estado

Alpha 0.1.0 — fundación en desarrollo. La primera versión incorpora la base de escritorio y un motor HTTP/HTTPS segmentado en los servidores que admiten Range. No pretende cubrir todavía toda la funcionalidad de un gestor maduro.

## Tecnologías

- C++20
- Qt 6 Widgets
- libcurl
- SQLite
- CMake

## Compilar en Debian/Ubuntu

    sudo apt update
    sudo apt install -y build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools libcurl4-openssl-dev libsqlite3-dev
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    ./build/OIDM

## Compilar en Windows

La compilación oficial se realiza con GitHub Actions sobre Windows Server, MSVC 2022, Qt 6 y vcpkg. El workflow de .github/workflows/build.yml genera un ZIP portable que incluye el ejecutable, las DLL de Qt y las dependencias de terceros.

Para desarrollo local se requiere Visual Studio 2022 (Desktop development with C++), CMake, Qt 6 Widgets y vcpkg con curl y sqlite3.

## Arquitectura

- src/core: motor de transferencia, modelo y persistencia.
- src/ui: interfaz Qt.
- tests: pruebas unitarias.
- contexto: decisiones y continuidad del proyecto.
- tareas: alcance y estado de implementación.

## Seguridad

- Solo se aceptan URL HTTP y HTTPS en la primera versión.
- Se rechazan esquemas locales y protocolos no habilitados.
- Se limita el número de redirecciones.
- Se restringen los protocolos permitidos también en redirecciones.
- Las descargas se escriben en archivos temporales y se ensamblan al finalizar.
- Los errores no deben registrar credenciales ni contenido descargado.

## Licencia

GPL-3.0-or-later. Consulta LICENSE.
