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

# Files under <root>, relative, '/' separators. A CMake list splits a name holding ';', so the
# pieces are joined back until the name exists, and ';' is kept as '|' (not allowed in Windows
# file names) for as long as the name sits in a list.
function(_de_list_files out_var root)
    file(GLOB_RECURSE _raw RELATIVE "${root}" "${root}/*")
    set(_names "")
    set(_pending "")
    foreach(_part IN LISTS _raw)
        if(_pending STREQUAL "")
            set(_name "${_part}")
        else()
            set(_name "${_pending};${_part}")
        endif()
        if(EXISTS "${root}/${_name}" AND NOT IS_DIRECTORY "${root}/${_name}")
            string(REPLACE "\\" "/" _name "${_name}")
            string(REPLACE ";" "|" _name "${_name}")
            list(APPEND _names "${_name}")
            set(_pending "")
        else()
            set(_pending "${_name}")
        endif()
    endforeach()
    if(NOT _pending STREQUAL "")
        message(WARNING "CopyContent: could not read a file name under ${root} (left in place): ${_pending}")
    endif()
    set(${out_var} "${_names}" PARENT_SCOPE)
endfunction()

_de_list_files(_src_files "${SRC}")
_de_list_files(_dst_files "${DST}")
if(NOT _src_files)
    message(WARNING "CopyContent: source listing is empty; stale files were left in place")
    return()
endif()

string(JOIN "\n" _src_blob ${_src_files})
# Names compare without case, as on Windows file systems: overwriting foo.hlsl with Foo.hlsl keeps
# the old name, and a case-sensitive match would remove the file just copied.
string(TOLOWER "\n${_src_blob}\n" _src_blob)

set(_failed "")
foreach(_dst IN LISTS _dst_files)
    if(_dst MATCHES "(^|/)env/brdf_lut_[0-9]+_[0-9]+\\.bin$")
        continue()
    endif()
    string(TOLOWER "${_dst}" _dst_lower)
    string(FIND "${_src_blob}" "\n${_dst_lower}\n" _pos)
    if(_pos EQUAL -1)
        string(REPLACE "|" ";" _name "${_dst}")
        message(STATUS "CopyContent: removing stale ${_name}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${DST}/${_name}"
            RESULT_VARIABLE _rm
            ERROR_VARIABLE _rm_err
        )
        if(NOT _rm EQUAL 0 OR EXISTS "${DST}/${_name}")
            string(APPEND _failed "\n  ${_name} ${_rm_err}")
        endif()
    endif()
endforeach()

if(NOT _failed STREQUAL "")
    message(WARNING "CopyContent: could not remove every stale file${_failed}")
endif()
