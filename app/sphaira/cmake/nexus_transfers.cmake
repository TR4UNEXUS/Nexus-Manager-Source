# Local D4 test: session-only adapters; legacy auto-start servers remain disabled.
set(nexus_deps "${CMAKE_SOURCE_DIR}/../deps")
set(libhaze_SOURCE_DIR "${nexus_deps}/libhaze")
add_subdirectory("${libhaze_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/nexus-haze")
set(FTPSRV_LIB_BUILD ON)
set(FTPSRV_LIB_VFS_CUSTOM "${nexus_deps}/ftpsrv/src/platform/nx/vfs_nx.h")
set(FTPSRV_LIB_PATH_SIZE 769)
set(FTPSRV_LIB_SESSIONS 2)
set(FTPSRV_LIB_BUF_SIZE 262144)
set(FTPSRV_LIB_CUSTOM_DEFINES USE_VFS_SAVE=0 USE_VFS_STORAGE=0 USE_VFS_GC=0 USE_VFS_USBHSFS=0 USE_VFS_ROMFS=0 VFS_NX_BUFFER_IO=1 FTP_SOCKET_HEADER="${nexus_deps}/ftpsrv/src/platform/nx/socket_nx.h")
add_subdirectory("${nexus_deps}/ftpsrv" "${CMAKE_BINARY_DIR}/nexus-ftpsrv")
set_property(TARGET ftpsrv PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
target_link_libraries(sphaira PRIVATE haze ftpsrv)
add_library(nexus-sphaira-ftp
    ${nexus_deps}/ftpsrv/src/platform/nx/vfs_nx.c
    ${nexus_deps}/ftpsrv/src/platform/nx/vfs/vfs_nx_none.c
    ${nexus_deps}/ftpsrv/src/platform/nx/vfs/vfs_nx_root.c
    ${nexus_deps}/ftpsrv/src/platform/nx/vfs/vfs_nx_fs.c
    ${nexus_deps}/ftpsrv/src/platform/nx/utils.c)
target_link_libraries(nexus-sphaira-ftp PUBLIC ftpsrv)
target_include_directories(nexus-sphaira-ftp PUBLIC ${nexus_deps}/ftpsrv/src/platform)
target_link_libraries(sphaira PRIVATE nexus-sphaira-ftp)
target_sources(sphaira PRIVATE source/haze_helper.cpp source/ftpsrv_helper.cpp)
# Actual upstream virtual NSP builder/reader and file-change notification.
# Their UI is not substituted for Manager; unreferenced sections are discarded.
target_sources(sphaira PRIVATE source/ui/menus/game_menu.cpp source/ui/menus/homebrew.cpp)
# Deliberately do not define ENABLE_LIBHAZE: Nexus owns foreground sessions.
target_sources(sphaira PRIVATE source/nexus/transfer.cpp source/nexus/transfer_install.cpp source/nexus/cartridge.cpp source/ui/menus/extended_tools_menu.cpp)
target_sources(sphaira PRIVATE source/nexus/port_transfer.cpp)
