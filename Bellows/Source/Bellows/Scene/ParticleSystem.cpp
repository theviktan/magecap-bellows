#include "bwpch.h"
#include "Bellows/Scene/ParticleSystem.h"

#include <glm/gtc/constants.hpp>

namespace Bellows {

	const char* ParticleShapeToString(ParticleShape shape)
	{
		switch (shape)
		{
			case ParticleShape::Point:	return "point";
			case ParticleShape::Sphere:	return "sphere";
			case ParticleShape::Box:	return "box";
		}
		return "point";
	}

	std::optional<ParticleShape> ParticleShapeFromString(std::string_view text)
	{
		if (text == "point") return ParticleShape::Point;
		if (text == "sphere") return ParticleShape::Sphere;
		if (text == "box") return ParticleShape::Box;
		return std::nullopt;
	}

	const char* ParticleBlendToString(ParticleBlend blend)
	{
		switch (blend)
		{
			case ParticleBlend::Additive:	return "additive";
			case ParticleBlend::Alpha:		return "alpha";
		}
		return "additive";
	}

	std::optional<ParticleBlend> ParticleBlendFromString(std::string_view text)
	{
		if (text == "additive") return ParticleBlend::Additive;
		if (text == "alpha") return ParticleBlend::Alpha;
		return std::nullopt;
	}

}

namespace Bellows::ParticleSystem {

	namespace {

		// The longest step one update takes, so a long hitch (a breakpoint, a slow load) doesn't throw
		// particles across the scene.
		constexpr float c_MaxStep = 0.25f;
		constexpr uint32_t c_DefaultSeed = 0x9E3779B9u;

		// xorshift32: small, fast and the same on every platform.
		uint32_t NextRandom(uint32_t& state)
		{
			uint32_t x = state;
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			state = x;
			return x;
		}

		// 0 to 1.
		float Random01(uint32_t& state)
		{
			return static_cast<float>(NextRandom(state) >> 8) * (1.0f / 16777216.0f);
		}

		// -1 to 1.
		float RandomSigned(uint32_t& state)
		{
			return Random01(state) * 2.0f - 1.0f;
		}

		glm::vec3 RandomUnitVector(uint32_t& state)
		{
			const float z = RandomSigned(state);
			const float angle = Random01(state) * glm::two_pi<float>();
			const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
			return glm::vec3(r * std::cos(angle), r * std::sin(angle), z);
		}

		glm::vec3 SpawnPosition(const ParticleEmitterComponent& emitter, uint32_t& state)
		{
			switch (emitter.Shape)
			{
				case ParticleShape::Sphere:
					return RandomUnitVector(state) * (emitter.ShapeSize.x * std::cbrt(Random01(state)));
				case ParticleShape::Box:
					return glm::vec3(RandomSigned(state), RandomSigned(state), RandomSigned(state)) * emitter.ShapeSize;
				case ParticleShape::Point:
					break;
			}
			return glm::vec3(0.0f);
		}

		// A direction in the cone of SpreadAngle around +Y, spread evenly over the cone's cap.
		glm::vec3 SpawnDirection(const ParticleEmitterComponent& emitter, uint32_t& state)
		{
			const float cosSpread = std::cos(glm::radians(emitter.SpreadAngle));
			const float cosTheta = 1.0f - Random01(state) * (1.0f - cosSpread);
			const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
			const float phi = Random01(state) * glm::two_pi<float>();
			return glm::vec3(sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi));
		}

