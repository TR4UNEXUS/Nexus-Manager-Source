include($ENV{DEVKITPRO}/cmake/Switch.cmake)
# Use the pinned FW23-compatible libnx without changing the shared SDK install.
if(DEFINED ENV{LIBNX} AND EXISTS "$ENV{LIBNX}/include/switch.h")
    set(NX_ROOT "$ENV{LIBNX}")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${NX_ROOT}")
endif()
