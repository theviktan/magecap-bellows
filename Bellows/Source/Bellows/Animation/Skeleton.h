#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	// A joint's transform relative to its parent.
	struct JointPose
	{
		glm::vec3 Translation { 0.0f };
		glm::quat Rotation { 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale { 1.0f };

		glm::mat4 ToMatrix() const;
		// From a to b by t (0 to 1); rotations take the shortest way.
		static JointPose Blend(const JointPose& a, const JointPose& b, float t);
	};

	struct Joint
	{
		std::string Name;
		int32_t Parent = -1;			// An earlier joint, or -1 for a root
		JointPose Rest;					// The pose the model was made in
		glm::mat4 InverseBind { 1.0f };	// Model space to the joint's space, in the rest pose
		// Nodes between the joint and its parent joint that aren't joints (an "Armature" above a root),
		// which stay as they are: model = parent joint's model * ParentTransform * pose.
		glm::mat4 ParentTransform { 1.0f };
	};

	// The joints of a skinned model, parents always before their children, so a pose turns into
	// matrices in one pass.
	class Skeleton
	{
	public:
		static constexpr size_t MaxJoints = 256;

		explicit Skeleton(std::vector<Joint> joints);

		const std::vector<Joint>& GetJoints() const { return m_Joints; }
		size_t GetJointCount() const { return m_Joints.size(); }
		// The joint's index, or -1.
		int32_t FindJoint(std::string_view name) const;
		std::vector<JointPose> GetRestPose() const;

		// Each joint's transform in the model's space, for a pose of every joint.
		void ComputeModelMatrices(const std::vector<JointPose>& pose, std::vector<glm::mat4>& outModel) const;
		// What the shader multiplies vertices by: each joint's model matrix times its inverse bind matrix.
		void ComputeSkinMatrices(const std::vector<glm::mat4>& model, std::vector<glm::mat4>& outSkin) const;
	private:
		std::vector<Joint> m_Joints;
	};

}
