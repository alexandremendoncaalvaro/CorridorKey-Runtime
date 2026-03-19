@echo off
set "SrcPath=C:\Dev\CorridorKey-Runtime\dist\CorridorKey.ofx"
set "DstPath=C:\Program Files\Common Files\OFX\Plugins\CorridorKey.ofx.bundle"
set "CacheFile=%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\OFXPluginCacheV2.xml"

echo Validating bundle before installation...
if not exist "%SrcPath%\Contents\Win64\onnxruntime.dll" (
    echo.
    echo ERROR: onnxruntime.dll not found in bundle!
    echo The bundle may not have been packaged correctly.
    echo.
    echo Please run the packaging script first:
    echo   powershell -ExecutionPolicy Bypass -File scripts\package_ofx.ps1
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
