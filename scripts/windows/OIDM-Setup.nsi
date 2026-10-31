; OIDM official per-user installer. Never modifies Chrome enterprise policy,
; extension preferences, or user download data. Chrome asks users to enable
; unpacked extensions. Requires: makensis /DSOURCE_DIR=<portable-dir> ...
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef SOURCE_DIR
  !error "Pass /DSOURCE_DIR=path/to/portable/folder"
!endif

!ifndef APP_VERSION
  !define APP_VERSION "0.3.0"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "OIDM-Windows-x64-v${APP_VERSION}-Setup.exe"
!endif
Name "OIDM - Open Internet Download Manager"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\OIDM"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
BrandingText "ThowiLabs | Open Internet Download Manager"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\OIDM.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--setup-browser"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir OIDM y conectar Chrome / Edge"
!define MUI_FINISHPAGE_RUN_CHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Spanish"

Section "OIDM" SecMain
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"
  WriteUninstaller "$INSTDIR\Desinstalar OIDM.exe"
  CreateDirectory "$SMPROGRAMS\OIDM"
  CreateShortcut "$SMPROGRAMS\OIDM\OIDM.lnk" "$INSTDIR\OIDM.exe"
  CreateShortcut "$SMPROGRAMS\OIDM\Configurar Chrome y Edge.lnk" "$INSTDIR\OIDM.exe" "--setup-browser"
  CreateShortcut "$SMPROGRAMS\OIDM\Desinstalar OIDM.lnk" "$INSTDIR\Desinstalar OIDM.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThowiLabs.OIDM" "DisplayName" "OIDM - Open Internet Download Manager"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThowiLabs.OIDM" "Publisher" "ThowiLabs"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThowiLabs.OIDM" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThowiLabs.OIDM" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThowiLabs.OIDM" "UninstallString" '$\"$INSTDIR\Desinstalar OIDM.exe$\"'
  ExecWait '$\"$INSTDIR\oidm-native-host.exe$\" --register-chromium ciipklijloaknncdmnlnnoimjfcllohi' $0
  ${If} $0 != 0
    DetailPrint "La integración se configurará desde el asistente de OIDM. Código: $0"
  ${EndIf}
SectionEnd

Section "Uninstall"
  ExecWait '$\"$INSTDIR\oidm-native-host.exe$\" --unregister-chromium' $0
  DetailPrint "Desregistro del host: $0"
  Delete "$INSTDIR\Desinstalar OIDM.exe"
  Delete "$INSTDIR\OIDM.exe"
  Delete "$INSTDIR\oidm-native-host.exe"
  Delete "$INSTDIR\qt.conf"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\LEEME-PRIMERO.txt"
  Delete "$INSTDIR\CONFIGURAR-INTEGRACION.cmd"
  Delete "$INSTDIR\*.dll"
  RMDir /r "$INSTDIR\plugins"
  RMDir /r "$INSTDIR\extension"
  RMDir "$INSTDIR"
  RMDir /r "$SMPROGRAMS\OIDM"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThowiLabs.OIDM"
  DetailPrint "Los datos y el historial de descargas del usuario se conservan."
SectionEnd
