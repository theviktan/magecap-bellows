#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Bellows::Math {

	// Splits an affine transform into translation, rotation and scale. Returns false for degenerate matrices.
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale);

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale);

	// Projections use depth 0..1 and Y-up clip space. NVRHI's Vulkan backend flips the viewport,
	// so no extra Y flip is needed (and screen-space UV (0,0) is the top-left corner).
	glm::mat4 PerspectiveProjection(float verticalFov, float aspectRatio, float nearClip, float farClip);
	glm::mat4 OrthographicProjection(float left, float right, float bottom, float top, float nearClip, float farClip);

	// Ray from a viewport position (pixels, top-left origin) through the scene.
	void ScreenPointToRay(const glm::vec2& viewportPosition, const glm::vec2& viewportSize, const glm::mat4& inverseViewProjection,
		glm::vec3& outOrigin, glm::vec3& outDirection);

	// Projects a world point to viewport pixels (top-left origin). Returns false if the point is behind the camera.
	bool WorldToScreenPoint(const glm::vec3& worldPosition, const glm::mat4& viewProjection, const glm::vec2& viewportSize, glm::vec2& outPosition);

	glm::vec3 EulerDegreesToRadians(const glm::vec3& degrees);

}
