# Tests 'forgescript --check' the way editors and MageCap Forge call it: exact output and exit codes.
# Run by ctest: cmake -DFORGESCRIPT=<path to forgescript> -DFOLDER=<this folder> -P CheckCommand.cmake

function(expect_run name expected_code expected_output)
	cmake_parse_arguments(RUN "" "INPUT" "ARGS" ${ARGN})
	if(RUN_INPUT)
		execute_process(COMMAND "${FORGESCRIPT}" ${RUN_ARGS} INPUT_FILE "${RUN_INPUT}"
			RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
	else()
		execute_process(COMMAND "${FORGESCRIPT}" ${RUN_ARGS}
			RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
	endif()
	if(NOT code EQUAL expected_code)
		message(FATAL_ERROR "${name}: exit code ${code}, expected ${expected_code}\nstdout: ${out}\nstderr: ${err}")
	endif()
	# Windows writes line breaks as \r\n; compare the text, not the line endings.
	string(REPLACE "\r\n" "\n" all "${out}${err}")
	string(STRIP "${all}" all)
	if(NOT all STREQUAL expected_output)
		message(FATAL_ERROR "${name}: output was\n${all}\nexpected\n${expected_output}")
	endif()
	message(STATUS "${name}: ok")
endfunction()

set(mistakes "${FOLDER}/mistakes.forge")
set(good "${FOLDER}/good.forge")

expect_run("text mistakes" 65
	"mistakes.forge:2:5: 'to' is a ForgeScript keyword and cannot be used as a variable name.\nmistakes.forge:4:9: Expected a value here, but found '*'."
	ARGS --check "${mistakes}")

expect_run("json mistakes" 65
	"{\"file\":\"${mistakes}\",\"diagnostics\":[{\"line\":2,\"column\":5,\"endColumn\":9,\"severity\":\"error\",\"message\":\"'to' is a ForgeScript keyword and cannot be used as a variable name.\"},{\"line\":4,\"column\":9,\"endColumn\":12,\"severity\":\"error\",\"message\":\"Expected a value here, but found '*'.\"}]}"
	ARGS --check --json "${mistakes}")

expect_run("text no mistakes" 0 "${good}: no mistakes found." ARGS --check "${good}")

# Warnings never change the exit code; they go to standard error as "place: warning: ...".
# (expect_run joins standard output before standard error.)
set(warns "${FOLDER}/warns.forge")
expect_run("text warnings" 0
	"${warns}: no mistakes found.\nwarns.forge:1:5: warning: Naming a value 'max' hides the built-in function 'max', so max(...) on line 2 won't call it. Pick another name, like 'max_value'."
	ARGS --check "${warns}")
expect_run("json warnings" 0
	"{\"file\":\"${warns}\",\"diagnostics\":[{\"line\":1,\"column\":5,\"endColumn\":14,\"severity\":\"warning\",\"message\":\"Naming a value 'max' hides the built-in function 'max', so max(...) on line 2 won't call it. Pick another name, like 'max_value'.\"}]}"
	ARGS --check --json "${warns}")
expect_run("json no mistakes" 0 "{\"file\":\"${good}\",\"diagnostics\":[]}" ARGS --check --json "${good}")

# --api gives --check the engine's phrases and functions, as --lsp gets them: "hide menu" is an engine
# phrase, not a mistake, and "hide (menu)" gets the bracket warning.
set(menu "${FOLDER}/menu.forge")
set(api "${FOLDER}/../LSP/test-api.json")
expect_run("check with api" 0
	"${menu}: no mistakes found.\nmenu.forge:2:1: warning: With brackets, 'hide (...)' is a call to a function named 'hide', not the 'hide' phrase. To use the phrase, leave the brackets out, like: hide menu"
	ARGS --check --api "${api}" "${menu}")
expect_run("check with a missing api" 66
	"Could not read the engine API description '${FOLDER}/no-such-api.json'."
	ARGS --check --api "${FOLDER}/no-such-api.json" "${menu}")
expect_run("api without lsp or check" 64 "--api works together with --lsp or --check." ARGS --api "${api}" "${menu}")

# With --stdin, --name says where the text belongs, so "use" finds cheers.forge next to it.
expect_run("stdin with name" 0 "{\"file\":\"${FOLDER}/unsaved.forge\",\"diagnostics\":[]}"
	ARGS --check --json --stdin --name "${FOLDER}/unsaved.forge" INPUT "${good}")
expect_run("stdin in another folder" 65
	"unsaved.forge:2:5: Could not find the script \"cheers\" to use.\nunsaved.forge:3:7: Expected the end of the line, but found '\"team\"'."
	ARGS --check --stdin --name "${FOLDER}/elsewhere/unsaved.forge" INPUT "${good}")

expect_run("missing file" 66 "Could not open '${FOLDER}/missing.forge'." ARGS --check "${FOLDER}/missing.forge")
expect_run("json without check" 64 "--json and --stdin work together with --check." ARGS --json "${good}")
expect_run("name without path" 64 "--name needs the script's path after it, like: --name Scripts/player.forge" ARGS --check --name)

# Inside a MageCap Bellows project, "use" starts at the asset folder named in the .bproject file,
# like the engine, so Content/Scripts/Main.forge can say use "Scripts/Helpers".
set(project "${FOLDER}/Project")
expect_run("project use" 0 "${project}/Content/Scripts/Main.forge: no mistakes found."
	ARGS --check "${project}/Content/Scripts/Main.forge")
expect_run("project stdin" 0 "{\"file\":\"${project}/Content/Scripts/Unsaved.forge\",\"diagnostics\":[]}"
	ARGS --check --json --stdin --name "${project}/Content/Scripts/Unsaved.forge" INPUT "${project}/Content/Scripts/Main.forge")
expect_run("project run" 0 "42" ARGS "${project}/Content/Scripts/Main.forge")

# The version: tools and MageCap Forge read it from --version, and CHANGELOG.md's newest entry must
# describe it, so a version bump without its changelog entry (or the other way round) fails here.
execute_process(COMMAND "${FORGESCRIPT}" --version RESULT_VARIABLE code OUTPUT_VARIABLE out)
string(STRIP "${out}" out)
if(NOT code EQUAL 0 OR NOT out MATCHES "^ForgeScript ([0-9]+\\.[0-9]+\\.[0-9]+)$")
	message(FATAL_ERROR "forgescript --version printed '${out}' (exit ${code}), expected 'ForgeScript <major>.<minor>.<patch>'")
endif()
set(version "${CMAKE_MATCH_1}")
file(STRINGS "${FOLDER}/../../CHANGELOG.md" headings REGEX "^## [0-9]+\\.[0-9]+\\.[0-9]+")
list(GET headings 0 newest)
if(NOT newest MATCHES "^## ${version} ")
	message(FATAL_ERROR "CHANGELOG.md's newest entry is '${newest}', but forgescript is version ${version}")
endif()
