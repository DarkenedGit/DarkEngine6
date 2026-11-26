# Best-effort copy of content/ next to the executable.
# Does not fail the build if files are locked (running Sandbox).
# copy_directory overwrites and adds. It does not remove a file the repo dropped.
# A leftover .hlsl keeps its name in the engine shader root, and addShaderRoot then
# refuses the game root (the same file name in two roots). That leaves camouflage
# and echolocation with no pipeline.

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "CopyContent.cmake requires -DSRC= and -DDST=")
endif()

if(NOT EXISTS "${SRC}")
    message(WARNING "CopyContent: source does not exist: ${SRC}")
    return()
endif()

file(MAKE_DIRECTORY "${DST}")

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

# Drop destination files the source no longer has. Skip when the source listing is
# empty so a bad glob cannot wipe the copy. env/brdf_lut_<size>_<samples>.bin is the
# renderer's LUT cache, written beside the exe, not a shipped file.
file(GLOB_RECURSE _src_files RELATIVE "${SRC}" "${SRC}/*")
file(GLOB_RECURSE _dst_files RELATIVE "${DST}" "${DST}/*")
if(NOT _src_files)
    message(WARNING "CopyContent: source listing is empty; stale files were left in place")
    return()
endif()

set(_src_norm "")
foreach(_src IN LISTS _src_files)
    string(REPLACE "\\" "/" _src "${_src}")
    list(APPEND _src_norm "${_src}")
endforeach()
string(JOIN "\n" _src_blob ${_src_norm})
# Names compare without case, as on Windows file systems: overwriting foo.hlsl with Foo.hlsl keeps
# the old name, and a case-sensitive match would remove the file just copied.
string(TOLOWER "\n${_src_blob}\n" _src_blob)

set(_stale "")
foreach(_dst IN LISTS _dst_files)
    string(REPLACE "\\" "/" _dst "${_dst}")
    if(_dst MATCHES "(^|/)env/brdf_lut_[0-9]+_[0-9]+\\.bin$")
        continue()
    endif()
    string(TOLOWER "${_dst}" _dst_lower)
    string(FIND "${_src_blob}" "\n${_dst_lower}\n" _pos)
    if(_pos EQUAL -1)
        list(APPEND _stale "${DST}/${_dst}")
        message(STATUS "CopyContent: removing stale ${_dst}")
    endif()
endforeach()

if(_stale)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E rm -f ${_stale}
        RESULT_VARIABLE _rm
        ERROR_VARIABLE _rm_err
    )
    if(NOT _rm EQUAL 0)
        message(WARNING "CopyContent: could not remove every stale file\n${_rm_err}")
    endif()
endif()
