#pragma once

#include "Bellows/Core/Timestep.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include <glm/glm.hpp>

namespace Bellows {

	// Viewport camera for editing.
	//   Right mouse + WASD/QE : fly (scroll changes speed)
	//   Alt + left mouse      : orbit around the focus point
	//   Middle mouse          : pan
	//   Scroll                : dolly towards the focus point
	class EditorCamera
	{
	public:
		EditorCamera();

		// Processes input while the viewport is hovered/focused. Returns true while the camera is being controlled.
		// 'mouseWheel' is the wheel movement collected during the last UI frame (ImGui clears its
		// own value at the end of each frame, before layers update).
		bool OnUpdate(Timestep timestep, bool viewportHovered, float mouseWheel);

		void SetViewportSize(float width, float height);
		void Focus(const glm::vec3& point, float radius);
		void SetPose(const glm::vec3& position, const glm::vec3& target);

		RenderCamera GetRenderCamera() const;
		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjectionMatrix() const;
		const glm::vec3& GetPosition() const { return m_Position; }
		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;
		const glm::vec3& GetFocusPoint() const { return m_FocusPoint; }

		float GetFlySpeed() const { return m_FlySpeed; }
		void SetFlySpeed(float speed) { m_FlySpeed = std::clamp(speed, 0.1f, 500.0f); }
		float GetFieldOfView() const { return m_FieldOfView; }
		void SetFieldOfView(float radians) { m_FieldOfView = radians; }
		bool IsFlying() const { return m_Flying; }
	private:
		glm::vec3 m_Position { 6.0f, 4.0f, 8.0f };
		glm::vec3 m_FocusPoint { 0.0f };
		float m_Yaw = 0.0f;		// Radians around +Y
		float m_Pitch = 0.0f;	// Radians
		float m_FieldOfView = glm::radians(55.0f);
		float m_NearClip = 0.05f;
		float m_FarClip = 2000.0f;
		float m_ViewportWidth = 1280.0f;
		float m_ViewportHeight = 720.0f;
		float m_FlySpeed = 6.0f;
		bool m_Flying = false;
		bool m_Orbiting = false;
		bool m_Panning = false;
	};

}
