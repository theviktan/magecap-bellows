#include "bwpch.h"
#include "Bellows/Animation/Skeleton.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	glm::mat4 JointPose::ToMatrix() const
	{
		return glm::translate(glm::mat4(1.0f), Translation) * glm::mat4_cast(Rotation) * glm::scale(glm::mat4(1.0f), Scale);
	}

	JointPose JointPose::Blend(const JointPose& a, const JointPose& b, float t)
	{
		JointPose result;
		result.Translation = glm::mix(a.Translation, b.Translation, t);
		result.Scale = glm::mix(a.Scale, b.Scale, t);
		// The shortest way: q and -q are the same turn.
		const glm::quat to = glm::dot(a.Rotation, b.Rotation) < 0.0f ? -b.Rotation : b.Rotation;
		result.Rotation = glm::normalize(glm::slerp(a.Rotation, to, t));
		return result;
	}

	Skeleton::Skeleton(std::vector<Joint> joints)
		: m_Joints(std::move(joints))
	{
		BW_CORE_ASSERT(m_Joints.size() <= MaxJoints, "A skeleton has at most 256 joints");
		for (size_t i = 0; i < m_Joints.size(); i++)
			BW_CORE_ASSERT(m_Joints[i].Parent < static_cast<int32_t>(i), "Joints must come after their parents");
	}

	int32_t Skeleton::FindJoint(std::string_view name) const
	{
		for (size_t i = 0; i < m_Joints.size(); i++)
		{
			if (m_Joints[i].Name == name)
				return static_cast<int32_t>(i);
		}
		return -1;
	}

	std::vector<JointPose> Skeleton::GetRestPose() const
	{
		std::vector<JointPose> pose;
		pose.reserve(m_Joints.size());
		for (const Joint& joint : m_Joints)
			pose.push_back(joint.Rest);
		return pose;
	}

	void Skeleton::ComputeModelMatrices(const std::vector<JointPose>& pose, std::vector<glm::mat4>& outModel) const
	{
		outModel.resize(m_Joints.size());
		for (size_t i = 0; i < m_Joints.size(); i++)
		{
			const Joint& joint = m_Joints[i];
			const glm::mat4 local = (i < pose.size() ? pose[i] : joint.Rest).ToMatrix();
			const glm::mat4 parent = joint.Parent >= 0 ? outModel[static_cast<size_t>(joint.Parent)] : glm::mat4(1.0f);
			outModel[i] = parent * joint.ParentTransform * local;
		}
	}

	void Skeleton::ComputeSkinMatrices(const std::vector<glm::mat4>& model, std::vector<glm::mat4>& outSkin) const
	{
		outSkin.resize(m_Joints.size());
		for (size_t i = 0; i < m_Joints.size(); i++)
			outSkin[i] = (i < model.size() ? model[i] : glm::mat4(1.0f)) * m_Joints[i].InverseBind;
	}

}
