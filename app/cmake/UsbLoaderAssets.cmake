# Only app resources, no upstream update/feed definitions or unused themes.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DEST OR NOT DEST MATCHES "/usbloader-romfs$")
    message(FATAL_ERROR "Expected an explicit generated usbloader-romfs directory")
endif()
file(MAKE_DIRECTORY "${DEST}/themes")
file(COPY "${SOURCE_DIR}/assets/usb-loader-icon.jpg" DESTINATION "${DEST}")
foreach(folder certs theme i18n fonts nexus-hats)
    file(COPY "${SOURCE_DIR}/assets/romfs/${folder}" DESTINATION "${DEST}")
endforeach()
file(MAKE_DIRECTORY "${DEST}/nexus")
file(COPY "${SOURCE_DIR}/assets/romfs/nexus/repair-catalog.nxrc"
     DESTINATION "${DEST}/nexus")
file(COPY "${SOURCE_DIR}/assets/romfs/themes/base_black_theme.ini"
          "${SOURCE_DIR}/assets/romfs/themes/usb_loader_theme.ini"
     DESTINATION "${DEST}/themes")
# Catalogues include both legacy keys and the current Nexus UI messages.
# Copy them unchanged: replacing text here changes lookup keys, attribution
# and documented paths after the source catalogue has already been validated.
