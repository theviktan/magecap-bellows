#include "bwpch.h"
#include "Bellows/Navigation/SceneNavigation.h"

#include "Bellows/Core/Buffer.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <glm/gtc/quaternion.hpp>

namespace Bellows {

	namespace {

		// Places shape-space points the way PhysicsWorld places a body's shapes: scaled sizes, turned
		// and moved by the entity's world transform.
		struct ShapePlacement
		{
			glm::vec3 Translation { 0.0f };
			glm::quat Rotation { 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 Scale { 1.0f };
			glm::vec3 Offset { 0.0f };		// Already scaled

			glm::vec3 operator()(const glm::vec3& point) const { return Translation + Rotation * (Offset + point); }
		};

		void AddBox(NavMeshGeometry& geometry, const ShapePlacement& place, const glm::vec3& halfExtents)
		{
			glm::vec3 corners[8];
			for (int i = 0; i < 8; i++)
				corners[i] = place(glm::vec3(i & 1 ? halfExtents.x : -halfExtents.x, i & 2 ? halfExtents.y : -halfExtents.y, i & 4 ? halfExtents.z : -halfExtents.z));
			// Two counter-clockwise triangles (seen from outside) per face.
			static const int c_Faces[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
			for (const auto& face : c_Faces)
			{
				geometry.AddTriangle(corners[face[0]], corners[face[1]], corners[face[2]]);
				geometry.AddTriangle(corners[face[0]], corners[face[2]], corners[face[3]]);
			}
		}

		// A round shape as rings around the Y axis, from the bottom up: a sphere, or a capsule when its
		// rings are split by a straight middle.
		void AddRings(NavMeshGeometry& geometry, const ShapePlacement& place, float radius, float halfHeight)
		{
			constexpr int c_Slices = 12;
			constexpr int c_Stacks = 6;		// Per hemisphere
			std::vector<std::pair<float, float>> rings;	// y, ring radius
			for (int i = 0; i <= c_Stacks; i++)
			{
				const float angle = glm::half_pi<float>() * static_cast<float>(i) / c_Stacks;
				rings.emplace_back(-halfHeight - radius * std::cos(angle), radius * std::sin(angle));
			}
			for (int i = 0; i <= c_Stacks; i++)
			{
				const float angle = glm::half_pi<float>() * static_cast<float>(i) / c_Stacks;
				rings.emplace_back(halfHeight + radius * std::sin(angle), radius * std::cos(angle));
			}
			auto point = [&](size_t ring, int slice)
			{
				const float angle = glm::two_pi<float>() * static_cast<float>(slice) / c_Slices;
				return place(glm::vec3(std::cos(angle) * rings[ring].second, rings[ring].first, std::sin(angle) * rings[ring].second));
			};
			for (size_t ring = 0; ring + 1 < rings.size(); ring++)
			{
				for (int slice = 0; slice < c_Slices; slice++)
				{
					const glm::vec3 a = point(ring, slice), b = point(ring, slice + 1), c = point(ring + 1, slice + 1), d = point(ring + 1, slice);
					geometry.AddTriangle(a, c, b);
					geometry.AddTriangle(a, d, c);
				}
			}
		}

	}

	NavMeshGeometry CollectNavigationGeometry(Scene& scene)
	{
		NavMeshGeometry geometry;
		for (entt::entity handle : scene.GetAllEntitiesWith<RigidBodyComponent>())
		{
			Entity entity(handle, &scene);
			const RigidBodyComponent& body = entity.GetComponent<RigidBodyComponent>();
			if (body.Type != RigidBodyType::Static || body.IsTrigger)
				continue;

			ShapePlacement place;
			if (!Math::DecomposeTransform(scene.GetWorldTransform(entity), place.Translation, place.Rotation, place.Scale))
				continue;
			place.Scale = glm::abs(place.Scale);

			if (const auto* box = entity.TryGetComponent<BoxColliderComponent>())
			{
				place.Offset = box->Offset * place.Scale;
				AddBox(geometry, place, glm::max(box->HalfExtents * place.Scale, glm::vec3(0.001f)));
			}
			if (const auto* sphere = entity.TryGetComponent<SphereColliderComponent>())
			{
				place.Offset = sphere->Offset * place.Scale;
				AddRings(geometry, place, std::max(sphere->Radius * std::max({ place.Scale.x, place.Scale.y, place.Scale.z }), 0.001f), 0.0f);
			}
			if (const auto* capsule = entity.TryGetComponent<CapsuleColliderComponent>())
			{
				place.Offset = capsule->Offset * place.Scale;
				AddRings(geometry, place, std::max(capsule->Radius * std::max(place.Scale.x, place.Scale.z), 0.001f),
					std::max(capsule->HalfHeight * place.Scale.y, 0.001f));
			}
			if (entity.HasComponent<MeshColliderComponent>())
			{
				const auto* meshRenderer = entity.TryGetComponent<MeshRendererComponent>();
				if (!meshRenderer || !meshRenderer->RuntimeMesh)
					continue;
				const Mesh& mesh = *meshRenderer->RuntimeMesh;
				place.Offset = glm::vec3(0.0f);
				for (const Submesh& submesh : mesh.GetSubmeshes())
				{
					const glm::mat4 vertexTransform = glm::scale(glm::mat4(1.0f), place.Scale) * submesh.Transform;
					const auto corner = [&](uint32_t i)
					{
						const uint32_t index = mesh.GetIndices()[submesh.BaseIndex + i];
						return place(glm::vec3(vertexTransform * glm::vec4(mesh.GetVertices()[submesh.BaseVertex + index].Position, 1.0f)));
					};
					for (uint32_t i = 0; i + 2 < submesh.IndexCount; i += 3)
						geometry.AddTriangle(corner(i), corner(i + 1), corner(i + 2));
				}
			}
		}
		return geometry;
	}

	std::filesystem::path GetNavMeshPath(const std::filesystem::path& sceneFile)
	{
		std::filesystem::path path = sceneFile;
		path.replace_extension(".bnav");
		return path;
	}

	Ref<NavMesh> LoadSceneNavMesh(const std::filesystem::path& sceneFile)
	{
		const std::filesystem::path path = GetNavMeshPath(sceneFile);
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error))
			return nullptr;
		const Buffer file = FileSystem::ReadBinaryFile(path);
		const std::vector<uint8_t> bytes(file.Data(), file.Data() + file.Size());
		std::string reason;
		Scope<NavMesh> navMesh = NavMesh::Load(bytes, reason);
		if (!navMesh)
		{
			BW_CORE_ERROR("Navigation: '{}' can't be used: {}. Bake the scene's navigation again", path.filename().string(), reason);
			return nullptr;
		}
		return Ref<NavMesh>(std::move(navMesh));
	}

	bool SaveSceneNavMesh(const NavMesh& navMesh, const std::filesystem::path& sceneFile)
	{
		const std::vector<uint8_t> bytes = navMesh.Save();
		const std::filesystem::path path = GetNavMeshPath(sceneFile);
		if (!FileSystem::WriteBinaryFile(path, bytes.data(), bytes.size()))
		{
			BW_CORE_ERROR("Navigation: couldn't write '{}'", path.string());
			return false;
		}
		return true;
	}

}
