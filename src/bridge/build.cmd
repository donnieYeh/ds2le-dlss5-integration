@echo off
setlocal EnableExtensions
pushd "%~dp0"

rem Locate the installed 64-bit MSVC toolchain instead of assuming a VS edition.
if defined VCToolsInstallDir goto vc_ready
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do if not defined VCVARS set "VCVARS=%%V"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
    echo ERROR: Visual Studio C++ x64 tools were not found.
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

set "RESHADEROOT=%DLSS5_RESHADE_INCLUDE%"
if not defined RESHADEROOT set "RESHADEROOT=%~dp0"
if not exist "%RESHADEROOT%\reshade\reshade_events.hpp" (
    echo ERROR: ReShade SDK headers were not found under "%RESHADEROOT%\reshade".
    echo Set DLSS5_RESHADE_INCLUDE to a directory containing the reshade folder.
    popd
    exit /b 1
)

rc /nologo version.rc
if errorlevel 1 (
    echo ERROR: resource compilation failed.
    popd
    exit /b 1
)

cl /nologo /W4 /O2 /MT /EHsc /std:c++17 /I"%RESHADEROOT%" /LD ^
   dlss5-bridge.cpp version.res ^
   /Fe:dlss5-bridge.addon64 ^
   /link /DLL user32.lib advapi32.lib bcrypt.lib
if errorlevel 1 (
    echo ERROR: bridge compilation failed.
    popd
    exit /b 1
)

echo BUILD OK: %CD%\dlss5-bridge.addon64
popd
exit /b 0
