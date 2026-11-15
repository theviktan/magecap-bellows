#include "EditorCamera.h"

#include "Bellows/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace Bellows {

	EditorCamera::EditorCamera()
	{
		SetPose(m_Position, m_FocusPoint);
	}

	void EditorCamera::SetPose(const glm::vec3& position, const glm::vec3& target)
	{
		m_Position = position;
		m_FocusPoint = target;
		glm::vec3 direction = target - position;
		if (glm::length(direction) < 1e-5f)
			direction = glm::vec3(0.0f, 0.0f, -1.0f);
		direction = glm::normalize(direction);
		m_Pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
		m_Yaw = std::atan2(-direction.x, -direction.z);
	}

	glm::vec3 EditorCamera::GetForward() const
	{
		return glm::normalize(glm::vec3(-std::sin(m_Yaw) * std::cos(m_Pitch), std::sin(m_Pitch), -std::cos(m_Yaw) * std::cos(m_Pitch)));
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	glm::vec3 EditorCamera::GetUp() const
	{
		return glm::normalize(glm::cross(GetRight(), GetForward()));
	}

	glm::mat4 EditorCamera::GetViewMatrix() const
	{
		return glm::lookAt(m_Position, m_Position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 EditorCamera::GetProjectionMatrix() const
	{
		return Math::PerspectiveProjection(m_FieldOfView, m_ViewportWidth / std::max(m_ViewportHeight, 1.0f), m_NearClip, m_FarClip);
	}

	RenderCamera EditorCamera::GetRenderCamera() const
	{
		RenderCamera camera;
		camera.View = GetViewMatrix();
		camera.Projection = GetProjectionMatrix();
		camera.Position = m_Position;
		camera.NearClip = m_NearClip;
		camera.FarClip = m_FarClip;
		return camera;
	}

	void EditorCamera::SetViewportSize(float width, float height)
	{
		m_ViewportWidth = std::max(width, 1.0f);
		m_ViewportHeight = std::max(height, 1.0f);
	}

	void EditorCamera::Focus(const glm::vec3& point, float radius)
	{
		const float distance = std::max(radius, 0.25f) / std::tan(m_FieldOfView * 0.5f) * 1.2f;
		m_FocusPoint = point;
		m_Position = point - GetForward() * distance;
	}

	bool EditorCamera::OnUpdate(Timestep timestep, bool viewportHovered, float mouseWheel)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const float deltaTime = std::max(timestep.GetSeconds(), 1e-4f);
		const float rotationSpeed = 0.0035f;

		// Gestures begin only inside the viewport but continue when the cursor leaves it.
		if (viewportHovered)
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				m_Flying = true;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
				m_Panning = true;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)
				m_Orbiting = true;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
			m_Flying = false;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			m_Panning = false;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_Orbiting = false;

		const glm::vec2 mouseDelta(io.MouseDelta.x, io.MouseDelta.y);

		if (m_Flying)
		{
			m_Yaw -= mouseDelta.x * rotationSpeed;
			m_Pitch = std::clamp(m_Pitch - mouseDelta.y * rotationSpeed, glm::radians(-89.0f), glm::radians(89.0f));

			glm::vec3 movement(0.0f);
			if (ImGui::IsKeyDown(ImGuiKey_W)) movement += GetForward();
			if (ImGui::IsKeyDown(ImGuiKey_S)) movement -= GetForward();
			if (ImGui::IsKeyDown(ImGuiKey_D)) movement += GetRight();
			if (ImGui::IsKeyDown(ImGuiKey_A)) movement -= GetRight();
			if (ImGui::IsKeyDown(ImGuiKey_E)) movement += glm::vec3(0.0f, 1.0f, 0.0f);
			if (ImGui::IsKeyDown(ImGuiKey_Q)) movement -= glm::vec3(0.0f, 1.0f, 0.0f);
			float speed = m_FlySpeed * (io.KeyShift ? 3.0f : 1.0f);
			if (glm::length(movement) > 0.0f)
				m_Position += glm::normalize(movement) * speed * deltaTime;

			if (mouseWheel != 0.0f)
				SetFlySpeed(m_FlySpeed * (mouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f));

			const float focusDistance = std::max(glm::length(m_FocusPoint - m_Position), 1.0f);
			m_FocusPoint = m_Position + GetForward() * focusDistance;
			return true;
		}

		if (m_Orbiting)
		{
			const float distance = glm::length(m_FocusPoint - m_Position);
			m_Yaw -= mouseDelta.x * rotationSpeed;
			m_Pitch = std::clamp(m_Pitch - mouseDelta.y * rotationSpeed, glm::radians(-89.0f), glm::radians(89.0f));
			m_Position = m_FocusPoint - GetForward() * distance;
			return true;
		}

		if (m_Panning)
		{
			const float distance = std::max(glm::length(m_FocusPoint - m_Position), 0.5f);
			const float scale = distance * std::tan(m_FieldOfView * 0.5f) * 2.0f / m_ViewportHeight;
			const glm::vec3 offset = (-GetRight() * mouseDelta.x + GetUp() * mouseDelta.y) * scale;
			m_Position += offset;
			m_FocusPoint += offset;
			return true;
		}

		if (viewportHovered && mouseWheel != 0.0f)
		{
			const float distance = glm::length(m_FocusPoint - m_Position);
			const float step = std::max(distance * 0.15f, 0.1f) * mouseWheel;
			if (distance - step > 0.2f)
				m_Position += GetForward() * step;
			else
				m_FocusPoint = m_Position + GetForward() * (distance + 1.0f);
		}
		return false;
	}

}
