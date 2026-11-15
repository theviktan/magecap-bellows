cmake_minimum_required(VERSION 3.20)

project(ForgeScript VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FORGESCRIPT_BUILD_CLI "Build the forgescript command line tool" ON)
option(FORGESCRIPT_BUILD_TESTS "Build the ForgeScript test suite" ON)
option(FORGESCRIPT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Shared compiler warnings for every ForgeScript target.
function(forge_set_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
		if(FORGESCRIPT_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif()
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
		if(FORGESCRIPT_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
endfunction()

add_subdirectory(ForgeScript)

if(FORGESCRIPT_BUILD_CLI)
	add_subdirectory(ForgeScriptCLI)
endif()

if(FORGESCRIPT_BUILD_TESTS)
	enable_testing()
	add_subdirectory(Tests)
endif()

option(FORGESCRIPT_BUILD_EXAMPLES "Build the engine integration example" ON)
if(FORGESCRIPT_BUILD_EXAMPLES)
	add_subdirectory(Examples)
endif()
