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
# VPL
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(libvpl)

# VPL's CompileOptions.cmake sets CMAKE_DEBUG_POSTFIX "d", which gives
# the VPL target DEBUG_POSTFIX = "d" property. In multi-config generators
# (Visual Studio) this causes all consumers of VPL to link against debug
# library names (libvpld.dll, Qt6Widgetsd.dll) even in Release builds.
# Explicitly reset to empty to prevent this behavior.
if(TARGET VPL)
  set_target_properties(VPL PROPERTIES DEBUG_POSTFIX "")
endif()
endif()

# VPL-private (experimental / hidden stuff)
add_subdirectory(libvpl-private)

# Actual plugin
add_subdirectory(obs-qsvonevpl)