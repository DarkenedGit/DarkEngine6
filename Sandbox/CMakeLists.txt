add_executable(Sandbox WIN32
    main.cpp
    SandboxApp.cpp
    SandboxBoot.cpp
)

target_link_libraries(Sandbox PRIVATE DarkEngine)

include(CompilerOptions)
de_set_compiler_options(Sandbox)
