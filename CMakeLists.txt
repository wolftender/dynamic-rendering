cmake_minimum_required(VERSION 3.28)
project(dynamic-rendering VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /EHsc /Zi")
    set(CMAKE_CXX_FLAGS_DEBUG "/EHsc /Zi /D_CRTDBG_MAP_ALLOC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
endif()

# libraries
include(FetchContent)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party)

# vulkan sdk
# find_package(Vulkan REQUIRED)
# if (NOT DEFINED Vulkan_FOUND)
#    message(FATAL_ERROR "cannot find vulkan, are you sure vulkan sdk is installed?")
# endif()

FetchContent_Declare(
    lz4
    REQUIRED
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    SOURCE_SUBDIR build/cmake
    GIT_TAG ebb370ca83af193212df4dcbadcc5d87bc0de2f0 # v1.10.0
)

FetchContent_Declare(
    glfw
    REQUIRED
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # 3.4
)

FetchContent_Declare(
    fmt
    REQUIRED
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 40626af88bd7df9a5fb80be7b25ac85b122d6c21 # 11.2.0
)

FetchContent_Declare(
    glm
    REQUIRED
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG a532f5b1cf27d6a3c099437e6959cf7e398a0a67 # 1.0.2
)

FetchContent_Declare(
    vma
    REQUIRED
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG 1d8f600fd424278486eade7ed3e877c99f0846b1 # 3.3.0
)

FetchContent_Declare(
    volk
    REQUIRED
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 4f3bcee79618a9abe79f4c717c50379197c77512 # 1.4.335.0
)

FetchContent_MakeAvailable(volk glfw fmt glm lz4 vma)

add_compile_definitions(
    ENABLE_LOGGING
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
    GLM_FORCE_INTRINSICS 
    VK_NO_PROTOTYPES
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(tools)

set(ASSET_DIR ${CMAKE_SOURCE_DIR}/assets)
set(RESOURCE_DIR ${CMAKE_SOURCE_DIR}/resources)

set_property(GLOBAL PROPERTY PACKER_COMPRESSED_ASSETS "")
set_property(GLOBAL PROPERTY PACKER_UNCOMPRESSED_ASSETS "")

function(add_asset_compressed FILE)
    get_property(list GLOBAL PROPERTY PACKER_COMPRESSED_ASSETS)
    list(APPEND list "${FILE}")
    set_property(GLOBAL PROPERTY PACKER_COMPRESSED_ASSETS "${list}")
endfunction()

function(add_asset_uncompressed FILE)
    get_property(list GLOBAL PROPERTY PACKER_UNCOMPRESSED_ASSETS)
    list(APPEND list "${FILE}")
    set_property(GLOBAL PROPERTY PACKER_UNCOMPRESSED_ASSETS "${list}")
endfunction()

function(add_packed_assets OUTPUT_FILE)
    get_property(C_ASSETS GLOBAL PROPERTY PACKER_COMPRESSED_ASSETS)
    get_property(U_ASSETS GLOBAL PROPERTY PACKER_UNCOMPRESSED_ASSETS)

    set(PACKER_ARGS "")
    foreach(f IN LISTS C_ASSETS)
        list(APPEND PACKER_ARGS "c:${f}")
    endforeach()
    foreach(f IN LISTS U_ASSETS)
        list(APPEND PACKER_ARGS "a:${f}")
    endforeach()

    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND $<TARGET_FILE:assetpacker> pack binary "${OUTPUT_FILE}" ${PACKER_ARGS}
        DEPENDS
            ${ARGS_FILE}
            ${C_ASSETS}
            ${U_ASSETS}
            assetpacker
        COMMENT "packing assets into ${OUTPUT_FILE}"
        VERBATIM
    )

    add_custom_target(packed_assets ALL
        DEPENDS "${OUTPUT_FILE}"
    )

endfunction()

add_subdirectory(src)
