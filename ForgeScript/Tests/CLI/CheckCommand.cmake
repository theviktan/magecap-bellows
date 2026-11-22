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
# A mistake whose message names the change carries it as "fix", for editors that read the JSON.
set(fixable "${FOLDER}/fixable.forge")
expect_run("json fix" 65
	"{\"file\":\"${fixable}\",\"diagnostics\":[{\"line\":2,\"column\":11,\"endColumn\":14,\"severity\":\"error\",\"message\":\"Use 'to' instead of '=', like: set health to 100\",\"fix\":{\"title\":\"Change '=' to 'to'\",\"line\":2,\"column\":11,\"endLine\":2,\"endColumn\":12,\"newText\":\"to\"}}]}"
	ARGS --check --json "${fixable}")

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

# --test runs the script, then its tests: PASS or FAIL (with the reason, indented), a summary, and
# exit code 1 when one failed. A plain run doesn't run tests.
set(tests "${FOLDER}/tests.forge")
expect_run("tests" 1
	"PASS double works\nFAIL a failing one\n    Error: Expected double(2) is 5, but double(2) is 4.\n        in 'test \"a failing one\"' (tests.forge line 10)\nPASS waits\n\n3 tests: 2 passed, 1 failed."
	ARGS --test "${tests}")
expect_run("tests don't run on a plain run" 0 "" ARGS "${tests}")
expect_run("no tests" 0 "Go team!\nNo tests in good.forge. Write one like:\ntest \"adding works\"\n\texpect 1 + 1 is 2\nend" ARGS --test "${good}")
expect_run("test with check" 64 "--test runs on its own: use it without --check or --realtime." ARGS --test --check "${tests}")

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

# --format: lays a script out (only whitespace at line starts and ends changes); --check only tells.
set(messy "${FOLDER}/messy.forge")
set(laidOut "function greet(name)\n\tsay \"Hi {name}\"\n\tif name is \"Ron\" then\n\t\tsay \"Welcome back\"\n\tend\nend\n\n\ngreet(\"Ron\")")
expect_run("format to stdout" 0 "${laidOut}" ARGS --format --stdin INPUT "${messy}")
expect_run("format check, not laid out" 1 "${messy}: not formatted. Run: forgescript --format ${messy}" ARGS --format --check "${messy}")
set(copy "${CMAKE_CURRENT_BINARY_DIR}/format-copy.forge")
file(COPY_FILE "${messy}" "${copy}")
expect_run("format a file" 0 "${copy}: formatted." ARGS --format "${copy}")
file(READ "${copy}" formatted)
string(STRIP "${formatted}" formatted)
if(NOT formatted STREQUAL laidOut)
	message(FATAL_ERROR "format a file: the file holds\n${formatted}")
endif()
expect_run("format a laid-out file" 0 "${copy}: already formatted." ARGS --format "${copy}")
expect_run("format check, laid out" 0 "${copy}: already formatted." ARGS --format --check "${copy}")
file(REMOVE "${copy}")
expect_run("format unbalanced" 65 "${FOLDER}/unbalanced.forge:1: A block is missing its 'end', so the script was left as it is."
	ARGS --format "${FOLDER}/unbalanced.forge")
expect_run("format missing file" 66 "Could not open '${FOLDER}/missing.forge'." ARGS --format "${FOLDER}/missing.forge")
expect_run("format with json" 64 "--format works on its own, with --check or --stdin (and --name)." ARGS --format --json "${messy}")

# --profile: the script runs (its output first), then the table of where its instructions went.
set(profiled "${FOLDER}/profile.forge")
set(profileTable "Profile of profile.forge: 68 instructions in script code\n  instructions       %      calls  function\n            56   82.4%          1  (top of the script) (profile.forge line 1)\n            12   17.6%          3  square (profile.forge line 2)")
expect_run("profile" 0 "14\n${profileTable}" ARGS --profile "${profiled}")
set(profileFile "${CMAKE_CURRENT_BINARY_DIR}/profile-output.txt")
expect_run("profile to a file" 0 "14\nProfile written to ${profileFile}" ARGS --profile "${profiled}" --output "${profileFile}")
file(READ "${profileFile}" written)
string(STRIP "${written}" written)
if(NOT written STREQUAL profileTable)
	message(FATAL_ERROR "profile to a file: the file holds\n${written}")
endif()
file(REMOVE "${profileFile}")
expect_run("output without profile" 64 "--profile takes one script, and --output only goes with --profile, like: forgescript --profile game.forge --output profile.txt"
	ARGS --output "${profileFile}" "${profiled}")
