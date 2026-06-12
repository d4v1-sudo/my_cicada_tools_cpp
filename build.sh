#!/bin/bash

echo "Cleaning the build folder..."
if [ -d "build" ]; then
    rm -rf build
fi

echo "Creating and configuring the project..."
mkdir build
cd build || exit 1

cmake -DCMAKE_BUILD_TYPE=Release ..

echo "Compiling the project..."
cmake --build . --config Release -j 1