# Runs 'forgescript --lsp' as an editor would, over standard input and output, with a recorded session.
# Run by ctest: cmake -DFORGESCRIPT=<path to forgescript> -DFOLDER=<this folder> -P LspCommand.cmake

execute_process(COMMAND "${FORGESCRIPT}" --lsp --api "${FOLDER}/test-api.json"
	INPUT_FILE "${FOLDER}/session.lsp" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
	message(FATAL_ERROR "forgescript --lsp exited with ${code}, expected 0 after shutdown and exit\nstdout: ${out}\nstderr: ${err}")
endif()

foreach(expected
		"\"id\":1,\"result\":{\"capabilities\""
		"\"method\":\"textDocument/publishDiagnostics\""
		"\"message\":\"'to' is a ForgeScript keyword and cannot be used as a variable name.\""
		"\"id\":2,\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"```forge\\nsave screenshot to (path) -> text"
		"\"id\":3,\"result\":null")
	string(FIND "${out}" "${expected}" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "forgescript --lsp output is missing:\n${expected}\nOutput:\n${out}")
	endif()
endforeach()

# Only one mistake: the engine phrase on line 1 is known from the API file.
string(REGEX MATCHALL "\"severity\":1" mistakes "${out}")
list(LENGTH mistakes count)
if(NOT count EQUAL 1)
	message(FATAL_ERROR "expected 1 diagnostic, got ${count}:\n${out}")
endif()

# Wrong usage is refused with exit code 64.
execute_process(COMMAND "${FORGESCRIPT}" --lsp game.forge RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 64)
	message(FATAL_ERROR "forgescript --lsp game.forge exited with ${code}, expected 64")
endif()
execute_process(COMMAND "${FORGESCRIPT}" --api x.json --check game.forge RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(NOT code EQUAL 64)
	message(FATAL_ERROR "forgescript --api without --lsp exited with ${code}, expected 64")
endif()
message(STATUS "forgescript --lsp: ok")
