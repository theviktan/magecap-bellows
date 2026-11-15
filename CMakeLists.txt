cmake_minimum_required(VERSION 3.20)

project(ForgeLanguage VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FORGE_BUILD_CLI "Build the forge command line tool" ON)
option(FORGE_BUILD_TESTS "Build the Forge test suite" ON)
option(FORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Shared compiler warnings for every Forge target.
function(forge_set_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
		if(FORGE_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif()
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
		if(FORGE_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
endfunction()

add_subdirectory(Forge)

if(FORGE_BUILD_CLI)
	add_subdirectory(ForgeCLI)
endif()

if(FORGE_BUILD_TESTS)
	enable_testing()
	add_subdirectory(Tests)
endif()

option(FORGE_BUILD_EXAMPLES "Build the engine integration example" ON)
if(FORGE_BUILD_EXAMPLES)
	add_subdirectory(Examples)
endif()
