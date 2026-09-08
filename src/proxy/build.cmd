@echo off
setlocal EnableExtensions
pushd "%~dp0"

rem Locate the 64-bit MSVC environment. GitHub's Windows runner and a
rem developer machine may install Visual Studio in different editions.
if defined VCToolsInstallDir goto vc_ready
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do if not defined VCVARS set "VCVARS=%%V"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
    echo ERROR: Visual Studio 2022 C++ x64 tools were not found.
    popd
    exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 (
    echo ERROR: failed to initialize the MSVC environment.
    popd
    exit /b 1
)
:vc_ready

cl /nologo /c /O2 /Oi- /W3 /GS- dinput8.c
if errorlevel 1 (
    echo ERROR: cl compilation failed.
    popd
    exit /b 1
)

rem GNU ld is used because dinput8.def contains DLL forwarders to the
rem original system proxy (dinput8_orig.dll). Set DLSS5_LD and DLSS5_LIB in
rem CI; the paths below cover a normal local MSYS2 installation.
set "LD=%DLSS5_LD%"
set "LIBROOT=%DLSS5_LIB%"
if not defined LD if exist "C:\msys64\ucrt64\bin\ld.exe" set "LD=C:\msys64\ucrt64\bin\ld.exe"
if not defined LIBROOT if exist "C:\msys64\ucrt64\lib" set "LIBROOT=C:\msys64\ucrt64\lib"
if not defined LD if exist "C:\msys64\mingw64\bin\ld.exe" set "LD=C:\msys64\mingw64\bin\ld.exe"
if not defined LIBROOT if exist "C:\msys64\mingw64\lib" set "LIBROOT=C:\msys64\mingw64\lib"
if not defined LD for /f "delims=" %%L in ('where ld.exe 2^>nul') do if not defined LD set "LD=%%L"
if not defined LIBROOT if defined LD for %%L in ("%LD%") do for %%P in ("%%~dpL..") do set "LIBROOT=%%~fP\lib"

if not defined LD (
    echo ERROR: GNU ld was not found. Install MSYS2/UCRT64 binutils.
    popd
    exit /b 1
)
if not defined LIBROOT (
    echo ERROR: the MSYS2 library directory was not found.
    popd
    exit /b 1
)

"%LD%" -shared -o DINPUT8.dll dinput8.obj dinput8.def --entry DllMainCRTStartup --subsystem windows -L"%LIBROOT%" -lkernel32 -luser32 -lgdi32 -lcomctl32
if errorlevel 1 (
    echo ERROR: GNU ld linking failed.
    popd
    exit /b 1
)

echo BUILD OK: %CD%\DINPUT8.dll
popd
exit /b 0
