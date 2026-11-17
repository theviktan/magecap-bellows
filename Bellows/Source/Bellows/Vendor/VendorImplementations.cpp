// Implementation units for single-header third-party libraries.
// Compiled without first-party warning flags (see Bellows/CMakeLists.txt).

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

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
