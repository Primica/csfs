cmake_minimum_required(VERSION 3.10)
project(csfs C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

# Include directories
include_directories(include)

# list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/raylib")

# find_package(raylib 5.0 REQUIRED CONFIG)

# Recursively find all source files
file(GLOB_RECURSE SOURCES 
    "src/*.c"
    "src/*/*.c"
)

# Create executable
add_executable(csfs ${SOURCES})

# Output directory for build artifacts
set_target_properties(csfs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# # Link raylib library
# target_link_libraries(csfs raylib)
