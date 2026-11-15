# forgescript: run scripts, check them for mistakes, or try code in the interactive prompt.
add_executable(ForgeScriptCLI src/Main.cpp)
set_target_properties(ForgeScriptCLI PROPERTIES OUTPUT_NAME forgescript)
target_link_libraries(ForgeScriptCLI PRIVATE ForgeScript)
forge_set_warnings(ForgeScriptCLI)
