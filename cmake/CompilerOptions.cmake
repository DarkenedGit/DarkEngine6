# cmake/CompilerOptions.cmake
# Shared compiler flags for all DarkEngine6 targets

function(de_set_compiler_options target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4 /WX /MP /permissive- /Zc:__cplusplus
            /wd4100   # unreferenced formal parameter
            /wd4505   # unreferenced local function removed
        >
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
            -Wall -Wextra -Wpedantic -Werror
        >
    )

    if(DE_ENABLE_ASSERTS)
        target_compile_definitions(${target} PRIVATE DE_ENABLE_ASSERTS=1)
    endif()
endfunction()
