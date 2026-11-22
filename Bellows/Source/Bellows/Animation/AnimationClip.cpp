#include "bwpch.h"
#include "Bellows/Animation/AnimationClip.h"

namespace Bellows {

	namespace {

		glm::vec4 Hermite(const glm::vec4& value0, const glm::vec4& out0, const glm::vec4& value1, const glm::vec4& in1, float t, float span)
		{
			const float t2 = t * t;
			const float t3 = t2 * t;
			return (2.0f * t3 - 3.0f * t2 + 1.0f) * value0 + (t3 - 2.0f * t2 + t) * span * out0
				+ (-2.0f * t3 + 3.0f * t2) * value1 + (t3 - t2) * span * in1;
		}

		glm::vec4 SampleTrack(const AnimationTrack& track, float time)
		{
			const std::vector<float>& times = track.Times;
			const bool cubic = track.Interpolation == AnimationInterpolation::CubicSpline;
			const auto value = [&](size_t key) { return track.Values[cubic ? key * 3 + 1 : key]; };
			if (time <= times.front())
				return value(0);
			if (time >= times.back())
				return value(times.size() - 1);

			// The key at or before 'time', and the one after it.
			const size_t next = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
			const size_t key = next - 1;
			const float span = times[next] - times[key];
			const float t = span > 0.0f ? (time - times[key]) / span : 0.0f;
			switch (track.Interpolation)
			{
				case AnimationInterpolation::Step:
					return value(key);
				case AnimationInterpolation::CubicSpline:
					return Hermite(value(key), track.Values[key * 3 + 2], value(next), track.Values[next * 3], t, span);
				case AnimationInterpolation::Linear:
					break;
			}
			if (track.Path != AnimationPath::Rotation)
				return glm::mix(value(key), value(next), t);
			const glm::vec4 a = value(key);
			const glm::vec4 b = value(next);
			const JointPose from { glm::vec3(0.0f), glm::quat(a.w, a.x, a.y, a.z), glm::vec3(1.0f) };
			const JointPose to { glm::vec3(0.0f), glm::quat(b.w, b.x, b.y, b.z), glm::vec3(1.0f) };
			const glm::quat q = JointPose::Blend(from, to, t).Rotation;
			return glm::vec4(q.x, q.y, q.z, q.w);
		}

	}

	AnimationClip::AnimationClip(std::string name, std::vector<AnimationTrack> tracks)
		: m_Name(std::move(name)), m_Tracks(std::move(tracks))
	{
		for (const AnimationTrack& track : m_Tracks)
		{
			const size_t valuesPerKey = track.Interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
			BW_CORE_ASSERT(!track.Times.empty() && track.Values.size() == track.Times.size() * valuesPerKey, "A track needs its values");
			m_Duration = std::max(m_Duration, track.Times.back());
		}
	}

	void AnimationClip::Sample(float time, std::vector<JointPose>& pose) const
	{
		time = std::clamp(std::isfinite(time) ? time : 0.0f, 0.0f, m_Duration);
		for (const AnimationTrack& track : m_Tracks)
		{
			if (track.Joint >= pose.size())
				continue;
			const glm::vec4 value = SampleTrack(track, time);
			JointPose& joint = pose[track.Joint];
			switch (track.Path)
			{
				case AnimationPath::Translation:	joint.Translation = glm::vec3(value); break;
				case AnimationPath::Scale:			joint.Scale = glm::vec3(value); break;
				case AnimationPath::Rotation:
				{
					const glm::quat q(value.w, value.x, value.y, value.z);
					const float length = glm::length(q);
					joint.Rotation = length > 1e-6f ? q / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
					break;
				}
			}
		}
	}

}
