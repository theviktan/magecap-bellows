#pragma once

#include "Bellows/Animation/AnimationClip.h"
#include "Bellows/Animation/Skeleton.h"
#include "Bellows/Core/Base.h"

#include <glm/glm.hpp>

#include <vector>

namespace Bellows {

	// Plays a skeleton's clips one at a time, crossfading from the pose on screen to a new clip. Speed
	// and looping are passed to Update each frame, so they can change while a clip plays.
	class AnimationPlayer
	{
	public:
		static constexpr float MaxSpeed = 10.0f;		// Either way; negative plays backward
		static constexpr float MaxFadeSeconds = 10.0f;

		// Starts 'clip' from its start (or its end, when 'speed' is negative), blending from the current
		// pose over 'fade' seconds (0 cuts). Asking for the clip that is already playing changes nothing.
		void Play(const Ref<AnimationClip>& clip, float fade = 0.0f, float speed = 1.0f);
		// Holds the current pose; Play starts again.
		void Stop();
		void Pause() { m_Paused = true; }
		void Resume() { m_Paused = false; }
		// Back to the rest pose, with no clip.
		void Reset();

		// Moves the clip on by 'seconds' times 'speed'. A looping clip wraps around; one that doesn't stops
		// at its end (its start, played backward) and gives back true that once.
		bool Update(float seconds, float speed, bool loop);

		// The pose as skin matrices for the shader; false (and nothing written) without a clip, when
		// the mesh is drawn in its rest pose.
		bool ComputeSkinMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outSkin);
		// Each joint's transform in the model's space for the pose shown (the rest pose without a clip).
		void ComputeModelMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outModel);

		const Ref<AnimationClip>& GetClip() const { return m_Clip; }
		float GetTime() const { return m_Time; }
		// Seconds into the clip, held to its length.
		void SetTime(float time);
		// A clip is moving: started, not stopped, not paused and not at the end of a clip that doesn't loop.
		bool IsPlaying() const { return m_Clip && m_Playing && !m_Paused; }
		bool IsPaused() const { return m_Paused; }
		bool IsFading() const { return m_FadeSeconds > 0.0f && m_FadeElapsed < m_FadeSeconds; }
	private:
		void ComputePose(const Skeleton& skeleton);
		static float Advance(const AnimationClip& clip, float time, float delta, bool loop);
	private:
		Ref<AnimationClip> m_Clip;
		float m_Time = 0.0f;
		bool m_Playing = false;
		bool m_Paused = false;

		// The pose being faded from: a clip held at a time, or the rest pose when null.
		Ref<AnimationClip> m_FromClip;
		float m_FromTime = 0.0f;
		float m_FadeSeconds = 0.0f;
		float m_FadeElapsed = 0.0f;

		std::vector<JointPose> m_Pose;
		std::vector<JointPose> m_FromPose;
		std::vector<glm::mat4> m_Model;
	};

}
