#!/usr/bin/env python3
"""Collect Qt/MinGW runtime dependencies into a Windows portable ZIP.

Expected environment: MinGW-w64 cross builds against official Qt for MinGW.
This script never runs Windows binaries; it validates their PE imports and archives
only the libraries they reference plus explicitly required Qt runtime plugins.
"""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


DLL_RE = re.compile(r"DLL Name:\s*([^\s]+)", re.IGNORECASE)


def imports(path: Path) -> set[str]:
    output = subprocess.check_output(["x86_64-w64-mingw32-objdump", "-p", str(path)], text=True)
    if "file format pei-x86-64" not in output:
        raise ValueError(f"Not a 64-bit Windows PE: {path}")
    return set(DLL_RE.findall(output))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qt", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    qt = args.qt.resolve()
    build = args.build.resolve()
    output = args.output.resolve()
    root = output / "OIDM-Windows-x64"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    repo = Path(__file__).resolve().parents[1]
    shutil.copy2(build / "oidm.exe", root / "OIDM.exe")
    shutil.copy2(build / "oidm-native-host.exe", root / "oidm-native-host.exe")
    shutil.copytree(repo / "extension", root / "extension")
    shutil.copy2(repo / "LICENSE", root / "LICENSE")
    shutil.copy2(repo / "README.md", root / "README.md")
    extension_id = (repo / "extension" / "chromium" / "extension-id.txt").read_text(encoding="ascii").strip()
    (root / "CONFIGURAR-INTEGRACION.cmd").write_text(
        "@echo off\r\n"
        "setlocal EnableExtensions\r\n"
        "cd /d \"%~dp0\"\r\n"
        "echo.\r\n"
        "echo === REPARAR EXTENSION OIDM: CHROME Y EDGE ===\r\n"
        "echo.\r\n"
        "echo Abre chrome://extensions o edge://extensions, activa Modo de desarrollador\r\n"
        "echo y busca el ID de 32 letras de la extension OIDM instalada.\r\n"
        f"echo ID recomendado de la extension nueva: {extension_id}\r\n"
        "echo.\r\n"
        "set \"EXTENSION_ID=\"\r\n"
        "set /p \"EXTENSION_ID=Pega el ID que aparece en Chrome/Edge (Enter usa el recomendado): \"\r\n"
        f"if not defined EXTENSION_ID set \"EXTENSION_ID={extension_id}\"\r\n"
        "echo.\r\n"
        "echo Registrando host nativo...\r\n"
        "oidm-native-host.exe --register-chromium %EXTENSION_ID%\r\n"
        "if errorlevel 1 goto error\r\n"
        "echo.\r\n"
        "echo Verificando registro, manifiesto, ejecutable e ID permitido...\r\n"
        "oidm-native-host.exe --diagnose-chromium %EXTENSION_ID%\r\n"
        "if errorlevel 1 goto error\r\n"
        "echo.\r\n"
        "echo LISTO: Registro de OIDM validado para Chrome y Edge.\r\n"
        "echo Recarga la extension en chrome://extensions o edge://extensions.\r\n"
        "echo Haz clic en su icono para comprobar la conexion con el programa.\r\n"
        "goto end\r\n"
        ":error\r\n"
        "echo.\r\n"
        "echo ERROR: No se pudo completar la integracion.\r\n"
        "echo Ejecuta este archivo con el mismo usuario de Windows con el que abres Chrome.\r\n"
        "echo NO usar Ejecutar como administrador.\r\n"
        ":end\r\n"
        "echo.\r\n"
        "pause\r\n",
        encoding="ascii",
    )
    (root / "qt.conf").write_text("[Paths]\nPlugins=plugins\n", encoding="utf-8")
    (root / "LEEME-PRIMERO.txt").write_text(
        "OIDM para Windows x64 - version de prueba\n\n"
        "1. Extrae el contenido completo del ZIP.\n"
        "2. Ejecuta OIDM.exe directamente. No necesitas instalar Qt.\n"
        "3. Instala la extension nueva de extension/chromium (elimina la anterior).\n"
        "4. Haz clic en su icono: debe mostrar OIDM conectado.\n"
        "5. Pulsa Descargar en MediaFire y observa la biblioteca OIDM.\n"
        "6. Si no conecta, ejecuta CONFIGURAR-INTEGRACION.cmd.\n"
        "7. Para mas detalles visita extension/README.md.\n\n"
        "Esta version preliminar no esta firmada digitalmente.\n"
        "Todavia no incluye todas las funciones de IDM.\n"
        "Si Windows muestra una alerta, revisa el editor y el archivo.\n",
        encoding="utf-8",
    )
    plugin_names = [
        "platforms/qwindows.dll",
        "sqldrivers/qsqlite.dll",
        "tls/qschannelbackend.dll",
        "tls/qcertonlybackend.dll",
        "styles/qmodernwindowsstyle.dll",
        "imageformats/qjpeg.dll",
        "imageformats/qico.dll",
        "imageformats/qgif.dll",
    ]
    for plugin in plugin_names:
        src = qt / "plugins" / plugin
        if not src.is_file():
            raise FileNotFoundError(src)
        dest = root / "plugins" / plugin
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)

    runtime_dirs = [
        Path("/usr/lib/gcc/x86_64-w64-mingw32/14-posix"),
        Path("/usr/x86_64-w64-mingw32/lib"),
        qt / "bin",
    ]
    extras = ("d3dcompiler_47.dll", "opengl32sw.dll")
    for name in extras:
        src = qt / "bin" / name
        if src.exists():
            shutil.copy2(src, root / name)

    queue = list(root.rglob("*.exe")) + list((root / "plugins").rglob("*.dll"))
    checked = set()
    builtin = {
        "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "comctl32.dll", "comdlg32.dll",
        "crypt32.dll", "d3d11.dll", "d3d12.dll", "d3d9.dll", "dwmapi.dll",
        "dxgi.dll", "gdi32.dll", "gdi32full.dll", "imm32.dll", "iphlpapi.dll",
        "kernel32.dll", "msvcrt.dll", "netapi32.dll", "ncrypt.dll", "ntdll.dll",
        "ole32.dll", "oleaut32.dll", "opengl32.dll", "powrprof.dll", "rpcrt4.dll",
        "secur32.dll", "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
        "userenv.dll", "uxtheme.dll", "version.dll", "winmm.dll", "winspool.drv",
        "ws2_32.dll", "wtsapi32.dll", "winhttp.dll", "windowscodecs.dll", "dwrite.dll",
        "mpr.dll", "authz.dll", "urlmon.dll", "propsys.dll", "d2d1.dll", "dcomp.dll",
        "cryptui.dll", "normaliz.dll", "dnsapi.dll", "wintrust.dll", "mfplat.dll", "shcore.dll",
        "avrt.dll", "d3dcompiler_47.dll", "api-ms-win-core-synch-l1-2-0.dll",
    }
    while queue:
        binary = queue.pop()
        if binary in checked:
            continue
        checked.add(binary)
        for name in imports(binary):
            if name.lower().startswith(("api-ms-win-", "ext-ms-win-")) or name.lower() in builtin:
                continue
            dest = root / name
            if dest.is_file():
                continue
            source = next((directory / name for directory in runtime_dirs if (directory / name).is_file()), None)
            if source is None:
                raise FileNotFoundError(f"Unresolved dependency {name} imported by {binary}")
            shutil.copy2(source, dest)
            queue.append(dest)

    extension_manifest = json.loads((repo / "extension" / "chromium" / "manifest.json").read_text(encoding="utf-8"))
    version = extension_manifest["version"]
    zip_path = output / f"OIDM-Windows-x64-v{version}-portable.zip"
    if zip_path.exists():
        zip_path.unlink()
    with ZipFile(zip_path, "w", compression=ZIP_DEFLATED, compresslevel=7) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                archive.write(path, arcname=Path("OIDM-Windows-x64") / path.relative_to(root))
    checksum = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    print(f"PACKAGED: {zip_path}")
    print(f"BYTES: {zip_path.stat().st_size}")
    print(f"SHA256: {checksum}")
    print("BINARIES:", len(checked), "PLUGINS:", len(plugin_names))
    print("CONTENTS:")
    for path in sorted(root.rglob("*.dll")):
        print(" ", path.relative_to(root))


if __name__ == "__main__":
    main()
