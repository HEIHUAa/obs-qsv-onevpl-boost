cmake_minimum_required(VERSION 3.24...3.25)

project(obs-qsv-onevpl
  VERSION 1.2.0.0
  LANGUAGES CXX
)

# If building as subdirectory..
if(NOT PROJECT_IS_TOP_LEVEL)
  # Remove warnings as errors, needed for in-OBS builds
  if(UNIX)
    add_compile_options(-Wno-error)
  elseif(WIN32)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY COMPILE_WARNING_AS_ERROR OFF)
  endif()
endif()

if(WIN32)
# VPL's CompileOptions.cmake sets CMAKE_DEBUG_POSTFIX "d" internally,
# which gives the VPL target DEBUG_POSTFIX = "d" property.
# Pre-set CMAKE_DEBUG_POSTFIX in the CACHE to block VPL's override,
# then also explicitly clear the property on VPL after creation.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "Debug library postfix" FORCE)

# VPL
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(libvpl)

# Override VPL target property as defense-in-depth
if(TARGET VPL)
  set_target_properties(VPL PROPERTIES DEBUG_POSTFIX "")
endif()

# Qt6 imported targets from OBS deps may not expose standard "RELEASE"
# configuration to multi-config generators.  Override the imported config
# mapping for Release builds so that CMake always resolves imported targets
# (Qt6::Widgets, etc.) to their "RELEASE" variant first, then falls back
# to "RelWithDebInfo".  Without this, Release builds can incorrectly resolve
# Qt6 to debug-named libraries (Qt6Widgetsd.dll).
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE
  "RELEASE"
  "RelWithDebInfo"
  CACHE STRING "Release config mapping for imported targets" FORCE)
endif()

# VPL-private (experimental / hidden stuff)
add_subdirectory(libvpl-private)

# Actual plugin
add_subdirectory(obs-qsvonevpl)