set(ASSET_PACKER_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

add_executable(assetpacker ${ASSET_PACKER_SOURCES})
target_link_libraries(assetpacker PRIVATE fmt::fmt lz4_static)

if(MSVC)
    target_compile_options(assetpacker PRIVATE /W4 /WX)
else()
    target_compile_options(assetpacker PRIVATE -Wall -Wextra -pedantic)
endif()

set(ASSET_PACKER_BINARY "$<TARGET_FILE:assetpacker>")
