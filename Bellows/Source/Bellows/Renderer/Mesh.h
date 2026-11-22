#pragma once

#include "Bellows/Animation/AnimationClip.h"
#include "Bellows/Animation/Skeleton.h"
#include "Bellows/Core/Base.h"
#include "Bellows/Math/AABB.h"
#include "Bellows/Renderer/Material.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <nvrhi/nvrhi.h>

#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	struct Vertex
	{
		glm::vec3 Position { 0.0f };
		glm::vec3 Normal { 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent { 0.0f };		// xyz = tangent, w = bitangent sign (0 = no tangent data)
		glm::vec2 TexCoord { 0.0f };
	};
	static_assert(sizeof(Vertex) == 48, "Vertex layout must match Mesh.vert");

	// A skinned vertex's joints and how much each moves it (the weights add up to 1). Kept in a second
	// stream so static meshes keep their 48-byte vertex.
	struct SkinVertex
	{
		glm::u8vec4 Joints { 0 };
		glm::vec4 Weights { 1.0f, 0.0f, 0.0f, 0.0f };
	};
	static_assert(sizeof(SkinVertex) == 20, "SkinVertex layout must match the skinned vertex shaders");

	// What makes a mesh animatable: its skeleton, a SkinVertex for every vertex of the mesh (those of
	// submeshes that aren't skinned are never read), and the clips that came with it.
	struct MeshSkin
	{
		Ref<Skeleton> Rig;
		std::vector<SkinVertex> Vertices;
		std::vector<Ref<AnimationClip>> Clips;

		// The clip with that name, or null.
		Ref<AnimationClip> FindClip(std::string_view name) const;
	};

	// A draw range inside a mesh's shared vertex/index buffers.
	struct Submesh
	{
		uint32_t BaseVertex = 0;
		uint32_t BaseIndex = 0;
		uint32_t IndexCount = 0;
		uint32_t VertexCount = 0;
		uint32_t MaterialIndex = 0;
		glm::mat4 Transform { 1.0f };	// Node transform relative to the mesh origin
		AABB BoundingBox;				// Local bounds (before Transform)
		std::string Name;
		bool Skinned = false;			// Moved by the mesh's skeleton; Transform is then only the model's turn (its rest pose)
	};

	// Geometry with its default materials. CPU data is kept for physics and picking.
	class Mesh
	{
	public:
		Mesh(std::string name, std::vector<Vertex> vertices, std::vector<uint32_t> indices,
			std::vector<Submesh> submeshes, std::vector<Ref<Material>> materials);

		const std::string& GetName() const { return m_Name; }
		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }
		const std::vector<Ref<Material>>& GetMaterials() const { return m_Materials; }
		const AABB& GetBoundingBox() const { return m_BoundingBox; }

		// The skeleton, weights and clips of a skinned model, or null for a static mesh.
		const MeshSkin* GetSkin() const { return m_Skin ? m_Skin.get() : nullptr; }
		void SetSkin(MeshSkin skin);

		nvrhi::IBuffer* GetVertexBuffer() const { return m_VertexBuffer; }
		nvrhi::IBuffer* GetIndexBuffer() const { return m_IndexBuffer; }

		// Computes per-vertex tangents for triangles that lack them.
		static void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
	private:
		void Upload();
	private:
		std::string m_Name;
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		std::vector<Submesh> m_Submeshes;
		std::vector<Ref<Material>> m_Materials;
		AABB m_BoundingBox;
		Scope<MeshSkin> m_Skin;

		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
	};

	enum class PrimitiveMesh : uint8_t
	{
		Cube = 0,
		Sphere,
		Plane,
		Capsule,
		Cylinder,
		Cone,
		Quad,
	};

	const char* PrimitiveMeshToString(PrimitiveMesh primitive);

	// Procedural meshes, each centered at the origin and one unit in size (1x1x1 bounds).
	class MeshFactory
	{
	public:
		static Ref<Mesh> CreatePrimitive(PrimitiveMesh primitive);

		static Ref<Mesh> CreateCube();
		static Ref<Mesh> CreateSphere(uint32_t segments = 48, uint32_t rings = 24);
		static Ref<Mesh> CreatePlane(uint32_t subdivisions = 1);
		static Ref<Mesh> CreateCapsule(uint32_t segments = 32, uint32_t rings = 8);
		static Ref<Mesh> CreateCylinder(uint32_t segments = 48);
		static Ref<Mesh> CreateCone(uint32_t segments = 48);
		static Ref<Mesh> CreateQuad();
	};

}
