#include "bwpch.h"
#include "Bellows/Renderer/Mesh.h"

#include "Bellows/Renderer/Renderer.h"

#include <glm/gtc/constants.hpp>

namespace Bellows {

	Ref<AnimationClip> MeshSkin::FindClip(std::string_view name) const
	{
		for (const Ref<AnimationClip>& clip : Clips)
		{
			if (clip->GetName() == name)
				return clip;
		}
		return nullptr;
	}

	void Mesh::SetSkin(MeshSkin skin)
	{
		BW_CORE_ASSERT(skin.Rig && skin.Vertices.size() == m_Vertices.size(), "A skin needs a skeleton and a SkinVertex per vertex");
		m_Skin = CreateScope<MeshSkin>(std::move(skin));
	}

	Mesh::Mesh(std::string name, std::vector<Vertex> vertices, std::vector<uint32_t> indices,
		std::vector<Submesh> submeshes, std::vector<Ref<Material>> materials)
		: m_Name(std::move(name)), m_Vertices(std::move(vertices)), m_Indices(std::move(indices)),
		  m_Submeshes(std::move(submeshes)), m_Materials(std::move(materials))
	{
		if (m_Materials.empty())
			m_Materials.push_back(CreateRef<Material>("Default"));

		if (m_Submeshes.empty())
		{
			Submesh submesh;
			submesh.IndexCount = static_cast<uint32_t>(m_Indices.size());
			submesh.VertexCount = static_cast<uint32_t>(m_Vertices.size());
			submesh.Name = m_Name;
			m_Submeshes.push_back(submesh);
		}

		for (Submesh& submesh : m_Submeshes)
		{
			BW_CORE_ASSERT(submesh.BaseIndex + submesh.IndexCount <= m_Indices.size(), "Submesh index range is out of bounds");
			submesh.MaterialIndex = std::min<uint32_t>(submesh.MaterialIndex, static_cast<uint32_t>(m_Materials.size() - 1));
			if (!submesh.BoundingBox.IsValid())
			{
				for (uint32_t i = 0; i < submesh.IndexCount; i++)
					submesh.BoundingBox.Expand(m_Vertices[submesh.BaseVertex + m_Indices[submesh.BaseIndex + i]].Position);
			}
			m_BoundingBox.Expand(submesh.BoundingBox.Transformed(submesh.Transform));
		}

		Upload();
	}

	void Mesh::Upload()
	{
		if (m_Vertices.empty() || m_Indices.empty() || !Renderer::IsInitialized())
			return;

		nvrhi::IDevice* device = Renderer::GetDevice();

		nvrhi::BufferDesc vertexDesc;
		vertexDesc.byteSize = m_Vertices.size() * sizeof(Vertex);
		vertexDesc.isVertexBuffer = true;
		vertexDesc.debugName = m_Name + "_Vertices";
		vertexDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
		vertexDesc.keepInitialState = true;
		m_VertexBuffer = device->createBuffer(vertexDesc);

		nvrhi::BufferDesc indexDesc;
		indexDesc.byteSize = m_Indices.size() * sizeof(uint32_t);
		indexDesc.isIndexBuffer = true;
		indexDesc.debugName = m_Name + "_Indices";
		indexDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
		indexDesc.keepInitialState = true;
		m_IndexBuffer = device->createBuffer(indexDesc);

		Renderer::ExecuteImmediate([this](nvrhi::ICommandList* commandList)
		{
			commandList->writeBuffer(m_VertexBuffer, m_Vertices.data(), m_Vertices.size() * sizeof(Vertex));
			commandList->writeBuffer(m_IndexBuffer, m_Indices.data(), m_Indices.size() * sizeof(uint32_t));
		});
	}

	void Mesh::GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
			if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
				continue;

			const Vertex& v0 = vertices[i0];
			const Vertex& v1 = vertices[i1];
			const Vertex& v2 = vertices[i2];

			const glm::vec3 edge1 = v1.Position - v0.Position;
			const glm::vec3 edge2 = v2.Position - v0.Position;
			const glm::vec2 deltaUV1 = v1.TexCoord - v0.TexCoord;
			const glm::vec2 deltaUV2 = v2.TexCoord - v0.TexCoord;

			const float determinant = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
			if (std::abs(determinant) < 1e-12f)
				continue;
			const float inverse = 1.0f / determinant;

