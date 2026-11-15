// Descriptor layout shared by all fullscreen passes:
// binding 0 = pass constants, 1-4 = input textures, 5 = linear clamp sampler, 6 = point clamp sampler.

#ifndef BELLOWS_POST_PROCESS_GLSL
#define BELLOWS_POST_PROCESS_GLSL

#include "Common.glsl"

layout(set = 0, binding = 5) uniform sampler s_LinearClamp;
layout(set = 0, binding = 6) uniform sampler s_PointClamp;

#endif
