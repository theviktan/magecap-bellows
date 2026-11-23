# Checks every ForgeScript script in the repository against the engine API, and fails when one of
# them gives its own function, kind, choices or value the name of a built-in or engine function.
# A script's own name always wins, so such a script keeps working, but it no longer reaches the
# built-in. This keeps a Bellows change that adds an engine name from silently taking over a name a
# test or sample game already uses (audit S-15). A test that checks own names winning on purpose says
# so with a line "# Takes built-in names on purpose." and is left out. The sample projects' scripts and
# the benchmarks must have no warning at all (--check --strict): they are what game makers copy, so a
# checker change that starts warning on working code is caught in that change. Run by ctest:
#   cmake -DFORGESCRIPT=<forgescript> -DAPI=<forgescript-api.json> "-DSCRIPTS=<file|file|...>" -P ScriptNames.cmake

cmake_minimum_required(VERSION 3.24)

set(checked 0)
set(failures "")
string(REPLACE "|" ";" SCRIPTS "${SCRIPTS}")
foreach(script IN LISTS SCRIPTS)
	file(READ "${script}" text)
	if(text MATCHES "(^|\n)# Takes built-in names on purpose\\.")
		continue()
	endif()
	get_filename_component(folder "${script}" DIRECTORY)
	execute_process(COMMAND "${FORGESCRIPT}" --check --api "${API}" "${script}"
		WORKING_DIRECTORY "${folder}" OUTPUT_VARIABLE out ERROR_VARIABLE err)
	math(EXPR checked "${checked} + 1")
	if("${out}${err}" MATCHES "takes the name of the built-in|hides the built-in")
		string(STRIP "${out}${err}" said)
		list(APPEND failures "${script}:\n${said}")
	elseif(script MATCHES "/Projects/|/Benchmarks/")
		execute_process(COMMAND "${FORGESCRIPT}" --check --strict --api "${API}" "${script}"
			WORKING_DIRECTORY "${folder}" RESULT_VARIABLE strictCode OUTPUT_VARIABLE out ERROR_VARIABLE err)
		if(NOT strictCode EQUAL 0)
			string(STRIP "${out}${err}" said)
			list(APPEND failures "${script} (a sample or benchmark must have no warnings):\n${said}")
		endif()
	endif()
endforeach()

if(checked EQUAL 0)
	message(FATAL_ERROR "No scripts were found to check.")
endif()
if(failures)
	list(JOIN failures "\n\n" report)
	message(FATAL_ERROR "These scripts use a built-in or engine name for their own, or are samples with warnings:\n\n${report}")
endif()
message(STATUS "${checked} scripts checked: none takes a built-in or engine name, and the samples have no warnings.")
