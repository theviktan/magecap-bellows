# The ForgeScript language as a static library. Link this into the engine and the editor.
add_library(ForgeScript STATIC
	src/ForgeScript/ForgeScript.h
	src/ForgeScript/API/Native.h
	src/ForgeScript/Compiler/Compiler.cpp
	src/ForgeScript/Compiler/Compiler.h
	src/ForgeScript/Compiler/Lexer.cpp
	src/ForgeScript/Compiler/Lexer.h
	src/ForgeScript/Compiler/Phrase.cpp
	src/ForgeScript/Compiler/Phrase.h
	src/ForgeScript/Core/Chunk.h
	src/ForgeScript/Core/Object.cpp
	src/ForgeScript/Core/Object.h
	src/ForgeScript/Core/Utf8.h
	src/ForgeScript/Core/Value.cpp
	src/ForgeScript/Core/Value.h
	src/ForgeScript/VM/Library.cpp
	src/ForgeScript/VM/Task.h
	src/ForgeScript/VM/VM.cpp
	src/ForgeScript/VM/VM.h
)

target_include_directories(ForgeScript PUBLIC src)
target_compile_definitions(ForgeScript PUBLIC FORGESCRIPT_VERSION="${PROJECT_VERSION}")
forge_set_warnings(ForgeScript)
