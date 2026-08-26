# Generate Core/Version.h from Version.h.in + optional git short hash.
# Required: VERSION_IN, VERSION_OUT, CMAKE_PROJECT_VERSION{,_MAJOR,_MINOR,_PATCH}
# Optional: GIT_EXECUTABLE, SOURCE_DIR

if(NOT DEFINED VERSION_IN OR NOT DEFINED VERSION_OUT)
    message(FATAL_ERROR "GenerateVersion.cmake requires -DVERSION_IN= and -DVERSION_OUT=")
endif()

set(DE_ENGINE_GIT "")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE DE_ENGINE_GIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _de_git_result
    )
    if(NOT _de_git_result EQUAL 0)
        set(DE_ENGINE_GIT "")
    endif()
    unset(_de_git_result)
endif()

get_filename_component(_de_outdir "${VERSION_OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_de_outdir}")
unset(_de_outdir)

configure_file("${VERSION_IN}" "${VERSION_OUT}" @ONLY)
