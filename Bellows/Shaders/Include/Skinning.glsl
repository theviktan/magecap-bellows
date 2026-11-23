// Skinned vertices: the frame's joint matrices (set 2) and how a vertex blends them.

#ifndef BELLOWS_SKINNING_GLSL
#define BELLOWS_SKINNING_GLSL

// World-space joint matrices (the entity's transform times each joint's skin matrix) of every skinned
// draw this frame; a draw's start at its joint offset.
layout(set = 2, binding = 0, std430) readonly buffer JointBlock { mat4 u_Joints[]; };

mat4 SkinMatrix(uint jointOffset, uvec4 joints, vec4 weights)
{
	return weights.x * u_Joints[jointOffset + joints.x]
		+ weights.y * u_Joints[jointOffset + joints.y]
		+ weights.z * u_Joints[jointOffset + joints.z]
		+ weights.w * u_Joints[jointOffset + joints.w];
}

// What normals are multiplied by: the cofactor matrix is the inverse transpose scaled by the
// determinant, so it handles scaled joints without an inverse; its sign keeps mirrored ones facing out.
mat3 SkinNormalMatrix(mat4 skin)
{
	mat3 m = mat3(skin);
	mat3 cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
	return determinant(m) < 0.0 ? -cofactor : cofactor;
}

#endif