			const glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * inverse;
			const glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * inverse;
			for (uint32_t index : { i0, i1, i2 })
			{
				tangents[index] += tangent;
				bitangents[index] += bitangent;
			}
		}

		for (size_t i = 0; i < vertices.size(); i++)
		{
			const glm::vec3& normal = vertices[i].Normal;
			glm::vec3 tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);
			if (glm::dot(tangent, tangent) < 1e-12f)
			{
				// Degenerate UVs: pick any vector perpendicular to the normal.
				tangent = std::abs(normal.x) < 0.9f ? glm::cross(normal, glm::vec3(1, 0, 0)) : glm::cross(normal, glm::vec3(0, 1, 0));
			}
			tangent = glm::normalize(tangent);
			const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
			vertices[i].Tangent = glm::vec4(tangent, handedness);
		}
	}

	const char* PrimitiveMeshToString(PrimitiveMesh primitive)
	{
		switch (primitive)
		{
			case PrimitiveMesh::Cube:		return "Cube";
			case PrimitiveMesh::Sphere:		return "Sphere";
			case PrimitiveMesh::Plane:		return "Plane";
			case PrimitiveMesh::Capsule:	return "Capsule";
			case PrimitiveMesh::Cylinder:	return "Cylinder";
			case PrimitiveMesh::Cone:		return "Cone";
			case PrimitiveMesh::Quad:		return "Quad";
		}
		return "Cube";
	}

	Ref<Mesh> MeshFactory::CreatePrimitive(PrimitiveMesh primitive)
	{
		switch (primitive)
		{
			case PrimitiveMesh::Cube:		return CreateCube();
			case PrimitiveMesh::Sphere:		return CreateSphere();
			case PrimitiveMesh::Plane:		return CreatePlane();
			case PrimitiveMesh::Capsule:	return CreateCapsule();
			case PrimitiveMesh::Cylinder:	return CreateCylinder();
			case PrimitiveMesh::Cone:		return CreateCone();
			case PrimitiveMesh::Quad:		return CreateQuad();
		}
		return CreateCube();
	}

	namespace {

		// Appends a quad (two CCW triangles) with corners a, b, c, d in counter-clockwise order seen from the front.
		void AddQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
			const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& normal)
		{
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ a, normal, glm::vec4(0.0f), { 0.0f, 1.0f } });
			vertices.push_back({ b, normal, glm::vec4(0.0f), { 1.0f, 1.0f } });
			vertices.push_back({ c, normal, glm::vec4(0.0f), { 1.0f, 0.0f } });
			vertices.push_back({ d, normal, glm::vec4(0.0f), { 0.0f, 0.0f } });
			indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}

		Ref<Mesh> FinalizeMesh(const std::string& name, std::vector<Vertex> vertices, std::vector<uint32_t> indices)
		{
			Mesh::GenerateTangents(vertices, indices);
			return CreateRef<Mesh>(name, std::move(vertices), std::move(indices), std::vector<Submesh> {}, std::vector<Ref<Material>> {});
		}

	}

	Ref<Mesh> MeshFactory::CreateCube()
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		const float h = 0.5f;

		AddQuad(vertices, indices, { -h, -h,  h }, {  h, -h,  h }, {  h,  h,  h }, { -h,  h,  h }, {  0,  0,  1 });	// +Z
		AddQuad(vertices, indices, {  h, -h, -h }, { -h, -h, -h }, { -h,  h, -h }, {  h,  h, -h }, {  0,  0, -1 });	// -Z
		AddQuad(vertices, indices, {  h, -h,  h }, {  h, -h, -h }, {  h,  h, -h }, {  h,  h,  h }, {  1,  0,  0 });	// +X
		AddQuad(vertices, indices, { -h, -h, -h }, { -h, -h,  h }, { -h,  h,  h }, { -h,  h, -h }, { -1,  0,  0 });	// -X
		AddQuad(vertices, indices, { -h,  h,  h }, {  h,  h,  h }, {  h,  h, -h }, { -h,  h, -h }, {  0,  1,  0 });	// +Y
		AddQuad(vertices, indices, { -h, -h, -h }, {  h, -h, -h }, {  h, -h,  h }, { -h, -h,  h }, {  0, -1,  0 });	// -Y

		return FinalizeMesh("Cube", std::move(vertices), std::move(indices));
	}

	Ref<Mesh> MeshFactory::CreateSphere(uint32_t segments, uint32_t rings)
	{
		segments = std::max(segments, 3u);
		rings = std::max(rings, 2u);
		const float radius = 0.5f;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		for (uint32_t ring = 0; ring <= rings; ring++)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(rings);
			const float theta = v * glm::pi<float>();
			for (uint32_t segment = 0; segment <= segments; segment++)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(segments);
				const float phi = u * glm::two_pi<float>();
				const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));
				vertices.push_back({ normal * radius, normal, glm::vec4(0.0f), { u, v } });
			}
		}

		const uint32_t stride = segments + 1;
		for (uint32_t ring = 0; ring < rings; ring++)
		{
			for (uint32_t segment = 0; segment < segments; segment++)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + stride;
				indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}
		return FinalizeMesh("Sphere", std::move(vertices), std::move(indices));
	}

	Ref<Mesh> MeshFactory::CreatePlane(uint32_t subdivisions)
	{
		subdivisions = std::max(subdivisions, 1u);
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t z = 0; z <= subdivisions; z++)
		{
			for (uint32_t x = 0; x <= subdivisions; x++)
			{
				const float u = static_cast<float>(x) / static_cast<float>(subdivisions);
				const float v = static_cast<float>(z) / static_cast<float>(subdivisions);
				vertices.push_back({ { u - 0.5f, 0.0f, v - 0.5f }, { 0.0f, 1.0f, 0.0f }, glm::vec4(0.0f), { u, v } });
			}
		}

		const uint32_t stride = subdivisions + 1;
		for (uint32_t z = 0; z < subdivisions; z++)
		{
			for (uint32_t x = 0; x < subdivisions; x++)
			{
				const uint32_t a = z * stride + x;
				const uint32_t b = a + stride;
				indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}
		return FinalizeMesh("Plane", std::move(vertices), std::move(indices));
	}

	Ref<Mesh> MeshFactory::CreateCapsule(uint32_t segments, uint32_t rings)
	{
		// Radius 0.5, total height 2 (cylinder section of height 1).
		segments = std::max(segments, 3u);
		rings = std::max(rings, 2u);
		const float radius = 0.5f;
		const float halfCylinder = 0.5f;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		// Latitude rings: top hemisphere, then bottom hemisphere, sharing the cylinder seam.
		const uint32_t totalRings = rings * 2 + 1;
		for (uint32_t ring = 0; ring <= totalRings; ring++)
		{
			float theta;
			float yOffset;
			if (ring <= rings)
			{
				theta = (static_cast<float>(ring) / static_cast<float>(rings)) * glm::half_pi<float>();
				yOffset = halfCylinder;
			}
			else
			{
				theta = glm::half_pi<float>() + (static_cast<float>(ring - rings - 1) / static_cast<float>(rings)) * glm::half_pi<float>();
				yOffset = -halfCylinder;
			}

			const float v = static_cast<float>(ring) / static_cast<float>(totalRings);
			for (uint32_t segment = 0; segment <= segments; segment++)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(segments);
				const float phi = u * glm::two_pi<float>();
				const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));
				const glm::vec3 position = normal * radius + glm::vec3(0.0f, yOffset, 0.0f);
				vertices.push_back({ position, normal, glm::vec4(0.0f), { u, v } });
			}
		}

		const uint32_t stride = segments + 1;
		for (uint32_t ring = 0; ring < totalRings; ring++)
		{
			for (uint32_t segment = 0; segment < segments; segment++)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + stride;
				indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}
		return FinalizeMesh("Capsule", std::move(vertices), std::move(indices));
	}

	Ref<Mesh> MeshFactory::CreateCylinder(uint32_t segments)
	{
		// Radius 0.5, height 1.
		segments = std::max(segments, 3u);
		const float radius = 0.5f;
		const float halfHeight = 0.5f;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		// Side
		for (uint32_t segment = 0; segment <= segments; segment++)
		{
			const float u = static_cast<float>(segment) / static_cast<float>(segments);
			const float phi = u * glm::two_pi<float>();
			const glm::vec3 normal(std::cos(phi), 0.0f, -std::sin(phi));
			vertices.push_back({ normal * radius + glm::vec3(0.0f, halfHeight, 0.0f), normal, glm::vec4(0.0f), { u, 0.0f } });
			vertices.push_back({ normal * radius - glm::vec3(0.0f, halfHeight, 0.0f), normal, glm::vec4(0.0f), { u, 1.0f } });
		}
		for (uint32_t segment = 0; segment < segments; segment++)
		{
			const uint32_t top = segment * 2;
			const uint32_t bottom = top + 1;
			indices.insert(indices.end(), { top, bottom, top + 2, top + 2, bottom, bottom + 2 });
		}

		// Caps
		for (int side = 0; side < 2; side++)
		{
			const float y = side == 0 ? halfHeight : -halfHeight;
			const glm::vec3 normal(0.0f, side == 0 ? 1.0f : -1.0f, 0.0f);
			const uint32_t center = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ { 0.0f, y, 0.0f }, normal, glm::vec4(0.0f), { 0.5f, 0.5f } });
			for (uint32_t segment = 0; segment <= segments; segment++)
			{
				const float phi = static_cast<float>(segment) / static_cast<float>(segments) * glm::two_pi<float>();
				const glm::vec3 position(std::cos(phi) * radius, y, -std::sin(phi) * radius);
				vertices.push_back({ position, normal, glm::vec4(0.0f), { std::cos(phi) * 0.5f + 0.5f, std::sin(phi) * 0.5f + 0.5f } });
			}
			for (uint32_t segment = 0; segment < segments; segment++)
			{
				const uint32_t a = center + 1 + segment;
				if (side == 0)
					indices.insert(indices.end(), { center, a, a + 1 });
				else
					indices.insert(indices.end(), { center, a + 1, a });
			}
		}
		return FinalizeMesh("Cylinder", std::move(vertices), std::move(indices));
	}

	Ref<Mesh> MeshFactory::CreateCone(uint32_t segments)
	{
		// Base radius 0.5 at y = -0.5, apex at y = +0.5.
		segments = std::max(segments, 3u);
		const float radius = 0.5f;
		const float halfHeight = 0.5f;
		const float slope = radius / (2.0f * halfHeight);

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t segment = 0; segment <= segments; segment++)
		{
			const float u = static_cast<float>(segment) / static_cast<float>(segments);
			const float phi = u * glm::two_pi<float>();
			const glm::vec3 radial(std::cos(phi), 0.0f, -std::sin(phi));
			const glm::vec3 normal = glm::normalize(radial + glm::vec3(0.0f, slope, 0.0f));
			vertices.push_back({ { 0.0f, halfHeight, 0.0f }, normal, glm::vec4(0.0f), { u, 0.0f } });
			vertices.push_back({ radial * radius - glm::vec3(0.0f, halfHeight, 0.0f), normal, glm::vec4(0.0f), { u, 1.0f } });
		}
		for (uint32_t segment = 0; segment < segments; segment++)
		{
			const uint32_t apex = segment * 2;
			const uint32_t base = apex + 1;
			indices.insert(indices.end(), { apex, base, base + 2 });
		}

		const uint32_t center = static_cast<uint32_t>(vertices.size());
		vertices.push_back({ { 0.0f, -halfHeight, 0.0f }, { 0.0f, -1.0f, 0.0f }, glm::vec4(0.0f), { 0.5f, 0.5f } });
		for (uint32_t segment = 0; segment <= segments; segment++)
		{
			const float phi = static_cast<float>(segment) / static_cast<float>(segments) * glm::two_pi<float>();
			const glm::vec3 position(std::cos(phi) * radius, -halfHeight, -std::sin(phi) * radius);
			vertices.push_back({ position, { 0.0f, -1.0f, 0.0f }, glm::vec4(0.0f), { std::cos(phi) * 0.5f + 0.5f, std::sin(phi) * 0.5f + 0.5f } });
		}
		for (uint32_t segment = 0; segment < segments; segment++)
		{
			const uint32_t a = center + 1 + segment;
			indices.insert(indices.end(), { center, a + 1, a });
		}
		return FinalizeMesh("Cone", std::move(vertices), std::move(indices));
	}

	Ref<Mesh> MeshFactory::CreateQuad()
	{
		// 1x1 quad in the XY plane facing +Z.
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		AddQuad(vertices, indices, { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f });
		return FinalizeMesh("Quad", std::move(vertices), std::move(indices));
	}

}
