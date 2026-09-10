cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(electrodacus-esp32)

# ---------------------------------------------------------------------------
# Frontend build step
#
# The web UI is hand-written HTML/CSS/JS in frontend/. This step gzips every
# file there into data/www/*.gz so the SPIFFS image (packed from data/) always
# carries the current, compressed UI. data/www/ is generated and git-ignored;
# it is wiped and rebuilt whenever a file in frontend/ changes.
# ---------------------------------------------------------------------------
file(GLOB FRONTEND_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/frontend/*)
set(FRONTEND_WWW_DIR ${CMAKE_CURRENT_LIST_DIR}/data/www)
set(FRONTEND_STAMP ${CMAKE_BINARY_DIR}/frontend_www.stamp)

add_custom_command(
    OUTPUT ${FRONTEND_STAMP}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${FRONTEND_WWW_DIR}
    COMMAND ${CMAKE_COMMAND}
            -DFRONTEND_DIR=${CMAKE_CURRENT_LIST_DIR}/frontend
            -DOUT_DIR=${FRONTEND_WWW_DIR}
            -P ${CMAKE_CURRENT_LIST_DIR}/tools/gzip_frontend.cmake
    COMMAND ${CMAKE_COMMAND} -E touch ${FRONTEND_STAMP}
    DEPENDS ${FRONTEND_SRC} ${CMAKE_CURRENT_LIST_DIR}/tools/gzip_frontend.cmake
    COMMENT "Compressing frontend/ -> data/www/*.gz"
    VERBATIM
)
add_custom_target(frontend_www ALL DEPENDS ${FRONTEND_STAMP})

# Build the SPIFFS image from data/ (only after the frontend is compressed) and
# flash it together with the app.
spiffs_create_partition_image(spiffs ${CMAKE_CURRENT_LIST_DIR}/data FLASH_IN_PROJECT
    DEPENDS frontend_www)
