#include <doctest/doctest.h>

#include "Bellows/Renderer/Mesh.h"

using namespace Bellows;

TEST_CASE("Primitive meshes are closed, consistently wound and unit sized")
{
	for (uint8_t i = 0; i <= static_cast<uint8_t>(PrimitiveMesh::Quad); i++)
	{
		const PrimitiveMesh primitive = static_cast<PrimitiveMesh>(i);
		CAPTURE(PrimitiveMeshToString(primitive));

		Ref<Mesh> mesh = MeshFactory::CreatePrimitive(primitive);
		REQUIRE(mesh);
		const auto& vertices = mesh->GetVertices();
		const auto& indices = mesh->GetIndices();
		REQUIRE(!vertices.empty());
		REQUIRE(indices.size() % 3 == 0);
		REQUIRE(mesh->GetSubmeshes().size() == 1);

		// Every triangle's geometric normal agrees with its vertex normals (counter-clockwise front faces).
		uint32_t disagreements = 0;
		for (size_t t = 0; t < indices.size(); t += 3)
		{
			const Vertex& a = vertices[indices[t]];
			const Vertex& b = vertices[indices[t + 1]];
			const Vertex& c = vertices[indices[t + 2]];
			const glm::vec3 faceNormal = glm::cross(b.Position - a.Position, c.Position - a.Position);
			if (glm::length(faceNormal) < 1e-8f)
				continue;
			if (glm::dot(faceNormal, a.Normal + b.Normal + c.Normal) <= 0.0f)
				disagreements++;
		}
		CHECK(disagreements == 0);

		for (const Vertex& vertex : vertices)
		{
			CHECK(glm::length(vertex.Normal) == doctest::Approx(1.0f).epsilon(1e-3));
			CHECK(glm::length(glm::vec3(vertex.Tangent)) == doctest::Approx(1.0f).epsilon(1e-3));
			CHECK(std::abs(glm::dot(glm::vec3(vertex.Tangent), vertex.Normal)) < 1e-3f);
		}

		const AABB& bounds = mesh->GetBoundingBox();
		CHECK(bounds.Max.x == doctest::Approx(0.5f).epsilon(1e-3));
		CHECK(bounds.Min.x == doctest::Approx(-0.5f).epsilon(1e-3));
	}
}

TEST_CASE("Capsule is two units tall")
{
	Ref<Mesh> capsule = MeshFactory::CreateCapsule();
	CHECK(capsule->GetBoundingBox().Max.y == doctest::Approx(1.0f).epsilon(1e-3));
	CHECK(capsule->GetBoundingBox().Min.y == doctest::Approx(-1.0f).epsilon(1e-3));
}

TEST_CASE("Tangent generation follows the UV layout")
{
	std::vector<Vertex> vertices = {
		{ { 0, 0, 0 }, { 0, 0, 1 }, {}, { 0, 0 } },
		{ { 1, 0, 0 }, { 0, 0, 1 }, {}, { 1, 0 } },
		{ { 0, 1, 0 }, { 0, 0, 1 }, {}, { 0, 1 } },
	};
	const std::vector<uint32_t> indices = { 0, 1, 2 };
	Mesh::GenerateTangents(vertices, indices);
	for (const Vertex& vertex : vertices)
	{
		CHECK(vertex.Tangent.x == doctest::Approx(1.0f));
		CHECK(vertex.Tangent.w == doctest::Approx(1.0f));
	}
}
