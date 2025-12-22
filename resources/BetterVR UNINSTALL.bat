@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"
set "CEMU_DIR=%CD%"
set "CEMU_EXE=%CEMU_DIR%\Cemu.exe"
set "PACK_NAME=BreathOfTheWild_BetterVR"
set "LOG=%CEMU_DIR%\BetterVR_uninstall.log"

call :Log "=== BetterVR uninstaller started ==="
call :Log "Working dir: %CEMU_DIR%"

rem Require Cemu.exe next to script (same rule as installer)
if not exist "%CEMU_EXE%" (
  call :Log "ERROR: Cemu.exe not found next to script. Exiting."
  call :Popup "Cemu.exe was not found next to this script. Place this .bat next to Cemu.exe (or launch it with Cemu's folder as the working directory), then run again." "BetterVR Uninstaller"
  exit /b 1
)

rem Decide target(s) using same precedence as installer
set "MODE="
set "TARGET_BASE="

if exist "%CEMU_DIR%\portable\" (
  set "MODE=portable"
  set "TARGET_BASE=%CEMU_DIR%\portable\graphicPacks"
) else if exist "%CEMU_DIR%\settings.xml" (
  set "MODE=settings.xml"
  set "TARGET_BASE=%CEMU_DIR%\graphicPacks"
) else (
  set "MODE=appdata"
  set "TARGET_BASE=%APPDATA%\Cemu\graphicPacks"
)

call :Log "Mode: %MODE%"
call :Log "Primary target base: %TARGET_BASE%"

set "REMOVED=0"
call :RemoveIfExists "%TARGET_BASE%\%PACK_NAME%"

rem Also clean up other known locations if the pack exists there (helps if user moved between portable/non-portable)
call :RemoveIfExists "%CEMU_DIR%\portable\graphicPacks\%PACK_NAME%"
call :RemoveIfExists "%CEMU_DIR%\graphicPacks\%PACK_NAME%"
call :RemoveIfExists "%APPDATA%\Cemu\graphicPacks\%PACK_NAME%"

if "%REMOVED%"=="1" (
  call :Log "INFO: Uninstall complete."
  call :Popup "BreathOfTheWild_BetterVR graphic pack removed." "BetterVR Uninstaller"
  exit /b 0
) else (
  call :Log "WARN: Pack not found in any known graphicPacks location."
  call :Popup "BreathOfTheWild_BetterVR was not found in any known graphicPacks folder." "BetterVR Uninstaller"
  exit /b 2
)


:RemoveIfExists
set "P=%~1"
if exist "%P%\" (
  call :Log "INFO: Removing: %P%"
  rmdir /s /q "%P%" 2>nul
  if exist "%P%\" (
    call :Log "ERROR: Failed to remove: %P%"
  ) else (
    set "REMOVED=1"
    call :Log "INFO: Removed: %P%"
  )
)
exit /b


:Log
>>"%LOG%" echo [%DATE% %TIME%] %~1
exit /b


:Popup
set "MSG=%~1"
set "TTL=%~2"

powershell -NoProfile -Command ^
  "Add-Type -AssemblyName PresentationFramework;[System.Windows.MessageBox]::Show(\"%MSG%\",\"%TTL%\") | Out-Null" 2>nul

if errorlevel 1 (
  mshta "javascript:var sh=new ActiveXObject('WScript.Shell'); sh.Popup('%MSG%',0,'%TTL%',48); close();" >nul 2>&1
)
exit /b
