@echo off
REM Build scrcpy-cam on Windows.
REM
REM   build.bat          release build
REM   build.bat debug    debug build
REM
REM Fetches FFmpeg and SDL3 into third_party\ on first run (see tools\fetch_deps.ps1).
setlocal

cd /d "%~dp0"

set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

if not exist "third_party\ffmpeg\lib\avcodec.lib" (
    echo ==^> fetching dependencies
    powershell -NoProfile -ExecutionPolicy Bypass -File "tools\fetch_deps.ps1"
    if errorlevel 1 exit /b 1
)

if not exist "vendor\scrcpy-server" (
    echo [!!] vendor\scrcpy-server is missing.
    echo      It is our modified scrcpy server build; see README.md.
    exit /b 1
)

echo ==^> configuring (%BUILD_TYPE%)
REM SCRCPY_CAM_GENERATOR lets CI pick a toolchain it can actually find.
set "GEN=%SCRCPY_CAM_GENERATOR%"
if "%GEN%"=="" set "GEN=Visual Studio 17 2022"
set "ARCH="
if /I "%GEN%"=="Visual Studio 17 2022" set "ARCH=-A x64"
cmake -S . -B build -G "%GEN%" %ARCH% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1

echo ==^> building
cmake --build build --config %BUILD_TYPE% --parallel
if errorlevel 1 exit /b 1

cmake --build build --config %BUILD_TYPE% --target vendor_server
if errorlevel 1 exit /b 1

echo.
echo Done: build\bin\Release\scrcpy-cam.exe
endlocal