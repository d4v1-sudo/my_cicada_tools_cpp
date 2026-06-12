@echo off

echo Cleaning the build folder...
if exist build (
rmdir /s /q build
)

echo Creating build directory...
md build
cd build

set /p toolchain="Enter your toolchain (MinGW/MSYS): "
cmake -G "%toolchain% Makefiles" -DCMAKE_BUILD_TYPE=Release ..

echo Compiling the project...
cmake --build . --config Release -j 1