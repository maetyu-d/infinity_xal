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
set(story_library "${CMAKE_CURRENT_SOURCE_DIR}/data/story-library.tsv")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${story_library}")
file(READ "${story_library}" story_library_text)
set(story_expansion "${CMAKE_CURRENT_SOURCE_DIR}/data/story-expansion.tsv")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${story_expansion}")
file(READ "${story_expansion}" story_expansion_text)
string(APPEND story_library_text "\n${story_expansion_text}")
string(REPLACE "\n" ")XALARIA_STORIES\",\nR\"XALARIA_STORIES(" story_library_rows "${story_library_text}")
set(story_header "#pragma once\n#include <string_view>\ninline constexpr std::string_view xalariaStoryLibrary[] = {\nR\"XALARIA_STORIES(${story_library_rows})XALARIA_STORIES\"};\n")
set(story_header_path "${CMAKE_BINARY_DIR}/generated/XalariaStoryData.hpp")
file(WRITE "${story_header_path}" "${story_header}")
add_library(hybrid_objects OBJECT src/Universe.cpp src/Geometry.cpp src/ActivityMeshes.cpp src/Cockpit.cpp src/CockpitContext.cpp src/ShipInterior.cpp src/StationLayout.cpp src/CrewSilhouette.cpp src/AtlasFont.cpp src/Xalaria.cpp src/Flight.cpp src/Piloting.cpp src/Docking.cpp src/Planetology.cpp src/XalariaMap.cpp src/XalariaNavigation.cpp src/XalariaEconomy.cpp src/XalariaTraffic.cpp src/XalariaStories.cpp src/XalariaIncidents.cpp src/XalariaDiscovery.cpp src/XalariaInvestigation.cpp src/XalariaCombat.cpp src/XalariaConvoys.cpp src/WarpTravel.cpp)
target_include_directories(hybrid_objects PUBLIC include "${CMAKE_BINARY_DIR}/generated" "${dep}/glm" "${dep}/FastNoise2/include"
  "${dep}/FastSIMD/include" "${dep}/oneTBB/include" "${dep}/zstd/lib"
  "${INFINITY_ENGINE_BUILD}/external/FastNoise2/src/fastsimd/FastSIMD_FastNoise/include")
target_compile_definitions(hybrid_objects PUBLIC FASTNOISE_STATIC_LIB FASTSIMD_STATIC_LIB)
target_sources(hybrid_objects PRIVATE src/ShipExplosion.cpp src/CockpitMotion.cpp src/XalariaStrategy.cpp src/XalariaFleet.cpp src/XalariaStrategySave.cpp src/XalariaStrategyUI.cpp)
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

add_executable(xalaria_combat_tests tests/XalariaCombatTests.cpp)
target_link_libraries(xalaria_combat_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME piracy_warfare_and_boarding COMMAND xalaria_combat_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.tsv")

add_executable(xalaria_convoy_tests tests/XalariaConvoyTests.cpp)
target_link_libraries(xalaria_convoy_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME universe_convoys_escort_piracy_and_factions COMMAND xalaria_convoy_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.tsv")

add_executable(xalaria_cockpit_motion_tests tests/CockpitMotionTests.cpp)
target_link_libraries(xalaria_cockpit_motion_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME stable_cockpit_feedback COMMAND xalaria_cockpit_motion_tests)

add_executable(xalaria_strategy_tests tests/XalariaStrategyTests.cpp)
target_link_libraries(xalaria_strategy_tests PRIVATE hybrid_objects ${engine_libraries})
add_test(NAME living_universe_supply_fleets_and_consequences COMMAND xalaria_strategy_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/catalog.tsv")
