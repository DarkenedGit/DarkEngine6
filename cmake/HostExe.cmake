# Per-exe platform setup (plan §3.15 "de_add_exe"): every exe that links DarkEngine calls
#
#   de_host_exe(<target> SHADERS RUNTIME)         # Editor, UnitTests: runtime DXC + cache in every config
#   de_host_exe(<target> SHADERS BAKED_RELEASE)   # game exes: runtime DXC in Debug, baked DXIL otherwise
#
# It adds Core/AgilityExports.cpp and the Agility runtime (cmake/AgilitySdk.cmake), defines the shader
# policy (Core/HostShaderPolicy.cpp -> Dark::kHostRuntimeShaderCompile), copies the DXC runtime to
# <exe dir>/dxc/ where the policy compiles at runtime, copies the baked shaders to <exe dir>/shaders/,
# and copies WinPixEventRuntime.dll.
#
#   de_copy_content(<target>)                     # repo content/ to <exe dir>/content/
#
# The content and baked-shader copies run on every build (cmake/CopyContent.cmake mirrors the folder),
# not only when the exe relinks, so a content-only change or a removed file reaches the exe folder.

set(DE_HOST_SHADER_POLICY_CPP "${CMAKE_SOURCE_DIR}/Core/HostShaderPolicy.cpp")

# Mirrors <src> into <exe dir>/<subdir> before <target> builds. The copy is a custom target with no
# outputs, so it is always out of date and runs on every build. Exes with the same output folder share
# one copy target per <subdir>, so a parallel build never copies into one folder twice at once.
# <depends> are targets the copy runs after. Multi-config generators put the exe in <dir>/<config>.
function(_de_exe_folder_copy target subdir src depends)
    get_target_property(_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _dir)
        get_target_property(_dir ${target} BINARY_DIR)
    endif()
    get_property(_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(_exe_dir "${_dir}")
    if(_multi AND NOT _dir MATCHES "\\$<")
        string(APPEND _exe_dir "/$<CONFIG>")
    endif()

    string(MAKE_C_IDENTIFIER "${_dir}/${subdir}" _key)
    get_property(_copy GLOBAL PROPERTY "DE_EXE_FOLDER_COPY:${_key}")
    if(_copy)
        get_property(_copy_src GLOBAL PROPERTY "DE_EXE_FOLDER_COPY_SRC:${_key}")
        if(NOT _copy_src STREQUAL src)
            message(FATAL_ERROR "${target}: ${_dir}/${subdir} is already filled from ${_copy_src}, not ${src}")
        endif()
    else()
        file(RELATIVE_PATH _rel "${CMAKE_BINARY_DIR}" "${_dir}")
        if(_rel STREQUAL "" OR _rel MATCHES "^\\.\\.")
            set(_rel "${target}")
        endif()
        string(MAKE_C_IDENTIFIER "de_copy_${_rel}_${subdir}" _copy)
        add_custom_target(${_copy}
            COMMAND "${CMAKE_COMMAND}" "-DSRC=${src}" "-DDST=${_exe_dir}/${subdir}" -P "${CMAKE_SOURCE_DIR}/cmake/CopyContent.cmake"
            COMMENT "Copying ${src} to ${_dir}/${subdir}"
            VERBATIM
        )
        set_target_properties(${_copy} PROPERTIES FOLDER "Engine")
        if(depends)
            add_dependencies(${_copy} ${depends})
        endif()
        set_property(GLOBAL PROPERTY "DE_EXE_FOLDER_COPY:${_key}" "${_copy}")
        set_property(GLOBAL PROPERTY "DE_EXE_FOLDER_COPY_SRC:${_key}" "${src}")
    endif()
    add_dependencies(${target} ${_copy})
endfunction()

# Keeps the repo content/ next to the exe (the working directory is not reliable under VS). Best
# effort: a file locked by a running exe is reported, not a build failure.
function(de_copy_content target)
    _de_exe_folder_copy(${target} content "${CMAKE_SOURCE_DIR}/content" "")
endfunction()

function(de_host_exe target)
    cmake_parse_arguments(PARSE_ARGV 1 _host "" "SHADERS" "")
    if(NOT _host_SHADERS MATCHES "^(RUNTIME|BAKED_RELEASE)$")
        message(FATAL_ERROR "de_host_exe(${target}): SHADERS must be RUNTIME or BAKED_RELEASE")
    endif()

    de_add_agility(${target})
    de_copy_pix_dll(${target})

    target_sources(${target} PRIVATE "${DE_HOST_SHADER_POLICY_CPP}")
    if(_host_SHADERS STREQUAL "RUNTIME")
        target_compile_definitions(${target} PRIVATE DE_HOST_RUNTIME_SHADERS=1)
        de_copy_dxc_runtime(${target} ALWAYS)
    else()
        target_compile_definitions(${target} PRIVATE "DE_HOST_RUNTIME_SHADERS=$<IF:$<CONFIG:Debug>,1,0>")
        de_copy_dxc_runtime(${target} DEBUG_ONLY)
        # Baked DXIL in every config, so DE_SHADER_BAKED=1 can exercise the Release path from Debug.
        add_dependencies(${target} de_shaders)
        _de_exe_folder_copy(${target} shaders "${DE_BAKED_SHADER_DIR}" de_shaders)
    endif()
endfunction()
