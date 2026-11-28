#pragma once

#include "Bellows/Scene/Components.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace Bellows {

	// "point", "sphere", "box"; "additive", "alpha" (how scenes and scripts name them). Unknown text: none.
	const char* ParticleShapeToString(ParticleShape shape);
	std::optional<ParticleShape> ParticleShapeFromString(std::string_view text);
	const char* ParticleBlendToString(ParticleBlend blend);
	std::optional<ParticleBlend> ParticleBlendFromString(std::string_view text);

	// Particle emitters on the CPU: the scene calls Update once a frame for each emitter; scripts call
	// Play, Stop and Emit. Each emitter keeps its own random state, so a run is the same every time.
	namespace ParticleSystem {

		// Forgets every particle and the emitter's progress, as a fresh start of play needs. 'seed'
		// picks the emitter's random sequence (the scene passes the entity's UUID, so emitters differ).
		void Reset(ParticleEmitterComponent& emitter, uint64_t seed);

		// Picks the emitter's random sequence from 'seed' and leaves everything else as it is.
		void Seed(ParticleEmitterComponent& emitter, uint64_t seed);

		// Starts a cycle (sending its Burst), whether or not it was playing.
		void Play(ParticleEmitterComponent& emitter);
		// Stops sending new particles. With 'clear', the live ones go too; otherwise they live out their lives.
		void Stop(ParticleEmitterComponent& emitter, bool clear);
		// Sends 'count' particles at the next update, playing or not.
		void Emit(ParticleEmitterComponent& emitter, uint32_t count);

		// Ages and moves the live particles by 'seconds', removes the ones that died and sends new
		// ones. 'transform' is the emitter's world transform; 'gravity' the scene's (m/s²).
		void Update(ParticleEmitterComponent& emitter, const glm::mat4& transform, const glm::vec3& gravity, float seconds);

		// A particle's size (meters) and color (with Intensity applied to RGB) at its age.
		float GetSize(const ParticleEmitterComponent& emitter, const Particle& particle);
		glm::vec4 GetColor(const ParticleEmitterComponent& emitter, const Particle& particle);

	}

}
