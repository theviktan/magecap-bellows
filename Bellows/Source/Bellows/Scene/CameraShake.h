#pragma once

#include <glm/glm.hpp>

namespace Bellows {

	// A shake of the game camera (an explosion, a landing), which fades out over its length. It only moves
	// the rendered view: the camera's Transform never changes, so scripts reading its position, physics and
	// saved scenes don't see it. Strength 1 moves the view up to 0.15 m and turns it up to 1.5 degrees.
	class CameraShake
	{
	public:
		static constexpr float MaxStrength = 10.0f;
		static constexpr float MaxSeconds = 10.0f;
		static constexpr float MetersPerStrength = 0.15f;
		static constexpr float DegreesPerStrength = 1.5f;

		// Starts a shake. While one runs, the stronger of the two (the new one, or what is left of the
		// running one) is kept. A strength or length of 0 starts nothing.
		void Start(float strength, float seconds);
		// Advances game time; a paused game doesn't call it, so the shake holds.
		void Update(float seconds);
		void Clear();

		bool IsShaking() const { return m_Elapsed < m_Duration && m_Strength > 0.0f; }
		// What is left of the strength now: it fades to 0 at the end.
		float GetCurrentStrength() const;

		// The offset to apply to the camera, in its own space (right-multiply the camera's world transform).
		glm::mat4 GetOffset() const;
	private:
		float m_Strength = 0.0f;
		float m_Duration = 0.0f;
		float m_Elapsed = 0.0f;
	};

}
