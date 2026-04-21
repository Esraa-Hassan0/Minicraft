@echo off
setlocal
cd /d "%~dp0"

:: Set path to MinGW
echo [1/4] Setting environment...
set "PATH=D:\mingw64\bin;%PATH%"

:: Create build directory if it doesn't exist
if not exist build mkdir build

:: Configure the project
echo [2/4] Configuring project with CMake...
cmake -G "MinGW Makefiles" -S . -B build
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed!
    pause
    exit /b %errorlevel%
)

:: Build the project
echo [3/4] Building GAME_APPLICATION...
cmake --build build -j 8
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b %errorlevel%
)

:: Run the game
echo [4/4] Starting Game...
if exist ".\bin\GAME_APPLICATION.exe" (
    start "" ".\bin\GAME_APPLICATION.exe"
) else (
    echo [ERROR] Executable not found in .\bin\GAME_APPLICATION.exe
    pause
    exit /b 1
)

endlocal
