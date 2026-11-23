#include "bwpch.h"
#include "Bellows/Animation/AnimationPlayer.h"

namespace Bellows {

	namespace {

		float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

	}

	void AnimationPlayer::Play(const Ref<AnimationClip>& clip, float fade, float speed)
	{
		if (!clip)
		{
			Reset();
			return;
		}
		if (clip == m_Clip && m_Playing)
		{
			m_Paused = false;
			return;
		}

		// Fade from what is on screen now: the current clip where it is, or the rest pose.
		fade = std::clamp(Finite(fade, 0.0f), 0.0f, MaxFadeSeconds);
		m_FromClip = fade > 0.0f ? m_Clip : nullptr;
		m_FromTime = m_Time;
		m_FadeSeconds = fade;
		m_FadeElapsed = 0.0f;

		m_Clip = clip;
		m_Time = Finite(speed, 1.0f) < 0.0f ? clip->GetDuration() : 0.0f;
		m_Playing = true;
		m_Paused = false;
	}

	void AnimationPlayer::Stop()
	{
		m_Playing = false;
		m_Paused = false;
	}

	void AnimationPlayer::Reset()
	{
		m_Clip = nullptr;
		m_FromClip = nullptr;
		m_Time = 0.0f;
		m_FromTime = 0.0f;
		m_FadeSeconds = 0.0f;
		m_FadeElapsed = 0.0f;
		m_Playing = false;
		m_Paused = false;
	}

	float AnimationPlayer::Advance(const AnimationClip& clip, float time, float delta, bool loop)
	{
		const float duration = clip.GetDuration();
		if (duration <= 0.0f)
			return 0.0f;
		time += delta;
		if (loop)
		{
			time = std::fmod(time, duration);
			if (time < 0.0f)
				time += duration;
			return time;
		}
		return std::clamp(time, 0.0f, duration);
	}

	bool AnimationPlayer::Update(float seconds, float speed, bool loop)
	{
		seconds = std::max(Finite(seconds, 0.0f), 0.0f);
		speed = std::clamp(Finite(speed, 1.0f), -MaxSpeed, MaxSpeed);
		if (!m_Clip || m_Paused)
			return false;

		const float delta = seconds * speed;
		if (IsFading())
		{
			m_FadeElapsed = std::min(m_FadeElapsed + seconds, m_FadeSeconds);
			if (m_FromClip)
				m_FromTime = Advance(*m_FromClip, m_FromTime, delta, loop);
		}
		if (!m_Playing)
			return false;

		m_Time = Advance(*m_Clip, m_Time, delta, loop);
		if (loop)
			return false;
		// A clip that doesn't loop finishes at the end it was heading for.
		const bool atEnd = speed > 0.0f ? m_Time >= m_Clip->GetDuration() : speed < 0.0f && m_Time <= 0.0f;
		if (atEnd)
			m_Playing = false;
		return atEnd;
	}

	void AnimationPlayer::ComputePose(const Skeleton& skeleton)
	{
		m_Pose = skeleton.GetRestPose();
		if (!m_Clip)
			return;
		m_Clip->Sample(m_Time, m_Pose);
		if (IsFading())
		{
			m_FromPose = skeleton.GetRestPose();
			if (m_FromClip)
				m_FromClip->Sample(m_FromTime, m_FromPose);
			const float t = m_FadeElapsed / m_FadeSeconds;
			for (size_t joint = 0; joint < m_Pose.size(); joint++)
				m_Pose[joint] = JointPose::Blend(m_FromPose[joint], m_Pose[joint], t);
		}
	}

	bool AnimationPlayer::ComputeSkinMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outSkin)
	{
		if (!m_Clip)
			return false;
		ComputePose(skeleton);
		skeleton.ComputeModelMatrices(m_Pose, m_Model);
		skeleton.ComputeSkinMatrices(m_Model, outSkin);
		return true;
	}

	void AnimationPlayer::ComputeModelMatrices(const Skeleton& skeleton, std::vector<glm::mat4>& outModel)
	{
		ComputePose(skeleton);
		skeleton.ComputeModelMatrices(m_Pose, outModel);
	}

	void AnimationPlayer::SetTime(float time)
	{
		if (m_Clip)
			m_Time = std::clamp(Finite(time, 0.0f), 0.0f, m_Clip->GetDuration());
	}

}
