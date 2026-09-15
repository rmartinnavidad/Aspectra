@echo off
setlocal
cd /d "%~dp0"
echo BUILD_STARTED > build_status.txt
echo === CMake configure === > build_log.txt
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" >> build_log.txt 2>&1
if errorlevel 1 (
  echo CONFIGURE_FAILED >> build_status.txt
  goto :done
)
echo === CMake build === >> build_log.txt
cmake --build build --config Release --parallel >> build_log.txt 2>&1
if errorlevel 1 (
  echo BUILD_FAILED >> build_status.txt
  goto :done
)
echo === windeployqt === >> build_log.txt
"C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe" --release build\Release\Aspectra.exe >> build_log.txt 2>&1
echo BUILD_SUCCEEDED >> build_status.txt
:done
echo === finished, status file written === >> build_log.txt
