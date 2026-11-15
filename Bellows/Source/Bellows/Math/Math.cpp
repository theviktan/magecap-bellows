#include "bwpch.h"
#include "Bellows/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Bellows::Math {

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		if (!glm::decompose(transform, scale, rotation, translation, skew, perspective))
			return false;
		rotation = glm::normalize(rotation);
		return true;
	}

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
	}

	glm::mat4 PerspectiveProjection(float verticalFov, float aspectRatio, float nearClip, float farClip)
	{
		return glm::perspectiveRH_ZO(verticalFov, std::max(aspectRatio, 1e-4f), nearClip, farClip);
	}

	glm::mat4 OrthographicProjection(float left, float right, float bottom, float top, float nearClip, float farClip)
	{
		return glm::orthoRH_ZO(left, right, bottom, top, nearClip, farClip);
	}

	void ScreenPointToRay(const glm::vec2& viewportPosition, const glm::vec2& viewportSize, const glm::mat4& inverseViewProjection,
		glm::vec3& outOrigin, glm::vec3& outDirection)
	{
		const glm::vec2 uv = viewportPosition / glm::max(viewportSize, glm::vec2(1.0f));
		const glm::vec2 ndc(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
		glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc, 0.0f, 1.0f);
		glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;
		outOrigin = glm::vec3(nearPoint);
		outDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
	}

	bool WorldToScreenPoint(const glm::vec3& worldPosition, const glm::mat4& viewProjection, const glm::vec2& viewportSize, glm::vec2& outPosition)
	{
		const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
		if (clip.w <= 1e-6f)
			return false;
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		outPosition = glm::vec2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * viewportSize;
		return true;
	}

	glm::vec3 EulerDegreesToRadians(const glm::vec3& degrees)
	{
		return glm::radians(degrees);
	}

}
