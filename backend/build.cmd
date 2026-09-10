@echo off

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo Visual Studio installation not found.
    exit /b 1
)

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

if errorlevel 1 exit /b %errorlevel%

cl /nologo /std:c++17 /EHsc /Zi /Iinclude main.cpp /Fe:listener.exe ws2_32.lib bcrypt.lib

exit /b %errorlevel%