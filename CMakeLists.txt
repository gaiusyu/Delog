cmake_minimum_required(VERSION 3.14)
project(LogCompressor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the PCRE2 library (8-bit version)
find_package(pcre2-8 REQUIRED)

# Define the executable and its source files
add_executable(log_compressor
    src/main.cpp
    src/config.cpp
    src/utils.cpp
    src/json_parser.cpp
    src/pattern_recognizer.cpp
    src/tag_manager.cpp
    src/log_processor.cpp
    src/experiment_logger.cpp
)

# Add the 'include' directory to the include path
target_include_directories(log_compressor PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Link the executable against the PCRE2 library
target_link_libraries(log_compressor PRIVATE pcre2-8)

# Enable position-independent code, good practice for executables
set_target_properties(log_compressor PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Optional: Add optimization flags for release builds
if(CMAKE_BUILD_TYPE MATCHES "Release")
    target_compile_options(log_compressor PRIVATE -O3 -DNDEBUG)
endif()