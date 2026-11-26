# Release shader bake (plan D10, §3.15.1): custom target de_shaders compiles every permutation in
# each shader root's shaders.manifest with dxc into ${CMAKE_BINARY_DIR}/shaders/<name>.dxil, signed by
# the build machine's dxil.dll. PDBs go to ${CMAKE_BINARY_DIR}/shaders_pdb/ and stay on the build
# machine. Exes with baked shaders copy shaders/ next to themselves (de_host_exe).
#
# A shader root is a folder under content/ with its own shaders.manifest. content/shaders is added
# here; other roots call de_add_shader_root(<dir>) after this file is included. Every root bakes into
# the one flat shaders/ folder, so a shader file (path under its root) or a baked name in two roots
# fails the configure. A root's shaders may #include content/shaders files by name (-I after their
# own folder), as at runtime (addShaderRoot in Render/ShaderCompile.h).
#
# Inputs: DE_DXC_EXE (cmake/Dxc.cmake). The shader model is read from kShaderModelDxc in
# Render/ShaderKey.h so the bake and the runtime compiler always agree.
# Names follow bakedShaderFileName() in Render/ShaderKey.cpp; ShaderManifestTests checks the manifest.
#
# Each permutation is one custom command with two dxc runs: the compile, then `-M -MF` for a depfile
# so a change to an included .hlsli rebuilds only the permutations that include it.

