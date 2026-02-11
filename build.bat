@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo VCVARSALL FAILED
    exit /b 1
)
cd /d C:\Users\Run\CLionProjects\ProcMon
if exist cmake-build-debug rmdir /s /q cmake-build-debug
"C:\Program Files\JetBrains\CLion 2025.1\bin\cmake\win\x64\bin\cmake.exe" -DCMAKE_BUILD_TYPE=Debug -G Ninja "-DCMAKE_MAKE_PROGRAM=C:/Program Files/JetBrains/CLion 2025.1/bin/ninja/win/x64/ninja.exe" -S . -B cmake-build-debug
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
"C:\Program Files\JetBrains\CLion 2025.1\bin\cmake\win\x64\bin\cmake.exe" --build cmake-build-debug
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo === BUILD SUCCESSFUL ===
dir cmake-build-debug\ProcMonDriver\ProcMon.sys cmake-build-debug\ProcMonClient\ProcMonClient.exe
