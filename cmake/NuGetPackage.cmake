# Configure-time NuGet download with a pinned SHA-256, the same pattern as cmake/WinPixEventRuntime.cmake.
# The package is extracted under ${CMAKE_BINARY_DIR}/_deps/<id>-<version>/ so the repo stores no binaries.
#
#   de_fetch_nuget_package(<package id> <version> <sha256> <out_root_var> <file that must exist>...)

function(de_fetch_nuget_package package_id version sha256 out_root)
    string(TOLOWER "${package_id}" _id_lower)
    set(_root "${CMAKE_BINARY_DIR}/_deps/${_id_lower}-${version}")
    set(_missing FALSE)
    foreach(_rel IN LISTS ARGN)
        if(NOT EXISTS "${_root}/${_rel}")
            set(_missing TRUE)
        endif()
    endforeach()

    if(_missing)
        file(MAKE_DIRECTORY "${_root}")
        set(_nupkg "${_root}/${package_id}.${version}.nupkg")
        message(STATUS "Downloading ${package_id} ${version}")
        file(DOWNLOAD
            "https://www.nuget.org/api/v2/package/${package_id}/${version}"
            "${_nupkg}"
            EXPECTED_HASH SHA256=${sha256}
            STATUS _status
            TLS_VERIFY ON
        )
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            list(GET _status 1 _msg)
            message(FATAL_ERROR "${package_id} ${version} download failed (${_code}): ${_msg}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_nupkg}" DESTINATION "${_root}")
        file(REMOVE "${_nupkg}")
    endif()

    foreach(_rel IN LISTS ARGN)
        if(NOT EXISTS "${_root}/${_rel}")
            message(FATAL_ERROR "${package_id} ${version} is incomplete: ${_root}/${_rel} is missing")
        endif()
    endforeach()

    set(${out_root} "${_root}" PARENT_SCOPE)
endfunction()
