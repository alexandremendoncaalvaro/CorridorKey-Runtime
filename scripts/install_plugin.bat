@echo off
setlocal

set "ScriptDir=%~dp0"
set "RepoDir=%~dp0..\"

:: Check if running from inside the 'scripts' folder (development) or a release folder
set "SrcPath=%RepoDir%CorridorKey.ofx.bundle"
set "DistPath=%RepoDir%dist\CorridorKey.ofx.bundle"
set "PackageScript=%ScriptDir%package_ofx.ps1"

if not exist "%PackageScript%" (
    :: Fallback: Running from a release folder where install_plugin.bat is at the root
    set "SrcPath=%ScriptDir%CorridorKey.ofx.bundle"
    set "DistPath=%ScriptDir%dist\CorridorKey.ofx.bundle"
    set "PackageScript=%ScriptDir%scripts\package_ofx.ps1"
)

set "DstPath=C:\Program Files\Common Files\OFX\Plugins\CorridorKey.ofx.bundle"
set "CacheFile=%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\OFXPluginCacheV2.xml"
set "LogsDir=%LOCALAPPDATA%\CorridorKey\Logs"

echo Installing CorridorKey OFX Plugin...
echo Packaging plugin bundle...
if exist "%PackageScript%" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%PackageScript%"
    if errorlevel 1 (
        echo.
        echo ERROR: Packaging failed.
        echo.
        pause
        exit /b 1
    )
) else (
    echo Packaging script not found. Using prebuilt bundle in this folder.
)

if exist "%DistPath%" (
    set "SrcPath=%DistPath%"
)

echo Source: "%SrcPath%"
echo Destination: "%DstPath%"
echo.

if not exist "%SrcPath%" (
    echo ERROR: Plugin bundle not found at "%SrcPath%"
    echo.
    echo Expected one of these locations:
    echo   "%SrcPath%"
    echo   "%DistPath%"
    echo.
    echo If you extracted a release ZIP, run install_plugin.bat from inside the extracted release folder.
    echo.
    pause
    exit /b 1
)

echo Validating bundle...
if not exist "%SrcPath%\Contents\Win64\onnxruntime.dll" (
    echo ERROR: onnxruntime.dll not found in bundle!
    echo.
    pause
    exit /b 1
)

echo Bundle validation passed.
echo.

echo Checking if DaVinci Resolve is running...
tasklist /fi "imagename eq Resolve.exe" | find /i "Resolve.exe" > NUL
if not errorlevel 1 (
    echo Closing DaVinci Resolve forcefully to release file locks...
    taskkill /f /im Resolve.exe > NUL 2>NUL
    timeout /t 2 /nobreak > NUL
)
echo.

echo Removing old DaVinci Resolve OFX Cache...
if exist "%CacheFile%" del /f /q "%CacheFile%"

echo Removing old CorridorKey logs...
if exist "%LogsDir%" rmdir /s /q "%LogsDir%"

echo Removing any old plugin installation...
if exist "%DstPath%" rmdir /s /q "%DstPath%"

echo Copying new plugin bundle to OFX system directory...
xcopy "%SrcPath%" "%DstPath%" /E /I /H /Y /Q

echo Starting DaVinci Resolve...
if exist "C:\Program Files\Blackmagic Design\DaVinci Resolve\Resolve.exe" (
    start "" "C:\Program Files\Blackmagic Design\DaVinci Resolve\Resolve.exe"
)

echo Done!
pause