		void Spawn(ParticleEmitterComponent& emitter, const glm::mat4& transform, uint32_t count)
		{
			const size_t room = emitter.MaxParticles > emitter.RuntimeParticles.size() ? emitter.MaxParticles - emitter.RuntimeParticles.size() : 0;
			count = static_cast<uint32_t>(std::min<size_t>(count, room));
			const glm::mat3 rotation(transform);
			for (uint32_t i = 0; i < count; i++)
			{
				uint32_t& state = emitter.RuntimeRandom;
				Particle particle;
				const glm::vec3 position = SpawnPosition(emitter, state);
				glm::vec3 direction = SpawnDirection(emitter, state);
				const float speed = emitter.Speed * (1.0f + emitter.SpeedVariation * RandomSigned(state));
				particle.Lifetime = std::max(ParticleEmitterComponent::MinLifetime,
					emitter.Lifetime * (1.0f + emitter.LifetimeVariation * RandomSigned(state)));
				if (emitter.WorldSpace)
				{
					particle.Position = glm::vec3(transform * glm::vec4(position, 1.0f));
					const glm::vec3 turned = rotation * direction;
					const float length = glm::length(turned);
					if (length > 1e-6f)
						direction = turned / length;
				}
				else
				{
					particle.Position = position;
				}
				particle.Velocity = direction * speed;
				emitter.RuntimeParticles.push_back(particle);
			}
		}

	}

	void Reset(ParticleEmitterComponent& emitter, uint64_t seed)
	{
		emitter.RuntimeParticles.clear();
		emitter.RuntimeStarted = false;
		emitter.RuntimePlaying = false;
		emitter.RuntimeTime = 0.0f;
		emitter.RuntimeEmitCarry = 0.0f;
		emitter.RuntimePending = 0;
		Seed(emitter, seed);
	}

	void Seed(ParticleEmitterComponent& emitter, uint64_t seed)
	{
		emitter.RuntimeSeed = seed;
		// Fold the 64-bit seed into a nonzero 32-bit state (xorshift never leaves 0).
		seed ^= seed >> 33;
		seed *= 0xFF51AFD7ED558CCDull;
		seed ^= seed >> 33;
		const uint32_t state = static_cast<uint32_t>(seed ^ (seed >> 32));
		emitter.RuntimeRandom = state != 0 ? state : c_DefaultSeed;
	}

	void Play(ParticleEmitterComponent& emitter)
	{
		emitter.RuntimeStarted = true;
		emitter.RuntimePlaying = true;
		emitter.RuntimeTime = 0.0f;
		emitter.RuntimeEmitCarry = 0.0f;
		Emit(emitter, emitter.Burst);
	}

	void Stop(ParticleEmitterComponent& emitter, bool clear)
	{
		emitter.RuntimeStarted = true;
		emitter.RuntimePlaying = false;
		emitter.RuntimePending = 0;
		emitter.RuntimeEmitCarry = 0.0f;
		if (clear)
			emitter.RuntimeParticles.clear();
	}

	void Emit(ParticleEmitterComponent& emitter, uint32_t count)
	{
		emitter.RuntimePending = std::min(emitter.RuntimePending + std::min(count, ParticleEmitterComponent::MaxMaxParticles),
			ParticleEmitterComponent::MaxMaxParticles);
	}

	void Update(ParticleEmitterComponent& emitter, const glm::mat4& transform, const glm::vec3& gravity, float seconds)
	{
		seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.0f, c_MaxStep) : 0.0f;
		if (emitter.RuntimeRandom == 0)
			emitter.RuntimeRandom = c_DefaultSeed;
		if (!emitter.RuntimeStarted)
		{
			emitter.RuntimeStarted = true;
			if (emitter.PlayOnStart)
				Play(emitter);
		}

		// Local-space particles feel gravity turned into the emitter's space, so on screen it still pulls down.
		glm::vec3 pull = gravity * emitter.GravityScale;
		if (!emitter.WorldSpace)
		{
			const glm::mat3 rotation(transform);
			pull = std::abs(glm::determinant(rotation)) > 1e-12f ? glm::inverse(rotation) * pull : glm::vec3(0.0f);
		}
		const float keep = std::exp(-emitter.Drag * seconds);
		std::erase_if(emitter.RuntimeParticles, [&](Particle& particle)
		{
			particle.Age += seconds;
			if (particle.Age >= particle.Lifetime)
				return true;
			particle.Velocity = (particle.Velocity + pull * seconds) * keep;
			particle.Position += particle.Velocity * seconds;
			return false;
		});

		uint32_t count = emitter.RuntimePending;
		emitter.RuntimePending = 0;
		if (emitter.RuntimePlaying)
		{
			const float active = emitter.Looping ? seconds : std::clamp(emitter.Duration - emitter.RuntimeTime, 0.0f, seconds);
			emitter.RuntimeEmitCarry += emitter.Rate * active;
			// A hair of slack, so steps that add up to a whole particle in decimal (0.1 s × 10 / s) don't
			// fall just short of it in floating point.
			const float whole = std::floor(emitter.RuntimeEmitCarry + 1e-4f);
			emitter.RuntimeEmitCarry = std::max(emitter.RuntimeEmitCarry - whole, 0.0f);
			count += static_cast<uint32_t>(std::min(whole, static_cast<float>(ParticleEmitterComponent::MaxMaxParticles)));

			emitter.RuntimeTime += seconds;
			if (emitter.Looping)
			{
				// Each new cycle sends its burst. A step is at most c_MaxStep and a cycle at least
				// MinDuration, so this runs a handful of times at most.
				const float duration = std::max(emitter.Duration, ParticleEmitterComponent::MinDuration);
				while (emitter.RuntimeTime >= duration)
				{
					emitter.RuntimeTime -= duration;
					count = std::min(count + emitter.Burst, ParticleEmitterComponent::MaxMaxParticles);
				}
			}
			else if (emitter.RuntimeTime >= emitter.Duration)
			{
				emitter.RuntimePlaying = false;
			}
		}
		Spawn(emitter, transform, count);
	}

	float GetSize(const ParticleEmitterComponent& emitter, const Particle& particle)
	{
		const float t = particle.Lifetime > 0.0f ? std::clamp(particle.Age / particle.Lifetime, 0.0f, 1.0f) : 1.0f;
		return glm::mix(emitter.StartSize, emitter.EndSize, t);
	}

	glm::vec4 GetColor(const ParticleEmitterComponent& emitter, const Particle& particle)
	{
		const float t = particle.Lifetime > 0.0f ? std::clamp(particle.Age / particle.Lifetime, 0.0f, 1.0f) : 1.0f;
		glm::vec4 color = glm::mix(emitter.StartColor, emitter.EndColor, t);
		color.r *= emitter.Intensity;
		color.g *= emitter.Intensity;
		color.b *= emitter.Intensity;
		return color;
	}

}
