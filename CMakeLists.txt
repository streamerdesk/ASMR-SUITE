# Streamerdesk ASMR Suite - compilacion nativa con CMake (alternativa a scripts/build-all.sh)
#
#   cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release [-Dlibobs_DIR=<ruta a libobsConfig.cmake>]
#   cmake --build build-native --config Release
#
# Si no hay paquete CMake de libobs, se puede indicar OBS_INCLUDE_DIR (cabeceras libobs) y
# OBS_LIBRARY (obs.lib / libobs.so / libobs.framework/libobs).
# La compilacion oficial de los paquetes publicados se hace con Zig (scripts/build-all.sh,
# linux/build_linux_suite.py y macos/build_macos_suite.py).
cmake_minimum_required(VERSION 3.16)
project(streamerdesk-asmr-suite VERSION 1.0.2 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_VISIBILITY_PRESET hidden)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

option(SD_BUILD_PINGPONG_ECHO "Eco Ping-Pong" ON)
option(SD_BUILD_EAR_TO_EAR "Susurro de oido a oido" ON)
option(SD_BUILD_REVERB "Reverb" ON)
option(SD_BUILD_8D_ORBIT "8D / Orbita" ON)
option(SD_BUILD_PROXIMITY "Proximidad" ON)
option(SD_BUILD_BINAURAL "Espacialidad binaural" ON)
option(SD_BUILD_TESTS "Pruebas offline del DSP" ON)

find_package(libobs QUIET)
if(NOT TARGET OBS::libobs)
  set(OBS_INCLUDE_DIR "" CACHE PATH "Cabeceras de libobs (carpeta libobs/ del codigo de OBS)")
  set(OBS_LIBRARY "" CACHE FILEPATH "Biblioteca de libobs para enlazar")
  if(NOT OBS_INCLUDE_DIR)
    message(FATAL_ERROR "No se encontro libobs: define libobs_DIR o OBS_INCLUDE_DIR/OBS_LIBRARY")
  endif()
  add_library(OBS::libobs INTERFACE IMPORTED)
  target_include_directories(OBS::libobs INTERFACE "${OBS_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/obsconfig")
  if(OBS_LIBRARY)
    target_link_libraries(OBS::libobs INTERFACE "${OBS_LIBRARY}")
  endif()
endif()

# Biblioteca comun compartida (DSP header-only + presets 1.0.1)
add_library(sd_common INTERFACE)
target_include_directories(sd_common INTERFACE ${PROJECT_SOURCE_DIR}/common ${PROJECT_SOURCE_DIR}/common/dsp ${PROJECT_SOURCE_DIR}/common/obs)

# sd_add_obs_plugin(<id> <version> <src...>) : modulo OBS independiente por efecto.
# Si el directorio del plugin tiene common/obs/sd_presets.c, se usa esa copia vendorizada
# (Proximidad, Espacialidad y Susurro se publicaron con un common anterior).
function(sd_add_obs_plugin id version)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/common/obs/sd_presets.c")
    set(_sd_common_src "${CMAKE_CURRENT_SOURCE_DIR}/common/obs/sd_presets.c")
    set(_sd_common_inc "${CMAKE_CURRENT_SOURCE_DIR}/common"
                       "${CMAKE_CURRENT_SOURCE_DIR}/common/dsp"
                       "${CMAKE_CURRENT_SOURCE_DIR}/common/obs")
  else()
    set(_sd_common_src "${PROJECT_SOURCE_DIR}/common/obs/sd_presets.c")
    set(_sd_common_inc "${PROJECT_SOURCE_DIR}/common"
                       "${PROJECT_SOURCE_DIR}/common/dsp"
                       "${PROJECT_SOURCE_DIR}/common/obs")
  endif()
  add_library(${id} MODULE ${ARGN} ${_sd_common_src})
  target_link_libraries(${id} PRIVATE OBS::libobs)
  target_include_directories(${id} PRIVATE ${_sd_common_inc})
  target_compile_definitions(${id} PRIVATE PLUGIN_NAME="${id}" PLUGIN_VERSION="${version}")
  set_target_properties(${id} PROPERTIES PREFIX "")
  if(NOT MSVC)
    target_link_libraries(${id} PRIVATE m)
  endif()
  if(APPLE)
    set_target_properties(${id} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION plugin)
  endif()
endfunction()

enable_testing()
if(SD_BUILD_PINGPONG_ECHO)
  add_subdirectory(plugins/pingpong-echo)
endif()
if(SD_BUILD_EAR_TO_EAR)
  add_subdirectory(plugins/ear-to-ear)
endif()
if(SD_BUILD_REVERB)
  add_subdirectory(plugins/reverb)
endif()
if(SD_BUILD_8D_ORBIT)
  add_subdirectory(plugins/8d-orbit)
endif()
if(SD_BUILD_PROXIMITY)
  add_subdirectory(plugins/proximity)
endif()
if(SD_BUILD_BINAURAL)
  add_subdirectory(plugins/binaural)
endif()
