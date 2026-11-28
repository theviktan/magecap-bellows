#pragma once

#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"

#include <glm/glm.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace Bellows {

	// "ring", "cylinder", "cone", "sphere", "dome" (how scenes and scripts name them). Unknown text: none.
	const char* EffectShapeTypeToString(EffectShapeType shape);
	std::optional<EffectShapeType> EffectShapeTypeFromString(std::string_view text);

	// Effect shapes: the scene advances each one's cycle once a frame (Scene::UpdateEffectShapes) and
	// draws BuildMesh's triangles with BuildMaterial's look.
	namespace EffectShapeSystem {

		// Runs the cycle, the spin and the texture's scroll on by 'seconds' while it plays. A cycle
		// that ends without looping finishes the shape (hidden until played again).
		void Update(EffectShapeComponent& shape, float seconds);
		// From the start of its cycle, shown and playing.
		void Play(EffectShapeComponent& shape);
		// Hidden and back at the start, until played again.
		void Stop(EffectShapeComponent& shape);

		// How far into its cycle it is: 0 at the start, 1 at the end.
		float CycleProgress(const EffectShapeComponent& shape);

		// The surface in world space for an entity at 'world', at its point in the cycle (size curve
		// and spin): U runs around it, V across it. Nothing when finished.
		void BuildMesh(const EffectShapeComponent& shape, const glm::mat4& world, std::vector<EffectVertex>& outVertices);
		// The look at its point in the cycle (color gradient, dissolve curve, scroll); the texture is
		// the caller's to add. Noise cells follow the shape's size so dissolve blobs are DissolveScale
		// per meter.
		EffectMaterial BuildMaterial(const EffectShapeComponent& shape, const glm::mat4& world);

	}

}
