# Gives a Windows program the MageCap Bellows icon (Resources/Icons/Bellows.ico), which Explorer, the
# taskbar and shortcuts show. Elsewhere the window icon is set at run time from Bellows.png.
function(bellows_add_app_icon target)
	if(NOT WIN32)
		return()
	endif()
	set(BELLOWS_APP_ICON "${PROJECT_SOURCE_DIR}/Resources/Icons/Bellows.ico")
	set(resourceFile "${PROJECT_BINARY_DIR}/Generated/${target}Icon.rc")
	# The first icon resource is the one Windows shows for the program. Written only when it changes.
	file(CONFIGURE OUTPUT "${resourceFile}" CONTENT "1 ICON \"@BELLOWS_APP_ICON@\"\n" @ONLY)
	target_sources(${target} PRIVATE "${resourceFile}")
	set_property(SOURCE "${resourceFile}" APPEND PROPERTY OBJECT_DEPENDS "${BELLOWS_APP_ICON}")
endfunction()
