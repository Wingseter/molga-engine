# Suggested Commands

## Build
The project uses CMake. A typical build flow:

```bash
mkdir -p build
cd build
cmake ..
make
```

## Run Editor
```bash
# From project root
./build/molga_engine
```

## Run Runtime
```bash
./build/molga_runtime
```

## Test Build System
To run the build system test (GameBuilder):
```bash
clang++ test_build.cpp src/Core/GameBuilder.cpp ... -o test_build
# (Note: manual compilation might be complex due to dependencies, prefer CMake target if available or add one)
```
*Note: Currently `test_build.cpp` is not explicitly in a CMake target in the snippets seen, but likely can be added or is used for reference.*
