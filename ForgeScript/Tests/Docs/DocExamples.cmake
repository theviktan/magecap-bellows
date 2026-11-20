# Checks every ForgeScript example in the docs with 'forgescript --check' and the engine API, so a
# guide never teaches code the checker refuses. Run by ctest:
#   cmake -DFORGESCRIPT=<forgescript> -DAPI=<forgescript-api.json> -DOUT=<scratch folder>
#         -DDOCS=<file|file|...> -P DocExamples.cmake
#
# An example is a block that starts with a ```forge line and ends with a ``` line. Examples with a
# 'use' line are left out: the scripts they use exist only in the reader's project.

cmake_minimum_required(VERSION 3.24)

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

set(checked 0)
set(failures "")
string(REPLACE "|" ";" DOCS "${DOCS}")
foreach(doc IN LISTS DOCS)
	file(READ "${doc}" text)
	string(REPLACE "\r\n" "\n" text "${text}")
	get_filename_component(docName "${doc}" NAME)
	get_filename_component(docStem "${doc}" NAME_WE)
	set(rest "${text}")
	set(offset 0)
	while(TRUE)
		string(FIND "${rest}" "```forge\n" start)
		if(start EQUAL -1)
			break()
		endif()
		# The line the example starts on, for the message.
		math(EXPR before "${offset} + ${start}")
		string(SUBSTRING "${text}" 0 ${before} head)
		string(REGEX MATCHALL "\n" breaks "${head}")
		list(LENGTH breaks line)
		math(EXPR line "${line} + 1")

		math(EXPR codeStart "${start} + 9")
		string(SUBSTRING "${rest}" ${codeStart} -1 rest)
		math(EXPR offset "${offset} + ${codeStart}")
		string(FIND "${rest}" "\n```" end)
		if(end EQUAL -1)
			list(APPEND failures "${docName}:${line}: the example has no closing ```")
			break()
		endif()
		math(EXPR codeLength "${end} + 1")
		string(SUBSTRING "${rest}" 0 ${codeLength} code)
		math(EXPR skip "${end} + 4")
		string(SUBSTRING "${rest}" ${skip} -1 rest)
		math(EXPR offset "${offset} + ${skip}")

		if(code MATCHES "(^|\n)[ \t]*use \"")
			continue()
		endif()
		set(file "${OUT}/${docStem}_${line}.forge")
		file(WRITE "${file}" "${code}")
		execute_process(COMMAND "${FORGESCRIPT}" --check --api "${API}" "${file}"
			RESULT_VARIABLE exitCode OUTPUT_VARIABLE out ERROR_VARIABLE err)
		math(EXPR checked "${checked} + 1")
		if(NOT exitCode EQUAL 0)
			string(STRIP "${out}${err}" said)
			list(APPEND failures "${docName}:${line}: ${said}")
		endif()
	endwhile()
endforeach()

if(checked LESS 50)
	message(FATAL_ERROR "Only ${checked} examples were found; the docs or this test's patterns have moved")
endif()
if(failures)
	list(JOIN failures "\n" all)
	message(FATAL_ERROR "Examples in the docs that forgescript --check refuses:\n${all}")
endif()
message(STATUS "${checked} examples in the docs pass forgescript --check")
