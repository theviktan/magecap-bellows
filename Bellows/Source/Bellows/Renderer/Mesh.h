#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Math/AABB.h"
#include "Bellows/Renderer/Material.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <string>
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
