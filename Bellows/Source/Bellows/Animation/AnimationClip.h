#pragma once

#include "Bellows/Animation/Skeleton.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Bellows {

	enum class AnimationPath : uint8_t
	{
		Translation = 0,
		Rotation,
		Scale,
	};

	enum class AnimationInterpolation : uint8_t
	{
		Linear = 0,
		Step,			// Holds each key until the next
		CubicSpline,	// Hermite curves; each key stores an in-tangent, the value and an out-tangent
	};

	// One joint's translation, rotation or scale over time. Rotations are quaternions stored x, y, z, w.
	struct AnimationTrack
	{
		uint32_t Joint = 0;
		AnimationPath Path = AnimationPath::Translation;
		AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
		std::vector<float> Times;			// Seconds, increasing
		std::vector<glm::vec4> Values;		// One per time (three for CubicSpline)
	};

	// A named animation of a skeleton ("Walk"), as imported from a model.
	class AnimationClip
	{
	public:
		static constexpr size_t MaxClips = 256;

		AnimationClip(std::string name, std::vector<AnimationTrack> tracks);

		const std::string& GetName() const { return m_Name; }
		// Seconds: the last key of any track.
		float GetDuration() const { return m_Duration; }
		const std::vector<AnimationTrack>& GetTracks() const { return m_Tracks; }

		// Sets the animated parts of 'pose' to the clip at 'time' (held to 0 to the duration). Joints and
		// parts the clip doesn't animate keep the values they had.
		void Sample(float time, std::vector<JointPose>& pose) const;
	private:
		std::string m_Name;
		std::vector<AnimationTrack> m_Tracks;
		float m_Duration = 0.0f;
	};

}
