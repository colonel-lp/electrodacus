# Gzip every regular file in FRONTEND_DIR into OUT_DIR/<name>.gz
#
# Invoked from the top-level CMakeLists.txt as:
#   cmake -DFRONTEND_DIR=<src> -DOUT_DIR=<dst> -P tools/gzip_frontend.cmake
#
# The backend static file handler (main/app_httpd.cpp) requests "<file>.gz"
# first and serves it with "Content-Encoding: gzip", so only the compressed
# copies need to ship in the SPIFFS image.

if(NOT DEFINED FRONTEND_DIR OR NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "gzip_frontend.cmake: FRONTEND_DIR and OUT_DIR must be set")
endif()

find_program(GZIP_EXE gzip)
if(NOT GZIP_EXE)
    message(FATAL_ERROR "gzip_frontend.cmake: 'gzip' not found on PATH")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")
file(GLOB _files LIST_DIRECTORIES false "${FRONTEND_DIR}/*")

if(NOT _files)
    message(FATAL_ERROR "gzip_frontend.cmake: no files in ${FRONTEND_DIR}")
endif()

foreach(_f ${_files})
    get_filename_component(_name "${_f}" NAME)
    # -9 max compression, -n drop original name/timestamp (reproducible output)
    execute_process(
        COMMAND "${GZIP_EXE}" -9 -n -c "${_f}"
        OUTPUT_FILE "${OUT_DIR}/${_name}.gz"
        RESULT_VARIABLE _rc
    )
    if(_rc)
        message(FATAL_ERROR "gzip_frontend.cmake: failed on ${_f} (exit ${_rc})")
    endif()
    file(SIZE "${_f}" _raw)
    file(SIZE "${OUT_DIR}/${_name}.gz" _gz)
    message(STATUS "  frontend  ${_name}: ${_raw} -> ${_gz} bytes")
endforeach()
