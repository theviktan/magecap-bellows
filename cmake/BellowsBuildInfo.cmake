# The git commit the engine is built from, for the About box, Install.json, logs and package names,
# and the version text builds report.
#
# Only the release itself reports the plain version ("0.4.0"): a build of the commit tagged
# v<version>, or one configured with BELLOWS_RELEASE_BUILD (CI does that for version tags). Every
# other build is on its way to that version and says so ("0.4.0-dev"), so a report from it can't be
# mistaken for the release. The version in CMakeLists.txt moves to the next release when a cycle starts.
#
# It goes into one generated source file (Bellows/Core/BuildInfo.cpp), so a new commit recompiles that
# file only. CMake configures again when HEAD moves, so the commit stays current without a clean build.
# Set BELLOWS_BUILD_COMMIT to report a given commit instead (builds from a source archive).

set(BELLOWS_BUILD_COMMIT "" CACHE STRING "Commit to report instead of asking git (empty: ask git)")
option(BELLOWS_RELEASE_BUILD "This build is the release of the version in CMakeLists.txt (reports it without -dev)" OFF)

set(_bellows_commit "${BELLOWS_BUILD_COMMIT}")
find_package(Git QUIET)
if(NOT _bellows_commit AND GIT_FOUND)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
		WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
		OUTPUT_VARIABLE _bellows_commit
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
		RESULT_VARIABLE _bellows_git_result)
	if(NOT _bellows_git_result EQUAL 0)
		set(_bellows_commit "")
	else()
		# HEAD itself, the branch it points to, and packed refs: any of them changes with a new commit.
		foreach(_bellows_ref HEAD packed-refs)
			execute_process(
				COMMAND "${GIT_EXECUTABLE}" rev-parse --path-format=absolute --git-path ${_bellows_ref}
				WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
				OUTPUT_VARIABLE _bellows_ref_path
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET)
			if(_bellows_ref_path AND EXISTS "${_bellows_ref_path}")
				set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_bellows_ref_path}")
			endif()
		endforeach()
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
			WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
			OUTPUT_VARIABLE _bellows_branch
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET)
		if(_bellows_branch)
			execute_process(
				COMMAND "${GIT_EXECUTABLE}" rev-parse --path-format=absolute --git-path ${_bellows_branch}
				WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
				OUTPUT_VARIABLE _bellows_ref_path
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET)
			if(_bellows_ref_path AND EXISTS "${_bellows_ref_path}")
				set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_bellows_ref_path}")
			endif()
		endif()
	endif()
endif()
if(NOT _bellows_commit)
	set(_bellows_commit "unknown")
endif()

set(_bellows_release ${BELLOWS_RELEASE_BUILD})
if(NOT _bellows_release AND GIT_FOUND AND NOT _bellows_commit STREQUAL "unknown")
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" tag --points-at HEAD
		WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
		OUTPUT_VARIABLE _bellows_tags
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
	string(REPLACE "\n" ";" _bellows_tags "${_bellows_tags}")
	if("v${PROJECT_VERSION}" IN_LIST _bellows_tags)
		set(_bellows_release ON)
	endif()
endif()
if(_bellows_release)
	set(BELLOWS_VERSION_TEXT "${PROJECT_VERSION}")
else()
	set(BELLOWS_VERSION_TEXT "${PROJECT_VERSION}-dev")
endif()

set(BELLOWS_COMMIT "${_bellows_commit}")
configure_file("${PROJECT_SOURCE_DIR}/Bellows/Source/Bellows/Core/BuildInfo.cpp.in"
	"${PROJECT_BINARY_DIR}/Generated/BuildInfo.cpp" @ONLY)
message(STATUS "MageCap Bellows ${BELLOWS_VERSION_TEXT}, commit ${BELLOWS_COMMIT}")
