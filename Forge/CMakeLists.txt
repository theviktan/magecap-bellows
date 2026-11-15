# The Forge language as a static library. Link this into the engine and the editor.
add_library(Forge STATIC
	src/Forge/Forge.h
	src/Forge/API/Native.h
	src/Forge/Compiler/Compiler.cpp
	src/Forge/Compiler/Compiler.h
	src/Forge/Compiler/Lexer.cpp
	src/Forge/Compiler/Lexer.h
	src/Forge/Compiler/Phrase.cpp
	src/Forge/Compiler/Phrase.h
	src/Forge/Core/Chunk.h
	src/Forge/Core/Object.cpp
	src/Forge/Core/Object.h
	src/Forge/Core/Utf8.h
	src/Forge/Core/Value.cpp
	src/Forge/Core/Value.h
	src/Forge/VM/Library.cpp
	src/Forge/VM/Task.h
	src/Forge/VM/VM.cpp
	src/Forge/VM/VM.h
)

target_include_directories(Forge PUBLIC src)
target_compile_definitions(Forge PUBLIC FORGE_VERSION="${PROJECT_VERSION}")
forge_set_warnings(Forge)
