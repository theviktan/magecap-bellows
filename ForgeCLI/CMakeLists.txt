# forge: run scripts, check them for mistakes, or try code in the interactive prompt.
add_executable(ForgeCLI src/Main.cpp)
set_target_properties(ForgeCLI PROPERTIES OUTPUT_NAME forge)
target_link_libraries(ForgeCLI PRIVATE Forge)
forge_set_warnings(ForgeCLI)
