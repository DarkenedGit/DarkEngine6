# Best-effort copy of content/ next to the executable.
# Does not fail the build if files are locked (running Sandbox).

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "CopyContent.cmake requires -DSRC= and -DDST=")
endif()

if(NOT EXISTS "${SRC}")
    message(WARNING "CopyContent: source does not exist: ${SRC}")
    return()
endif()

file(MAKE_DIRECTORY "${DST}")

# Prefer file(COPY) — no external process quoting issues.
# If something is locked, CMake reports an error; we catch via execute_process fallback.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SRC}" "${DST}"
    RESULT_VARIABLE _rc
    ERROR_VARIABLE _err
    OUTPUT_VARIABLE _out
)

if(NOT _rc EQUAL 0)
    message(WARNING "CopyContent: could not refresh content (is Sandbox still running?)\n${_err}")
else()
    message(STATUS "CopyContent: ${SRC} -> ${DST}")
endif()
