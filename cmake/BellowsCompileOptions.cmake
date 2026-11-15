# Shared compiler settings for every first-party MageCap Bellows target.

function(bellows_set_compile_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP /Zc:__cplusplus /Zc:preprocessor /wd4100 /wd4201)
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
	endif()

	if(BELLOWS_WARNINGS_AS_ERRORS)
		if(MSVC)
			target_compile_options(${target} PRIVATE /WX)
		else()
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()

	target_compile_definitions(${target} PRIVATE
		$<$<CONFIG:Debug>:BW_DEBUG>
		$<$<CONFIG:Release>:BW_RELEASE>
		$<$<CONFIG:RelWithDebInfo>:BW_RELEASE>
		$<$<CONFIG:Dist>:BW_DIST>)
endfunction()

option(BELLOWS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in first-party code" ON)

# Windows executables load the Python DLL at startup, so it is copied next to them.
function(bellows_copy_python_runtime target)
	if(WIN32)
		get_filename_component(pythonDirectory "${Python_EXECUTABLE}" DIRECTORY)
		file(GLOB pythonDlls "${pythonDirectory}/python3*.dll")
		foreach(dll ${pythonDlls})
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>"
				VERBATIM)
		endforeach()
	endif()
endfunction()
