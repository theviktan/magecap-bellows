---
name: bellows-renderer-change
description: Change the MageCap Bellows renderer or shaders (new pass, shader edit, post effect, render setting) safely, with the conventions NVRHI/Vulkan require here. Use for any work in Bellows/Source/Bellows/Renderer or Bellows/Shaders.
---

# Changing the renderer

## Know the frame (SceneRenderer::EndScene)
1. Cascaded shadow maps (directional light, PCSS soft shadows).
2. Depth/normal/entity-ID prepass.
3. SSAO + bilateral blur (half resolution by default).
4. Forward PBR (opaque, depth EQUAL) with image-based lighting from the `Environment`.
5. Skybox, then the transparent pass (sorted back to front).
6. Bloom (downsample chain + tent upsample), tonemapping (ACES/AgX/Reinhard), vignette, dither.
7. FXAA, then editor overlays (grid, debug lines, selection outline).

## Conventions that bite
- Clip space is Y-up, depth 0..1; projections are **not** flipped (NVRHI flips the viewport).
  Front faces are counter-clockwise. Fullscreen UV (0,0) = top-left; use `UVToNDC`/`NDCToUV`.
- Binding layouts: descriptor set = register space (`setRegisterSpaceAndDescriptorSet`);
  volatile constant buffers need `VolatileConstantBuffer` layout items; push constants use slot
  `c_PushConstantSlot`. Keep `ShaderInterop.h` structs in sync with GLSL (std140, static_asserts).
- Shaders include `Include/*.glsl` with `GL_GOOGLE_include_directive`; every new shader is picked
  up by `cmake/BellowsShaders.cmake` automatically and fails the build on errors.
- New settings go in `SceneRendererSettings` (serialized with the scene) and the Settings panel.

## Verify
1. Add or extend a `[gpu]` test in `BellowsTests/Source/RendererTests.cpp` that renders a small
   scene, reads it back (`Renderer::ReadbackTexture`) and checks pixels; save the image to
   `TestOutput/` and look at it.
2. The GPU tests fail on any Vulkan validation message - keep them clean.
3. Look at the FeatureTest scene: `BellowsRuntime --project Projects/FeatureTest --headless --frames 120 --screenshot out.png`.
4. Check performance-sensitive changes on a real GPU in a Release build, not only on lavapipe.
