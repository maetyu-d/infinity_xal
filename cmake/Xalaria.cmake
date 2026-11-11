set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(INFINITY_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Infinite" CACHE PATH "Existing Infinity source tree")
set(INFINITY_ENGINE_BUILD "${INFINITY_ENGINE_ROOT}/build" CACHE PATH "Existing built Infinity engine")
if(NOT APPLE)
  message(FATAL_ERROR "This native build uses the existing macOS Infinity libraries.")
endif()
if(NOT EXISTS "${INFINITY_ENGINE_BUILD}/libinfinity_core.a")
  message(FATAL_ERROR "Build Infinity first, or set INFINITY_ENGINE_ROOT and INFINITY_ENGINE_BUILD.")
endif()
set(dep "${INFINITY_ENGINE_ROOT}/external")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedHyperion.cmake")
add_library(hybrid_objects OBJECT src/Universe.cpp src/Geometry.cpp src/ActivityMeshes.cpp src/Cockpit.cpp src/Xalaria.cpp src/Flight.cpp src/Planetology.cpp src/XalariaMap.cpp src/XalariaNavigation.cpp src/XalariaEconomy.cpp)
target_include_directories(hybrid_objects PUBLIC include "${dep}/glm" "${dep}/FastNoise2/include"
  "${dep}/FastSIMD/include" "${dep}/oneTBB/include" "${dep}/zstd/lib"
  "${INFINITY_ENGINE_BUILD}/external/FastNoise2/src/fastsimd/FastSIMD_FastNoise/include")
target_compile_definitions(hybrid_objects PUBLIC FASTNOISE_STATIC_LIB FASTSIMD_STATIC_LIB)
target_compile_options(hybrid_objects PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
set(engine_libraries
  "${INFINITY_ENGINE_BUILD}/libinfinity_core.a"
  "${INFINITY_ENGINE_BUILD}/external/FastNoise2/src/libFastNoise.a"
  "${INFINITY_ENGINE_BUILD}/appleclang_21.0_cxx20_64_release/libtbb.a"
  "${INFINITY_ENGINE_BUILD}/external/zstd/build/cmake/lib/libzstd.a" sqlite3)
add_executable(infinity_xalaria src/XalariaMain.cpp src/Renderer.cpp src/HyperionRenderer.cpp)
target_link_libraries(infinity_xalaria PRIVATE hybrid_objects ${engine_libraries}
  "${INFINITY_ENGINE_BUILD}/external/SDL/libSDL3.a"
  "${INFINITY_ENGINE_BUILD}/external/bgfx.cmake/cmake/bgfx/libbgfx.a"
  "${INFINITY_ENGINE_BUILD}/external/bgfx.cmake/cmake/bimg/libbimg.a"
  "${INFINITY_ENGINE_BUILD}/external/bgfx.cmake/cmake/bx/libbx.a")
target_include_directories(infinity_xalaria PRIVATE "${CMAKE_BINARY_DIR}/generated" "${dep}/SDL/include"
  "${INFINITY_ENGINE_BUILD}/external/SDL/include-revision" "${dep}/bgfx/include"
  "${dep}/bgfx/examples/common/debugdraw" "${dep}/bx/include" "${INFINITY_ENGINE_BUILD}/generated")
target_compile_definitions(infinity_xalaria PRIVATE BGFX_CONFIG_MULTITHREADED=1 BGFX_CONFIG_DEBUG_ANNOTATION=0
  BGFX_CONFIG_VIDEO=0 BX_CONFIG_DEBUG=0 XALARIA_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
foreach(framework Cocoa QuartzCore CoreMedia CoreVideo UniformTypeIdentifiers IOKit Carbon CoreAudio
    AudioToolbox AVFoundation Foundation Metal CoreFoundation)
  target_link_libraries(infinity_xalaria PRIVATE "-framework ${framework}")
endforeach()
enable_testing()
add_executable(xalaria_tests tests/XalariaTests.cpp)
target_link_libraries(xalaria_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME public_catalogue_map_and_flight COMMAND xalaria_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.tsv")
add_executable(xalaria_navigation_tests tests/XalariaNavigationTests.cpp)
target_link_libraries(xalaria_navigation_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME integrated_map_journey_and_controls COMMAND xalaria_navigation_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.tsv")
add_executable(xalaria_economy_tests tests/XalariaEconomyTests.cpp)
target_link_libraries(xalaria_economy_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME integrated_economy_and_physical_trade COMMAND xalaria_economy_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.tsv")
