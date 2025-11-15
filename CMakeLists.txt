# How to use:
# 
# - mkdir build
# - cd build
# - cmake .. # generate Makefile
# - make -j8 # build with 8 threads
# - make run # run the simulator with default memory image path
cmake_minimum_required(VERSION 3.10)

# 根据选项设置编译器
option(USE_CLANG "Use Clang/LLVM toolchain" OFF)
if(USE_CLANG)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
    message(STATUS "Using LLVM/Clang toolchain")
else()
    set(CMAKE_C_COMPILER gcc)
    set(CMAKE_CXX_COMPILER g++)
    message(STATUS "Using GNU/GCC toolchain")
endif()

project(rv_simu_mmu)
# Configurable options
option(USE_DEFAULT_IMG "Use default memory image path" ON)
set(EXECUTABLE_NAME "rv_simu_mmu" CACHE STRING "Name of the executable")
set(MEMORY_IMAGE_PATH "../baremetal/memory" CACHE STRING "Path to memory image file")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # generate compile_commands.json for IDE support

# set(CMAKE_CXX_STANDARD 17) # Set C++ standard 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find all source files
file(GLOB_RECURSE BACKEND_SOURCES "back-end/*.cpp")
file(GLOB_RECURSE FRONTEND_SOURCES "front-end/*.cpp") 
file(GLOB_RECURSE DIFF_SOURCES "diff/*.cpp")
file(GLOB_RECURSE MMU_SOURCES "mmu/*.cpp")

# Combine all sources
set(SOURCES 
    ${BACKEND_SOURCES}
    ${FRONTEND_SOURCES}
    ${DIFF_SOURCES}
    ${MMU_SOURCES}
    rv_simu_mmu_v2.cpp
)

# Set include directories
include_directories(
    ./include/
    ./back-end/include/
    ./back-end/EXU/include/
    ./back-end/tools/include/
    ./diff/include/
    ./front-end/
    ./mmu/include/
)

# Create executable
add_executable(${EXECUTABLE_NAME} ${SOURCES})

# Set optimization flags
target_compile_options(${EXECUTABLE_NAME} PRIVATE -O3)

# Add run target (custom target)
add_custom_target(run
    COMMAND ./${EXECUTABLE_NAME} ${MEMORY_IMAGE_PATH}
    DEPENDS ${EXECUTABLE_NAME}
    COMMENT "Running the simulator with memory image: ${MEMORY_IMAGE_PATH}"
)

# Add clean target (custom target that removes build artifacts)
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove a.out
    COMMAND ${CMAKE_COMMAND} -E remove_directory ./baremetal/memory
    COMMAND ${CMAKE_COMMAND} -E remove ./baremetal/test.code
    COMMENT "Cleaning all build artifacts and generated files"
)

# Add debug build target
add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug ${CMAKE_SOURCE_DIR}
    COMMENT "Configuring for debug build"
)

# Add gdb target
add_custom_target(gdb
    COMMAND gdb --args ./${EXECUTABLE_NAME} ${MEMORY_IMAGE_PATH}
    DEPENDS ${EXECUTABLE_NAME}
    COMMENT "Running simulator with gdb debugger using image: ${MEMORY_IMAGE_PATH}"
)
