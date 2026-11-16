#pragma once

#include "Bellows/Core/UUID.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Scene/ScriptField.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <string>
#include <vector>

namespace Bellows {

	// -------------------------------------------------------------------------
	// Core components (every entity has ID, Tag, Transform and Relationship)
	// -------------------------------------------------------------------------

	struct IDComponent
	{
		UUID ID;
	};

	struct TagComponent
	{
		std::string Tag;
	};

	// Local transform relative to the parent entity (or the world for root entities).
	struct TransformComponent
	{
		glm::vec3 Translation { 0.0f };
		glm::vec3 Scale { 1.0f };

		TransformComponent() = default;
		explicit TransformComponent(const glm::vec3& translation)
			: Translation(translation) {}

		glm::mat4 GetTransform() const;
		void SetTransform(const glm::mat4& transform);

		const glm::quat& GetRotation() const { return m_Rotation; }
		void SetRotation(const glm::quat& rotation);

		// Euler angles in radians (XYZ order). The Euler values are kept alongside the quaternion
		// so editing them never flips between equivalent representations.
		const glm::vec3& GetRotationEuler() const { return m_RotationEuler; }
		void SetRotationEuler(const glm::vec3& euler);

		glm::vec3 GetForward() const { return m_Rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
		glm::vec3 GetRight() const { return m_Rotation * glm::vec3(1.0f, 0.0f, 0.0f); }
		glm::vec3 GetUp() const { return m_Rotation * glm::vec3(0.0f, 1.0f, 0.0f); }
	private:
		glm::quat m_Rotation { 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 m_RotationEuler { 0.0f };
	};

	struct RelationshipComponent
	{
		UUID Parent = 0;
		std::vector<UUID> Children;
	};

	// -------------------------------------------------------------------------
	// Rendering
	// -------------------------------------------------------------------------

	// Mesh paths are project-relative ("Models/Ship.glb") or built-in ("Builtin/Cube").
	struct MeshRendererComponent
	{
		std::string MeshPath = "Builtin/Cube";
		std::string MaterialPath;	// Optional .bmat override for every submesh; empty = the mesh's own materials
		bool CastShadows = true;
		bool Visible = true;

		// Runtime cache (not serialized)
		Ref<Mesh> RuntimeMesh;
		Ref<Material> RuntimeMaterial;
		std::string LoadedMeshPath;
		std::string LoadedMaterialPath;
	};

	enum class ProjectionType : uint8_t
	{
		Perspective = 0,
		Orthographic,
	};

	struct CameraComponent
	{
		ProjectionType Projection = ProjectionType::Perspective;
		float FieldOfView = glm::radians(60.0f);	// Vertical, radians
		float OrthographicSize = 10.0f;				// Vertical extent in world units
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
		bool Primary = true;

		glm::mat4 GetProjection(float aspectRatio) const;
	};

	// Shines along the entity's forward direction (-Z).
	struct DirectionalLightComponent
	{
		glm::vec3 Color { 1.0f };
		float Intensity = 3.0f;
		bool CastShadows = true;
	};

	struct PointLightComponent
	{
		glm::vec3 Color { 1.0f };
		float Intensity = 10.0f;
		float Range = 10.0f;
	};

	// Shines along the entity's forward direction (-Z).
	struct SpotLightComponent
	{
		glm::vec3 Color { 1.0f };
		float Intensity = 10.0f;
		float Range = 10.0f;
		float InnerAngle = glm::radians(20.0f);
		float OuterAngle = glm::radians(30.0f);
	};

	// Environment lighting and background. An empty EnvironmentPath uses the procedural sky.
	struct SkyLightComponent
	{
		std::string EnvironmentPath;	// Equirectangular .hdr, e.g. from polyhaven.com
		float Intensity = 1.0f;
		float Rotation = 0.0f;			// Radians around +Y
		float SkyboxBlur = 0.0f;
		ProceduralSkySettings Sky;

		// Runtime cache (not serialized)
		Ref<Environment> RuntimeEnvironment;
		std::string LoadedEnvironmentPath;
		ProceduralSkySettings LoadedSky;
	};

	// -------------------------------------------------------------------------
	// Physics
	// -------------------------------------------------------------------------

	enum class RigidBodyType : uint8_t
	{
		Static = 0,
		Dynamic,
		Kinematic,
	};

	struct RigidBodyComponent
	{
		RigidBodyType Type = RigidBodyType::Dynamic;
		float Mass = 1.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityScale = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		bool IsTrigger = false;		// Detects overlaps without colliding
		bool LockRotationX = false;
		bool LockRotationY = false;
		bool LockRotationZ = false;
		bool ContinuousCollision = false;

		// Runtime (not serialized)
		uint32_t RuntimeBodyID = 0xFFFFFFFF;
	};

	struct BoxColliderComponent
	{
		glm::vec3 HalfExtents { 0.5f };
		glm::vec3 Offset { 0.0f };
	};

	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Offset { 0.0f };
	};

	// Capsule along the local Y axis. Total height = 2 * (HalfHeight + Radius).
	struct CapsuleColliderComponent
	{
		float Radius = 0.5f;
		float HalfHeight = 0.5f;
		glm::vec3 Offset { 0.0f };
	};

	// Triangle-mesh collider built from the entity's MeshRenderer. Static bodies only.
	struct MeshColliderComponent
	{
		bool UseMeshRenderer = true;
	};

	// -------------------------------------------------------------------------
	// Scripting and audio
	// -------------------------------------------------------------------------

	// A Python class (subclass of bellows.Script) attached to the entity.
	struct ScriptComponent
	{
		std::string ScriptPath;		// Project-relative .py file, e.g. "Scripts/Player.py"
		std::string ClassName;		// Class inside the module; empty = first bellows.Script subclass
		std::map<std::string, ScriptFieldValue> Fields;	// Overrides of the class's exposed fields
	};

	struct AudioSourceComponent
	{
		std::string ClipPath;		// .wav / .mp3 / .flac / .ogg
		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool Loop = false;
		bool PlayOnStart = false;
		bool Spatial = true;
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;
		std::string Bus = "Effects";	// "Master", "Music" or "Effects" (see AudioEngine)
	};

	struct AudioListenerComponent
	{
		bool Active = true;
	};

	// Marks an entity instantiated from a prefab file.
	struct PrefabComponent
	{
		std::string PrefabPath;
	};

	// Lists every component type the scene copies, serializes and exposes to scripts.
	template<typename... Components>
	struct ComponentGroup
	{
	};

	using AllComponents = ComponentGroup<TransformComponent, RelationshipComponent, MeshRendererComponent, CameraComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent,
		RigidBodyComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent,
		ScriptComponent, AudioSourceComponent, AudioListenerComponent, PrefabComponent>;

	const char* RigidBodyTypeToString(RigidBodyType type);
	RigidBodyType RigidBodyTypeFromString(std::string_view text);

}
