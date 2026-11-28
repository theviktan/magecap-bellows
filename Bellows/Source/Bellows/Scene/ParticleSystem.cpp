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
			case ParticleShape::Ring:	return "ring";
			case ParticleShape::Line:	return "line";
		}
		return "point";
	}

	std::optional<ParticleShape> ParticleShapeFromString(std::string_view text)
	{
		if (text == "point") return ParticleShape::Point;
		if (text == "sphere") return ParticleShape::Sphere;
		if (text == "box") return ParticleShape::Box;
		if (text == "ring") return ParticleShape::Ring;
		if (text == "line") return ParticleShape::Line;
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

	const char* ParticleFacingToString(ParticleFacing facing)
	{
		switch (facing)
		{
			case ParticleFacing::Camera:	return "camera";
			case ParticleFacing::Stretched:	return "stretched";
			case ParticleFacing::Flat:		return "flat";
		}
		return "camera";
	}

	std::optional<ParticleFacing> ParticleFacingFromString(std::string_view text)
	{
		if (text == "camera") return ParticleFacing::Camera;
		if (text == "stretched") return ParticleFacing::Stretched;
		if (text == "flat") return ParticleFacing::Flat;
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
				case ParticleShape::Ring:
				{
					const float angle = Random01(state) * glm::two_pi<float>();
					return glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * emitter.ShapeSize.x;
				}
				case ParticleShape::Line:
					return glm::vec3(RandomSigned(state) * emitter.ShapeSize.x * 0.5f, 0.0f, 0.0f);
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

		// 'v' (given around +Y) turned so +Y points along the unit vector 'axis'.
		glm::vec3 TurnFromUp(const glm::vec3& v, const glm::vec3& axis)
		{
			if (axis.y > 0.9999f)
				return v;
			if (axis.y < -0.9999f)
				return glm::vec3(v.x, -v.y, -v.z);
			const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), axis));
			const glm::vec3 forward = glm::cross(side, axis);
			return side * v.x + axis * v.y + forward * v.z;
		}

		// Which way 'Outward' sends a particle born at 'position' (local): away from the center, or
		// anywhere when it's born at the center.
		glm::vec3 OutwardAxis(const ParticleEmitterComponent& emitter, const glm::vec3& position, uint32_t& state)
		{
			glm::vec3 away = position;
			if (emitter.Shape == ParticleShape::Line)
				away = glm::vec3(position.x, 0.0f, 0.0f);
			const float length = glm::length(away);
			return length > 1e-5f ? away / length : RandomUnitVector(state);
		}

		// A smooth, swirling push that changes over time: three sine waves per axis at unrelated
		// angles, so it doesn't look like a grid. -1 to 1 on each axis, roughly.
		glm::vec3 Swirl(const glm::vec3& p, float time, float phase)
		{
			const glm::vec3 a = p + glm::vec3(phase, phase * 1.7f, phase * 2.3f);
			return glm::vec3(
				std::sin(a.y * 1.31f + time * 1.10f) + std::sin(a.z * 1.73f - time * 0.70f + 1.7f),
				std::sin(a.z * 1.19f + time * 0.90f + 2.9f) + std::sin(a.x * 1.57f - time * 1.30f + 0.4f),
				std::sin(a.x * 1.43f + time * 0.80f + 4.1f) + std::sin(a.y * 1.91f - time * 1.20f + 3.3f)) * 0.5f;
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
				if (emitter.Outward)
					direction = TurnFromUp(direction, OutwardAxis(emitter, position, state));
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
				particle.Rotation = glm::radians(emitter.StartRotation + emitter.RotationVariation * RandomSigned(state));
				particle.Spin = glm::radians(emitter.SpinSpeed + emitter.SpinVariation * RandomSigned(state));
				particle.Random = Random01(state);
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
		emitter.RuntimeClock = 0.0f;
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
		emitter.RuntimeClock = std::fmod(emitter.RuntimeClock + seconds * emitter.NoiseSpeed, 10000.0f);
		const bool swirl = emitter.NoiseStrength > 0.0f;
		// Each emitter's swirls are its own, from its seed.
		const float phase = static_cast<float>(emitter.RuntimeSeed % 1000u) * 0.37f;
		std::erase_if(emitter.RuntimeParticles, [&](Particle& particle)
		{
			particle.Age += seconds;
			if (particle.Age >= particle.Lifetime)
				return true;
			glm::vec3 push = pull;
			if (swirl)
				push += Swirl(particle.Position * emitter.NoiseFrequency, emitter.RuntimeClock, phase) * emitter.NoiseStrength;
			particle.Velocity = (particle.Velocity + push * seconds) * keep;
			particle.Position += particle.Velocity * seconds;
			particle.Rotation = std::fmod(particle.Rotation + particle.Spin * seconds, glm::two_pi<float>());
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

	float EvaluateCurve(const std::vector<ParticleCurveKey>& keys, float t, float fallback)
	{
		if (keys.empty())
			return fallback;
		if (t <= keys.front().Time)
			return keys.front().Value;
		for (size_t i = 1; i < keys.size(); i++)
		{
			if (t <= keys[i].Time)
			{
				const float span = keys[i].Time - keys[i - 1].Time;
				const float blend = span > 1e-6f ? (t - keys[i - 1].Time) / span : 1.0f;
				return glm::mix(keys[i - 1].Value, keys[i].Value, blend);
			}
		}
		return keys.back().Value;
	}

	glm::vec4 EvaluateGradient(const std::vector<ParticleColorKey>& keys, float t, const glm::vec4& fallback)
	{
		if (keys.empty())
			return fallback;
		if (t <= keys.front().Time)
			return keys.front().Color;
		for (size_t i = 1; i < keys.size(); i++)
		{
			if (t <= keys[i].Time)
			{
				const float span = keys[i].Time - keys[i - 1].Time;
				const float blend = span > 1e-6f ? (t - keys[i - 1].Time) / span : 1.0f;
				return glm::mix(keys[i - 1].Color, keys[i].Color, blend);
			}
		}
		return keys.back().Color;
	}

	namespace {

		float LifeFraction(const Particle& particle)
		{
			return particle.Lifetime > 0.0f ? std::clamp(particle.Age / particle.Lifetime, 0.0f, 1.0f) : 1.0f;
		}

	}

	float GetSize(const ParticleEmitterComponent& emitter, const Particle& particle)
	{
		const float t = LifeFraction(particle);
		if (!emitter.SizeCurve.empty())
			return emitter.StartSize * EvaluateCurve(emitter.SizeCurve, t, 1.0f);
		return glm::mix(emitter.StartSize, emitter.EndSize, t);
	}

	glm::vec4 GetFrameRect(const ParticleEmitterComponent& emitter, const Particle& particle)
	{
		const uint32_t columns = std::max(emitter.FlipbookColumns, 1u), rows = std::max(emitter.FlipbookRows, 1u);
		const uint32_t frames = columns * rows;
		if (frames == 1)
			return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
		const uint32_t start = emitter.FlipbookRandomStart ? std::min(static_cast<uint32_t>(particle.Random * static_cast<float>(frames)), frames - 1) : 0;
		uint32_t frame;
		if (emitter.FlipbookFps > 0.0f)
			frame = (static_cast<uint32_t>(std::max(particle.Age, 0.0f) * emitter.FlipbookFps) + start) % frames;
		else
			frame = (std::min(static_cast<uint32_t>(LifeFraction(particle) * static_cast<float>(frames)), frames - 1) + start) % frames;
		const float width = 1.0f / static_cast<float>(columns), height = 1.0f / static_cast<float>(rows);
		const float u = static_cast<float>(frame % columns) * width, v = static_cast<float>(frame / columns) * height;
		return glm::vec4(u, v, u + width, v + height);
	}

	glm::vec4 GetColor(const ParticleEmitterComponent& emitter, const Particle& particle)
	{
		const float t = LifeFraction(particle);
		glm::vec4 color = EvaluateGradient(emitter.ColorGradient, t, glm::mix(emitter.StartColor, emitter.EndColor, t));
		color.r *= emitter.Intensity;
		color.g *= emitter.Intensity;
		color.b *= emitter.Intensity;
		return color;
	}

}
