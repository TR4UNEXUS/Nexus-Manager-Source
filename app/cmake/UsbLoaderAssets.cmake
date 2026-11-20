# Only app resources, no upstream update/feed definitions or unused themes.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED DEST OR NOT DEST MATCHES "/usbloader-romfs$")
    message(FATAL_ERROR "Expected an explicit generated usbloader-romfs directory")
endif()
file(MAKE_DIRECTORY "${DEST}/themes")
file(COPY "${SOURCE_DIR}/assets/usb-loader-icon.jpg" DESTINATION "${DEST}")
foreach(folder certs theme i18n fonts)
    file(COPY "${SOURCE_DIR}/assets/romfs/${folder}" DESTINATION "${DEST}")
endforeach()
file(COPY "${SOURCE_DIR}/assets/romfs/themes/base_black_theme.ini"
          "${SOURCE_DIR}/assets/romfs/themes/usb_loader_theme.ini"
     DESTINATION "${DEST}/themes")
# Preserve upstream translation sources and attribution. Rebrand the generated
# runtime copies, including keys matching rebranded generic dialog messages.
file(GLOB translations "${DEST}/i18n/*.json")
foreach(path IN LISTS translations)
    file(READ "${path}" text)
    string(REPLACE "Sphaira" "Nexus Manager" text "${text}")
    string(REPLACE "sphaira" "Nexus Manager" text "${text}")
    file(WRITE "${path}" "${text}")
endforeach()
