# Compiles GLSL shaders to SPIR-V at build time with glslangValidator.
#
# glslangValidator ships with the LunarG Vulkan SDK on Windows and with the
# 'glslang-tools' package on Ubuntu.

find_program(BELLOWS_GLSLANG_VALIDATOR
	NAMES glslangValidator
	HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
	REQUIRED)

# bellows_compile_shaders(<target> OUTPUT_DIR <dir> INCLUDE_DIR <dir> SOURCES <files...>)
function(bellows_compile_shaders target)
	cmake_parse_arguments(ARG "" "OUTPUT_DIR;INCLUDE_DIR" "SOURCES" ${ARGN})

	file(GLOB includeFiles CONFIGURE_DEPENDS "${ARG_INCLUDE_DIR}/*.glsl")

	set(outputs "")
	foreach(source ${ARG_SOURCES})
		get_filename_component(fileName "${source}" NAME)
		set(output "${ARG_OUTPUT_DIR}/${fileName}.spv")
		add_custom_command(
			OUTPUT "${output}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUTPUT_DIR}"
			COMMAND "${BELLOWS_GLSLANG_VALIDATOR}" -V --target-env vulkan1.3 "-I${ARG_INCLUDE_DIR}" -o "${output}" "${source}"
			DEPENDS "${source}" ${includeFiles}
			COMMENT "Compiling shader ${fileName}"
			VERBATIM)
		list(APPEND outputs "${output}")
	endforeach()

	add_custom_target(${target} ALL DEPENDS ${outputs} SOURCES ${ARG_SOURCES} ${includeFiles})
	set_target_properties(${target} PROPERTIES FOLDER "Shaders")
endfunction()