set(DE_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/content/shaders")
set(DE_BAKED_SHADER_DIR "${CMAKE_BINARY_DIR}/shaders")
set(DE_BAKED_SHADER_PDB_DIR "${CMAKE_BINARY_DIR}/shaders_pdb")
set(DE_BAKED_SHADER_DEP_DIR "${CMAKE_BINARY_DIR}/shaders_deps")

file(STRINGS "${CMAKE_SOURCE_DIR}/Render/ShaderKey.h" _de_sm_line REGEX "kShaderModelDxc[ \t]*=")
string(REGEX MATCH "\"([0-9]+_[0-9]+)\"" _de_sm_match "${_de_sm_line}")
set(DE_SHADER_MODEL "${CMAKE_MATCH_1}")
if(NOT DE_SHADER_MODEL)
    message(FATAL_ERROR "ShaderBake: could not read kShaderModelDxc from Render/ShaderKey.h")
endif()
unset(_de_sm_line)
unset(_de_sm_match)

# Same rule as bakedShaderFileName(): "<stem>.<entry>.<stage>[.<defines>].dxil" with '=' -> '-',
# ',' and '/' -> '_', anything else outside [A-Za-z0-9_.-] -> '_'.
function(de_baked_shader_name out_var file entry stage defines)
    string(REGEX REPLACE "\\.hlsl$" "" _stem "${file}")
    set(_name "${_stem}.${entry}.${stage}")
    if(NOT defines STREQUAL "")
        string(APPEND _name ".${defines}")
    endif()
    string(REPLACE "=" "-" _name "${_name}")
    string(REPLACE "," "_" _name "${_name}")
    string(REPLACE "/" "_" _name "${_name}")
    string(REGEX REPLACE "[^A-Za-z0-9_.-]" "_" _name "${_name}")
    set(${out_var} "${_name}.dxil" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY "${DE_BAKED_SHADER_DIR}" "${DE_BAKED_SHADER_PDB_DIR}" "${DE_BAKED_SHADER_DEP_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Render/ShaderKey.h")

add_custom_target(de_shaders)
set_target_properties(de_shaders PROPERTIES FOLDER "Engine")

# Names compare without case, as on Windows file systems.
function(_de_shader_name_owner out_var kind name)
    string(TOLOWER "${name}" _lower)
    get_property(${out_var} GLOBAL PROPERTY "DE_SHADER_${kind}_OWNER:${_lower}")
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(_de_claim_shader_name kind name root)
    string(TOLOWER "${name}" _lower)
    set_property(GLOBAL PROPERTY "DE_SHADER_${kind}_OWNER:${_lower}" "${root}")
endfunction()

# Bakes every permutation in <root_dir>/shaders.manifest into de_shaders. root_dir is a folder under
# ${CMAKE_SOURCE_DIR}/content.
function(de_add_shader_root root_dir)
    get_filename_component(_root "${root_dir}" ABSOLUTE)
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}/content" "${_root}")
    if(_rel STREQUAL "" OR _rel MATCHES "^\\.\\." OR IS_ABSOLUTE "${_rel}")
        message(FATAL_ERROR "de_add_shader_root: ${root_dir} is not a folder under content/")
    endif()
    set(_manifest "${_root}/shaders.manifest")
    if(NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "de_add_shader_root: content/${_rel}/shaders.manifest does not exist")
    endif()
    get_property(_roots GLOBAL PROPERTY DE_SHADER_ROOTS)
    foreach(_other IN LISTS _roots)
        string(TOLOWER "${_rel}" _a)
        string(TOLOWER "${_other}" _b)
        string(FIND "${_a}/" "${_b}/" _a_in_b)
        string(FIND "${_b}/" "${_a}/" _b_in_a)
        if(_a_in_b EQUAL 0 OR _b_in_a EQUAL 0)
            message(FATAL_ERROR "de_add_shader_root: content/${_rel} overlaps shader root content/${_other}")
        endif()
    endforeach()

    # Every .hlsl/.hlsli in this root is claimed for it (manifest files must exist, so they are among them).
    file(GLOB_RECURSE _files RELATIVE "${_root}" "${_root}/*.hlsl" "${_root}/*.hlsli")
    file(STRINGS "${_manifest}" _lines)
    set(_entries "")
    set(_line_no 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _line_no "${_line_no} + 1")
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^#")
            continue()
        endif()
        string(REGEX REPLACE "[ \t]+" ";" _fields "${_line}")
        list(LENGTH _fields _count)
        if(_count LESS 3 OR _count GREATER 4)
            message(FATAL_ERROR "content/${_rel}/shaders.manifest line ${_line_no}: expected '<file> <entry> <stage> [defines]': ${_line}")
        endif()
        list(GET _fields 0 _file)
        list(GET _fields 2 _stage)
        if(NOT _stage MATCHES "^(vs|ps|cs)$")
            message(FATAL_ERROR "content/${_rel}/shaders.manifest line ${_line_no}: stage '${_stage}' is not vs, ps or cs")
        endif()
        if(NOT EXISTS "${_root}/${_file}")
            message(FATAL_ERROR "content/${_rel}/shaders.manifest line ${_line_no}: ${_file} does not exist under content/${_rel}")
        endif()
        string(REPLACE ";" "|" _entry "${_fields}")
        list(APPEND _entries "${_line_no}|${_entry}")
    endforeach()
    foreach(_file IN LISTS _files)
        _de_shader_name_owner(_owner FILE "${_file}")
        if(NOT _owner STREQUAL "" AND NOT _owner STREQUAL "${_rel}")
            message(FATAL_ERROR "Shader file '${_file}' is in both content/${_owner}/ and content/${_rel}/; shader file names must be unique across shader roots")
        endif()
    endforeach()

    set(_include_args -I "${_root}")
    if(NOT _root STREQUAL DE_SHADER_SOURCE_DIR)
        list(APPEND _include_args -I "${DE_SHADER_SOURCE_DIR}")
    endif()

    set(_outputs "")
    set(_names "")
    foreach(_e IN LISTS _entries)
        string(REPLACE "|" ";" _fields "${_e}")
        list(GET _fields 0 _line_no)
        list(GET _fields 1 _file)
        list(GET _fields 2 _entry)
        list(GET _fields 3 _stage)
        set(_defines "")
        list(LENGTH _fields _count)
        if(_count EQUAL 5)
            list(GET _fields 4 _defines)
        endif()
        set(_src "${_root}/${_file}")

        de_baked_shader_name(_name "${_file}" "${_entry}" "${_stage}" "${_defines}")
        _de_shader_name_owner(_owner BAKED "${_name}")
        string(TOLOWER "${_name}" _lower_name)
        if(NOT _owner STREQUAL "" OR _lower_name IN_LIST _names)
            if(_owner STREQUAL "")
                set(_owner "${_rel}")
            endif()
            message(FATAL_ERROR "content/${_rel}/shaders.manifest line ${_line_no}: baked shader '${_name}' is also baked from content/${_owner}/")
        endif()
        list(APPEND _names "${_lower_name}")

        set(_out "${DE_BAKED_SHADER_DIR}/${_name}")
        string(REGEX REPLACE "\\.dxil$" "" _base "${_name}")
        set(_pdb "${DE_BAKED_SHADER_PDB_DIR}/${_base}.pdb")
        set(_dep "${DE_BAKED_SHADER_DEP_DIR}/${_base}.d")

        set(_define_args "")
        if(NOT _defines STREQUAL "")
            string(REPLACE "," ";" _pairs "${_defines}")
            foreach(_pair IN LISTS _pairs)
                list(APPEND _define_args -D "${_pair}")
            endforeach()
        endif()

        set(_common -nologo -T "${_stage}_${DE_SHADER_MODEL}" -E "${_entry}" -HV 2021 -Ges ${_define_args} ${_include_args})
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${DE_DXC_EXE}" ${_common} -O3 -Zi -Qstrip_debug -Fd "${_pdb}" -Fo "${_out}" "${_src}"
            COMMAND "${DE_DXC_EXE}" ${_common} -M -MF "${_dep}" -Fo "${_out}" "${_src}"
            DEPENDS "${_src}" "${_manifest}"
            DEPFILE "${_dep}"
            COMMENT "DXC ${_stage}_${DE_SHADER_MODEL} ${_name}"
            VERBATIM
        )
        list(APPEND _outputs "${_out}")
    endforeach()

    # Claimed only once the whole root checked out.
    foreach(_file IN LISTS _files)
        _de_claim_shader_name(FILE "${_file}" "${_rel}")
    endforeach()
    foreach(_name IN LISTS _names)
        _de_claim_shader_name(BAKED "${_name}" "${_rel}")
    endforeach()
    set_property(GLOBAL APPEND PROPERTY DE_SHADER_ROOTS "${_rel}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")

    string(MAKE_C_IDENTIFIER "${_rel}" _target_suffix)
    set(_target "de_shaders_${_target_suffix}")
    add_custom_target(${_target} DEPENDS ${_outputs} SOURCES "${_manifest}")
    set_target_properties(${_target} PROPERTIES FOLDER "Engine")
    add_dependencies(de_shaders ${_target})

    list(LENGTH _outputs _count)
    message(STATUS "Shader bake: ${_count} permutations from content/${_rel} at SM ${DE_SHADER_MODEL}")
endfunction()

de_add_shader_root("${DE_SHADER_SOURCE_DIR}")

# Generated byte-array header for the LoadingScreen splash (no files, no compiler at runtime).
set(DE_LOADING_SCREEN_SHADERS_H "${DE_GENERATED_DIR}/Media/LoadingScreenShaders.h")
add_custom_command(
    OUTPUT "${DE_LOADING_SCREEN_SHADERS_H}"
    COMMAND ${CMAKE_COMMAND}
        "-DDXC=${DE_DXC_EXE}"
        "-DSRC=${DE_SHADER_SOURCE_DIR}/LoadingScreen.hlsl"
        "-DOUT=${DE_LOADING_SCREEN_SHADERS_H}"
        "-DSM=${DE_SHADER_MODEL}"
        -DNAMESPACE=LoadingScreenShaders
        "-DENTRIES=VSMain:vs$<SEMICOLON>PSMain:ps"
        -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS "${DE_SHADER_SOURCE_DIR}/LoadingScreen.hlsl" "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake" "${CMAKE_SOURCE_DIR}/Render/ShaderKey.h"
    COMMENT "Embedding LoadingScreen shaders"
    VERBATIM
)
