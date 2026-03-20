@echo off
setlocal

set "ScriptDir=%~dp0"
set "SrcPath=%ScriptDir%CorridorKey.ofx.bundle"
if not exist "%SrcPath%" (
    set "SrcPath=%ScriptDir%dist\CorridorKey.ofx.bundle"
)

set "DstPath=C:\Program Files\Common Files\OFX\Plugins\CorridorKey.ofx.bundle"
set "CacheFile=%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\OFXPluginCacheV2.xml"

echo Installing CorridorKey OFX Plugin...
echo Source: "%SrcPath%"
echo Destination: "%DstPath%"
echo.

if not exist "%SrcPath%" (
    echo ERROR: Plugin bundle not found at "%SrcPath%"
    echo.
    echo Expected one of these locations:
    echo   "%ScriptDir%CorridorKey.ofx.bundle"
    echo   "%ScriptDir%dist\CorridorKey.ofx.bundle"
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

echo Removing old DaVinci Resolve OFX Cache...
if exist "%CacheFile%" del /f /q "%CacheFile%"

echo Removing any old plugin installation...
if exist "%DstPath%" rmdir /s /q "%DstPath%"

echo Copying new plugin bundle to OFX system directory...
xcopy "%SrcPath%" "%DstPath%" /E /I /H /Y /Q

echo Done! You can now start DaVinci Resolve.
pause
