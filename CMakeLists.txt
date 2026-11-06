cmake_minimum_required(VERSION 3.20)
project(understory VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

option(UNDERSTORY_BUILD_PLUGIN "Build the OBS plugin (needs deps/obs-studio headers + OBS.app)" ON)

file(GLOB CORE_SRC src/core/*.c)
file(GLOB SCENE_SRC src/scenes/*.c)

add_library(us_core STATIC ${CORE_SRC} ${SCENE_SRC})
target_include_directories(us_core PUBLIC src)
set_target_properties(us_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  # C4013: implicit function declaration — an error everywhere else, only a warning in MSVC C
  target_compile_options(us_core PRIVATE /W3 /we4013)
  target_compile_definitions(us_core PUBLIC _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  target_compile_options(us_core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  target_link_libraries(us_core PUBLIC m)
endif()
find_package(Threads REQUIRED)
target_link_libraries(us_core PUBLIC Threads::Threads)

add_executable(us_render tools/us_render.c)
target_link_libraries(us_render PRIVATE us_core)

add_executable(us_tests tests/test_core.c)
target_link_libraries(us_tests PRIVATE us_core)
enable_testing()
add_test(NAME core COMMAND us_tests)

if(UNDERSTORY_BUILD_PLUGIN AND APPLE)
  set(OBS_SRC "${CMAKE_SOURCE_DIR}/deps/obs-studio" CACHE PATH "obs-studio source (headers)")
  set(OBS_APP "/Applications/OBS.app" CACHE PATH "OBS.app to link against")
  if(EXISTS "${OBS_SRC}/libobs/obs-module.h")
    set(GEN_INC "${CMAKE_BINARY_DIR}/generated")
    file(WRITE "${GEN_INC}/obsconfig.h"
      "#pragma once\n#define OBS_DATA_PATH \"../Resources\"\n#define OBS_PLUGIN_PATH \"../PlugIns\"\n#define OBS_PLUGIN_DESTINATION \"PlugIns\"\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n#define OBS_INSTALL_PREFIX \"\"\n")
    add_library(understory MODULE src/obs/plugin.c)
    target_include_directories(understory PRIVATE "${OBS_SRC}/libobs" "${GEN_INC}" "${CMAKE_SOURCE_DIR}/deps/simde" src)
    target_link_libraries(understory PRIVATE us_core "-F${OBS_APP}/Contents/Frameworks" "-framework libobs")
    target_compile_options(understory PRIVATE -Wall -Wno-unused-parameter -Wno-deprecated-declarations)
    set_target_properties(understory PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION plugin
      OUTPUT_NAME understory
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/obs/Info.plist.in"
      MACOSX_BUNDLE_GUI_IDENTIFIER "wtf.omigod.understory"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      BUILD_WITH_INSTALL_RPATH TRUE
      INSTALL_RPATH "@executable_path/../Frameworks")
    # data/ -> Contents/Resources
    file(GLOB_RECURSE DATA_FILES RELATIVE "${CMAKE_SOURCE_DIR}/data" "${CMAKE_SOURCE_DIR}/data/*")
    foreach(f ${DATA_FILES})
      get_filename_component(d "${f}" DIRECTORY)
      target_sources(understory PRIVATE "data/${f}")
      set_source_files_properties("data/${f}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${d}")
    endforeach()
  else()
    message(WARNING "deps/obs-studio not found — skipping OBS plugin. Run tools/fetch-obs-headers.sh")
  endif()
endif()

# Windows: build understory.dll against libobs headers + an import library for OBS's obs.dll.
# OBS_LIB: path to obs.lib (tools/make-obs-lib.ps1 makes one from an installed OBS's obs.dll).
if(UNDERSTORY_BUILD_PLUGIN AND WIN32)
  set(OBS_SRC "${CMAKE_SOURCE_DIR}/deps/obs-studio" CACHE PATH "obs-studio source (headers)")
  set(OBS_LIB "" CACHE FILEPATH "import library for obs.dll")
  if(EXISTS "${OBS_SRC}/libobs/obs-module.h" AND EXISTS "${OBS_LIB}")
    set(GEN_INC "${CMAKE_BINARY_DIR}/generated")
    file(WRITE "${GEN_INC}/obsconfig.h"
      "#pragma once\n#define OBS_DATA_PATH \"../../data\"\n#define OBS_PLUGIN_PATH \"../../obs-plugins/64bit\"\n#define OBS_PLUGIN_DESTINATION \"obs-plugins/64bit\"\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n#define OBS_INSTALL_PREFIX \"\"\n")
    add_library(understory MODULE src/obs/plugin.c)
    target_include_directories(understory PRIVATE "${OBS_SRC}/libobs" "${GEN_INC}" "${CMAKE_SOURCE_DIR}/deps/simde" src)
    target_link_libraries(understory PRIVATE us_core "${OBS_LIB}")
    target_compile_definitions(understory PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(understory PRIVATE /W3 /we4013)
    set_target_properties(understory PROPERTIES OUTPUT_NAME understory PREFIX "")
    # staged layout for C:/ProgramData/obs-studio/plugins/understory/{bin/64bit,data}
    set(STAGE "${CMAKE_BINARY_DIR}/stage/understory")
    add_custom_command(TARGET understory POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE}/bin/64bit"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:understory>" "${STAGE}/bin/64bit/"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${STAGE}/data")
  else()
    message(WARNING "Windows plugin needs deps/obs-studio (tools/fetch-deps.sh) and -DOBS_LIB=<obs.lib> — skipping plugin")
  endif()
endif()

# headless OBS load/render test: loads the plugin into the real libobs with a throwaway config
if(TARGET understory)
  add_executable(obs_host_test tools/obs_host_test.c)
  target_include_directories(obs_host_test PRIVATE "${OBS_SRC}/libobs" "${GEN_INC}" "${CMAKE_SOURCE_DIR}/deps/simde")
  if(APPLE)
    target_link_libraries(obs_host_test PRIVATE "-F${OBS_APP}/Contents/Frameworks" "-framework libobs" "-Wl,-rpath,${OBS_APP}/Contents/Frameworks")
    target_compile_options(obs_host_test PRIVATE -Wno-deprecated-declarations)
  else()
    target_link_libraries(obs_host_test PRIVATE "${OBS_LIB}")
    target_compile_definitions(obs_host_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  add_dependencies(obs_host_test understory)
endif()
