@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer / vswhere.exe not found.
    echo Please make sure Visual Studio is installed.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo ERROR: Visual Studio C++ tools were not found.
    echo Please install the "Desktop development with C++" workload.
    exit /b 1
)

echo Using Visual Studio:
echo %VS_PATH%

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

if errorlevel 1 (
    echo ERROR: Failed to initialize Visual Studio environment.
    exit /b %errorlevel%
)

echo.
echo Building sender.exe...

cl /nologo /std:c++17 /EHsc /Zi main.cpp /Fe:sender.exe ws2_32.lib

if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b %errorlevel%
)

echo.
echo BUILD SUCCESSFUL: sender.exe

endlocal