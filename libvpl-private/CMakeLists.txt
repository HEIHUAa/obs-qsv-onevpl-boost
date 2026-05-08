add_library(libvpl-private INTERFACE)

target_include_directories(libvpl-private
  INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(DEFINED MEDIASDK_INCLUDE_DIR AND NOT MEDIASDK_INCLUDE_DIR STREQUAL "")
  target_include_directories(libvpl-private
    INTERFACE
    ${MEDIASDK_INCLUDE_DIR}
  )
endif()