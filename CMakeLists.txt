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
# which gives the VPL target DEBUG_POSTFIX = "d" property. In multi-config
# generators (Visual Studio) this propagates to all consumers of VPL,
# causing them to link against debug-named libraries (libvpld.dll,
# Qt6Widgetsd.dll) even in Release builds.
#
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
endif()

# VPL-private (experimental / hidden stuff)
add_subdirectory(libvpl-private)

# Actual plugin
add_subdirectory(obs-qsvonevpl)