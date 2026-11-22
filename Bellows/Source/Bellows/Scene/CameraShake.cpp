#include "bwpch.h"
#include "Bellows/Scene/CameraShake.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Bellows {

	namespace {

		// Smooth noise from -1 to 1: two sine waves per channel, at frequencies no channel shares, so the
		// view wanders instead of buzzing back and forth on one line.
		float Wave(float time, int channel)
		{
			static constexpr float c_Fast[] = { 23.0f, 29.0f, 19.0f, 31.0f, 17.0f };
			static constexpr float c_Slow[] = { 11.0f, 13.0f, 7.0f, 9.0f, 5.0f };
			static constexpr float c_Phase[] = { 0.0f, 1.3f, 2.1f, 3.7f, 4.9f };
			return (std::sin(time * c_Fast[channel] + c_Phase[channel]) + 0.5f * std::sin(time * c_Slow[channel] + c_Phase[channel] * 1.7f)) / 1.5f;
		}

	}

	void CameraShake::Start(float strength, float seconds)
	{
		strength = std::clamp(std::isfinite(strength) ? strength : 0.0f, 0.0f, MaxStrength);
		seconds = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, MaxSeconds);
		if (strength <= 0.0f || seconds <= 0.0f || strength < GetCurrentStrength())
			return;
		m_Strength = strength;
		m_Duration = seconds;
		m_Elapsed = 0.0f;
	}

	void CameraShake::Update(float seconds)
	{
		if (IsShaking() && std::isfinite(seconds) && seconds > 0.0f)
			m_Elapsed = std::min(m_Elapsed + seconds, m_Duration);
	}

	void CameraShake::Clear()
	{
		*this = CameraShake();
	}

	float CameraShake::GetCurrentStrength() const
	{
		if (!IsShaking())
			return 0.0f;
		const float left = 1.0f - m_Elapsed / m_Duration;
		return m_Strength * left * left;
	}

	glm::mat4 CameraShake::GetOffset() const
	{
		const float strength = GetCurrentStrength();
		if (strength <= 0.0f)
			return glm::mat4(1.0f);
		const float t = m_Elapsed;
		const glm::vec3 move = strength * MetersPerStrength * glm::vec3(Wave(t, 0), Wave(t, 1), 0.0f);
		const glm::vec3 turn = glm::radians(strength * DegreesPerStrength * glm::vec3(Wave(t, 2), Wave(t, 3), 0.5f * Wave(t, 4)));
		return glm::translate(glm::mat4(1.0f), move) * glm::eulerAngleYXZ(turn.y, turn.x, turn.z);
	}

}
