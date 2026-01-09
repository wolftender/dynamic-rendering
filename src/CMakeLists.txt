set(GAME_SRC_DIR ${CMAKE_CURRENT_LIST_DIR})
set(SHADER_SRC_DIR ${GAME_SRC_DIR}/shaders)

add_library(thirdparty OBJECT ${GAME_SRC_DIR}/thirdparty.cpp)

set(GAME_SOURCE_FILE_LIST
    ${GAME_SRC_DIR}/main.cpp
    ${GAME_SRC_DIR}/util.cpp
    ${GAME_SRC_DIR}/vulkan.cpp
)

add_executable(${PROJECT_NAME} ${GAME_SOURCE_FILE_LIST} ${ASSET_OUTPUTS})

if(WIN32)
    if(MSVC)
        set(SUBSYSTEM_LINKER_OPTIONS 
            /SUBSYSTEM:WINDOWS
            /ENTRY:mainCRTStartup
        )
    else()
        set(SUBSYSTEM_LINKER_OPTIONS "-Wl,/subsystem:console")
    endif()
endif()

set(COMPILER_STRING "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
    target_compile_options(thirdparty PRIVATE /W0)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall 
        -Wextra 
        -Wpedantic 
        -Wno-nullability-extension   # for vulkan memory allocator
        -Wno-missing-designated-field-initializers # useless and makes it harder to code in vulkan
    )
    target_compile_options(thirdparty PRIVATE -w)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE COMPILER_STRING="${COMPILER_STRING}" VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

target_link_options(${PROJECT_NAME} PRIVATE ${SUBSYSTEM_LINKER_OPTIONS})
target_link_libraries(thirdparty volk::volk GPUOpen::VulkanMemoryAllocator)
target_link_libraries(${PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:thirdparty> volk::volk glfw fmt::fmt glm::glm lz4_static GPUOpen::VulkanMemoryAllocator)
target_include_directories(${PROJECT_NAME} PRIVATE ${RESOURCE_EMBED_DIR})

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
endif()
