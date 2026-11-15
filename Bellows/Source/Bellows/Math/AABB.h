#pragma once

#include <glm/glm.hpp>

#include <limits>

namespace Bellows {

	// Axis-aligned bounding box.
	struct AABB
	{
		glm::vec3 Min { std::numeric_limits<float>::max() };
		glm::vec3 Max { std::numeric_limits<float>::lowest() };

		AABB() = default;
		AABB(const glm::vec3& min, const glm::vec3& max)
			: Min(min), Max(max) {}

		bool IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
		glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }
		glm::vec3 GetExtents() const { return (Max - Min) * 0.5f; }
		glm::vec3 GetSize() const { return Max - Min; }

		void Expand(const glm::vec3& point)
		{
			Min = glm::min(Min, point);
			Max = glm::max(Max, point);
		}

		void Expand(const AABB& other)
		{
			if (!other.IsValid())
				return;
			Expand(other.Min);
			Expand(other.Max);
		}

		// Bounds of this box after an affine transform (Arvo's method).
		AABB Transformed(const glm::mat4& transform) const
		{
			if (!IsValid())
				return *this;

			const glm::vec3 translation(transform[3]);
			AABB result(translation, translation);
			for (int column = 0; column < 3; column++)
			{
				for (int row = 0; row < 3; row++)
				{
					const float a = transform[column][row] * Min[column];
					const float b = transform[column][row] * Max[column];
					result.Min[row] += std::min(a, b);
					result.Max[row] += std::max(a, b);
				}
			}
			return result;
		}
	};

}
