# Build NetAnomalyAnalyzer (Windows, Qt 6 + MinGW installed via aqtinstall)
#
# Usage:  powershell -ExecutionPolicy Bypass -File .\build.ps1
# Assumes already installed:
#   - Python 3.12 + pip install aqtinstall cmake ninja
#   - Qt 6.8.3 in C:\Qt\6.8.3\mingw_64      (aqt install-qt ...)
#   - MinGW 13.1 in C:\Qt\Tools\Tools\mingw1310_64 (aqt install-tool ...)
# Real packet capture requires Npcap SDK: -DNPCAP_SDK_DIR=<sdk path>

$ErrorActionPreference = "Stop"

$PythonScripts = "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts"
$Qt            = "C:\Qt\6.8.3\mingw_64"
$MinGW         = "C:\Qt\Tools\Tools\mingw1310_64\bin"
$Root          = Split-Path -Parent $MyInvocation.MyCommand.Path

# MinGW bin in PATH is mandatory: otherwise cc1plus cannot find its DLLs
$env:PATH = "$MinGW;$PythonScripts;$Qt\bin;$env:PATH"

& "$PythonScripts\cmake.exe" -S $Root -B "$Root\build" -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$PythonScripts\ninja.exe" `
    -DCMAKE_CXX_COMPILER="$MinGW\g++.exe" `
    -DCMAKE_PREFIX_PATH=($Qt -replace '\\', '/') `
    -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& "$PythonScripts\cmake.exe" --build "$Root\build"
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "OK: $Root\build\NetAnomalyAnalyzer.exe (GUI)"
Write-Host "OK: $Root\build\NetAnalyzerService.exe (консоль/служба Windows)"
