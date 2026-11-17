# Gives a Windows program an icon (by default the MageCap Bellows icon, Resources/Icons/Bellows.ico),
# which Explorer, the taskbar and shortcuts show. Elsewhere the window icon is set at run time.
#
#   bellows_add_app_icon(BellowsEditor)
#   bellows_add_app_icon(ForgeScriptCLI "${PROJECT_SOURCE_DIR}/ForgeScript/Icons/ForgeScript.ico")
function(bellows_add_app_icon target)
	if(NOT WIN32)
		return()
	endif()
	if(ARGC GREATER 1)
		set(BELLOWS_APP_ICON "${ARGV1}")
	else()
		set(BELLOWS_APP_ICON "${PROJECT_SOURCE_DIR}/Resources/Icons/Bellows.ico")
	endif()
	if(NOT EXISTS "${BELLOWS_APP_ICON}")
		message(FATAL_ERROR "bellows_add_app_icon(${target}): '${BELLOWS_APP_ICON}' does not exist")
	endif()
	set(resourceFile "${PROJECT_BINARY_DIR}/Generated/${target}Icon.rc")
	# The first icon resource is the one Windows shows for the program. Written only when it changes.
	file(CONFIGURE OUTPUT "${resourceFile}" CONTENT "1 ICON \"@BELLOWS_APP_ICON@\"\n" @ONLY)
	target_sources(${target} PRIVATE "${resourceFile}")
	set_property(SOURCE "${resourceFile}" APPEND PROPERTY OBJECT_DEPENDS "${BELLOWS_APP_ICON}")
endfunction()
