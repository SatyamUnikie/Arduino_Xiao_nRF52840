@echo off
setlocal enabledelayedexpansion

set "SOURCE_DIR=%~dp0"
set "SOURCE_DIR=%SOURCE_DIR:~0,-1%"
set "BUILD_DIR=%SOURCE_DIR%\build"
set "ZEPHYR_BASE=C:\ncs\v3.4.0\zephyr"
set "PATH=C:\ncs\toolchains\dcbdc366a1\opt\bin;%PATH%"

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%" || exit /b 1

pushd "%BUILD_DIR%" || exit /b 1
cmake -G Ninja -DBOARD=xiao_ble -S "%SOURCE_DIR%" -B .
if errorlevel 1 goto :fail

cmake --build .
if errorlevel 1 goto :fail

echo.
echo Build complete: %BUILD_DIR%\zephyr\zephyr.uf2
popd
exit /b 0

:fail
set "ERR=%ERRORLEVEL%"
popd
echo.
echo Build failed with errorlevel %ERR%.
exit /b %ERR%
