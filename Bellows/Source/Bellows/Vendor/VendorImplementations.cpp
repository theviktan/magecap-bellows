// Implementation units for single-header third-party libraries.
// Compiled without first-party warning flags (see Bellows/CMakeLists.txt).

// Only the formats Bellows reads (textures, environments, glTF's embedded images, the window icon):
// less decoder code for a bad file to reach. No image wider or taller than the GPU's usual limit.
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_HDR
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// OpenEXR images; ZIP-compressed EXR uses stb's zlib (above) instead of bundling miniz.
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_USE_THREAD 0
#define TINYEXR_USE_OPENMP 0
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// miniaudio decodes Ogg Vorbis only through stb_vorbis: its declarations come before miniaudio and
// its implementation after it.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

// Atlas packing and CFF glyph outlines for game text (Text/Font.cpp, Text/FontFile.cpp); Dear ImGui keeps its own
// private copies. The packer comes first, or stb_truetype declares a stand-in for it.
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

// Bellows reads font files itself (Text/FontFile.cpp) and uses stb_truetype only to run CFF
// charstrings, through buffers of each table's real size. Damaged charstrings reach stb_truetype's
// asserts, whose reads already fall back safely (to 0, or an empty buffer); as asserts they would
// stop a debug build over a bad file instead.
#define STBTT_assert(x) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
