#include "bwpch.h"
#include "Bellows/Scripting/ScriptGlue.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace py = pybind11;

namespace Bellows::Scripting {

	// -------------------------------------------------------------------------
	// Shared state
	// -------------------------------------------------------------------------

	namespace {

		Scene* s_ContextScene = nullptr;
		std::function<void(int)> s_QuitHandler;
		std::function<bool(const std::string&)> s_LoadSceneHandler;
		float s_DeltaTime = 0.0f;
		float s_Time = 0.0f;
		uint64_t s_Frame = 0;
		uint64_t s_RuntimeMaterialCounter = 0;

	}

	PyEntity::PyEntity(Entity entity)
		: SceneID(entity ? entity.GetScene()->GetID() : 0), ID(entity ? entity.GetUUID() : UUID(0))
	{
	}

	Scene* PyEntity::GetScene() const
	{
		return Scene::GetSceneByID(SceneID);
	}

	Entity PyEntity::TryResolve() const
	{
		Scene* scene = GetScene();
		return scene ? scene->TryGetEntityByUUID(ID) : Entity();
	}

	Entity PyEntity::Resolve() const
	{
		Entity entity = TryResolve();
		if (!entity)
			throw std::runtime_error("This entity no longer exists (it was destroyed or its scene was unloaded)");
		return entity;
	}

	void SetContextScene(Scene* scene) { s_ContextScene = scene; }

	Scene* GetContextScene()
	{
		if (s_ContextScene && Scene::GetSceneByID(s_ContextScene->GetID()) == s_ContextScene)
			return s_ContextScene;
		return nullptr;
	}

	Scene& RequireContextScene()
	{
		Scene* scene = GetContextScene();
		if (!scene)
			throw std::runtime_error("No scene is active");
		return *scene;
	}

	void SetQuitHandler(std::function<void(int)> handler) { s_QuitHandler = std::move(handler); }
	void SetLoadSceneHandler(std::function<bool(const std::string&)> handler) { s_LoadSceneHandler = std::move(handler); }

	void RequestQuit(int exitCode)
	{
		if (s_QuitHandler)
			s_QuitHandler(exitCode);
	}

	bool RequestLoadScene(const std::string& path)
	{
		return s_LoadSceneHandler ? s_LoadSceneHandler(path) : false;
	}

	void SetFrameTiming(float deltaTime, float time, uint64_t frame)
	{
		s_DeltaTime = deltaTime;
		s_Time = time;
		s_Frame = frame;
	}

	void LinkBindings()
	{
	}

	// -------------------------------------------------------------------------
	// Component proxies: every access re-fetches the component, so proxies never dangle.
	// -------------------------------------------------------------------------

	namespace {

		const char* ComponentDisplayName(const std::type_info& type);

		template<typename T>
		struct ComponentRef
		{
			PyEntity Owner;

			T& Get() const
			{
				Entity entity = Owner.Resolve();
				if (!entity.HasComponent<T>())
					throw std::runtime_error("Entity '" + entity.GetName() + "' no longer has a " + ComponentDisplayName(typeid(T)) + " component");
				return entity.GetComponent<T>();
			}

			Entity GetEntity() const { return Owner.Resolve(); }
		};

		using TransformRef = ComponentRef<TransformComponent>;
		using MeshRendererRef = ComponentRef<MeshRendererComponent>;
		using CameraRef = ComponentRef<CameraComponent>;
		using DirectionalLightRef = ComponentRef<DirectionalLightComponent>;
		using PointLightRef = ComponentRef<PointLightComponent>;
		using SpotLightRef = ComponentRef<SpotLightComponent>;
		using SkyLightRef = ComponentRef<SkyLightComponent>;
		using RigidBodyRef = ComponentRef<RigidBodyComponent>;
		using BoxColliderRef = ComponentRef<BoxColliderComponent>;
		using SphereColliderRef = ComponentRef<SphereColliderComponent>;
		using CapsuleColliderRef = ComponentRef<CapsuleColliderComponent>;
		using MeshColliderRef = ComponentRef<MeshColliderComponent>;
		using ScriptRef = ComponentRef<ScriptComponent>;
		using AudioSourceRef = ComponentRef<AudioSourceComponent>;
		using AudioListenerRef = ComponentRef<AudioListenerComponent>;

		struct ComponentInfo
		{
			const char* Name;
			std::function<bool(Entity)> Has;
			std::function<void(Entity)> Add;
			std::function<void(Entity)> Remove;
			std::function<py::object(const PyEntity&)> MakeRef;
		};

		template<typename T>
		ComponentInfo MakeComponentInfo(const char* name)
		{
			return ComponentInfo {
				name,
				[](Entity e) { return e.HasComponent<T>(); },
				[](Entity e) { if (!e.HasComponent<T>()) e.AddComponent<T>(); },
				[](Entity e) { if (e.HasComponent<T>()) e.RemoveComponent<T>(); },
				[](const PyEntity& owner) { return py::cast(ComponentRef<T> { owner }); },
			};
		}

		const std::vector<ComponentInfo>& GetComponentInfos()
		{
			static const std::vector<ComponentInfo> s_Infos = {
				MakeComponentInfo<TransformComponent>("Transform"),
				MakeComponentInfo<MeshRendererComponent>("MeshRenderer"),
				MakeComponentInfo<CameraComponent>("Camera"),
				MakeComponentInfo<DirectionalLightComponent>("DirectionalLight"),
				MakeComponentInfo<PointLightComponent>("PointLight"),
				MakeComponentInfo<SpotLightComponent>("SpotLight"),
				MakeComponentInfo<SkyLightComponent>("SkyLight"),
				MakeComponentInfo<RigidBodyComponent>("RigidBody"),
				MakeComponentInfo<BoxColliderComponent>("BoxCollider"),
				MakeComponentInfo<SphereColliderComponent>("SphereCollider"),
				MakeComponentInfo<CapsuleColliderComponent>("CapsuleCollider"),
				MakeComponentInfo<MeshColliderComponent>("MeshCollider"),
				MakeComponentInfo<ScriptComponent>("Script"),
				MakeComponentInfo<AudioSourceComponent>("AudioSource"),
				MakeComponentInfo<AudioListenerComponent>("AudioListener"),
			};
			return s_Infos;
		}

		const char* ComponentDisplayName(const std::type_info& type)
		{
			if (type == typeid(TransformComponent)) return "Transform";
			if (type == typeid(MeshRendererComponent)) return "MeshRenderer";
			if (type == typeid(CameraComponent)) return "Camera";
			if (type == typeid(DirectionalLightComponent)) return "DirectionalLight";
			if (type == typeid(PointLightComponent)) return "PointLight";
			if (type == typeid(SpotLightComponent)) return "SpotLight";
			if (type == typeid(SkyLightComponent)) return "SkyLight";
			if (type == typeid(RigidBodyComponent)) return "RigidBody";
			if (type == typeid(BoxColliderComponent)) return "BoxCollider";
			if (type == typeid(SphereColliderComponent)) return "SphereCollider";
			if (type == typeid(CapsuleColliderComponent)) return "CapsuleCollider";
			if (type == typeid(MeshColliderComponent)) return "MeshCollider";
			if (type == typeid(ScriptComponent)) return "Script";
			if (type == typeid(AudioSourceComponent)) return "AudioSource";
			if (type == typeid(AudioListenerComponent)) return "AudioListener";
			return "Unknown";
		}

		// Accepts a component name ("RigidBody") or a component class (bellows.RigidBody).
		const ComponentInfo& FindComponentInfo(const py::object& key)
		{
			std::string name;
			if (py::isinstance<py::str>(key))
				name = key.cast<std::string>();
			else if (py::hasattr(key, "__name__"))
				name = key.attr("__name__").cast<std::string>();
			else
				throw py::type_error("Expected a component name or component class");

			// The Script component's Python class is ScriptComponent (bellows.Script is the script base class).
			if (name == "ScriptComponent")
				name = "Script";

			for (const ComponentInfo& info : GetComponentInfos())
			{
				if (name == info.Name)
					return info;
			}

			std::string valid;
			for (const ComponentInfo& info : GetComponentInfos())
				valid += (valid.empty() ? "" : ", ") + std::string(info.Name);
			throw py::value_error("Unknown component '" + name + "'. Valid components: " + valid);
		}

		py::object EntityOrNone(Entity entity)
		{
			return entity ? py::cast(PyEntity(entity)) : py::none();
		}

		// ---------------------------------------------------------------------
		// Value conversion helpers
		// ---------------------------------------------------------------------

		template<typename V>
		V SequenceToVector(const py::sequence& sequence)
		{
			constexpr int count = V::length();
			if (static_cast<int>(py::len(sequence)) != count)
				throw py::value_error("Expected a sequence of " + std::to_string(count) + " numbers");
			V result;
			for (int i = 0; i < count; i++)
				result[i] = sequence[static_cast<size_t>(i)].cast<float>();
			return result;
		}

		glm::vec4 ToColor(const py::object& value)
		{
			if (py::isinstance<glm::vec4>(value))
				return value.cast<glm::vec4>();
			if (py::isinstance<glm::vec3>(value))
				return glm::vec4(value.cast<glm::vec3>(), 1.0f);
			const py::sequence sequence = value.cast<py::sequence>();
			if (py::len(sequence) == 3)
				return glm::vec4(SequenceToVector<glm::vec3>(sequence), 1.0f);
			return SequenceToVector<glm::vec4>(sequence);
		}

		KeyCode ToKeyCode(const py::object& key)
		{
			if (py::isinstance<KeyCode>(key))
				return key.cast<KeyCode>();
			if (py::isinstance<py::str>(key))
			{
				std::string name = key.cast<std::string>();
				std::string normalized;
				for (char c : name)
				{
					if (c != '_' && c != ' ')
						normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}

				py::object enumType = py::module_::import("_bellows").attr("Key");
				for (auto item : enumType.attr("__members__").cast<py::dict>())
				{
					std::string member = item.first.cast<std::string>();
					std::string memberNormalized;
					for (char c : member)
						memberNormalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (memberNormalized == normalized)
						return item.second.cast<KeyCode>();
				}
				// Digits written plainly ("1") map to D1.
				if (normalized.size() == 1 && std::isdigit(static_cast<unsigned char>(normalized[0])))
					return static_cast<KeyCode>(static_cast<int>(KeyCode::D0) + (normalized[0] - '0'));
				throw py::value_error("Unknown key '" + name + "'");
			}
			if (py::isinstance<py::int_>(key))
				return static_cast<KeyCode>(key.cast<int>());
			throw py::type_error("Expected a bellows.Key, a key name or a key code");
		}

		MouseButton ToMouseButton(const py::object& button)
		{
			if (py::isinstance<MouseButton>(button))
				return button.cast<MouseButton>();
			if (py::isinstance<py::int_>(button))
				return static_cast<MouseButton>(button.cast<int>());
			if (py::isinstance<py::str>(button))
			{
				const std::string name = button.cast<std::string>();
				if (name == "left" || name == "Left") return MouseButton::Left;
				if (name == "right" || name == "Right") return MouseButton::Right;
				if (name == "middle" || name == "Middle") return MouseButton::Middle;
			}
			throw py::value_error("Expected bellows.MouseButton, 'left', 'right' or 'middle'");
		}

		// Registers a material under a unique runtime path so MeshRenderers can reference it.
		std::string RegisterRuntimeMaterial(const Ref<Material>& material)
		{
			const std::string path = "Runtime/Material_" + std::to_string(++s_RuntimeMaterialCounter);
			AssetManager::RegisterMaterial(path, material);
			return path;
		}

		struct PyMaterial
		{
			Ref<Material> Instance;
			std::string Path;
		};

		// ---------------------------------------------------------------------
		// Entity helpers
		// ---------------------------------------------------------------------

		glm::vec3 GetWorldPosition(const PyEntity& handle)
		{
			Entity entity = handle.Resolve();
			return entity.GetScene()->GetWorldPosition(entity);
		}

		void TeleportIfSimulated(Entity entity)
		{
			Scene* scene = entity.GetScene();
			if (scene->GetPhysicsWorld() && scene->GetPhysicsWorld()->HasBody(entity))
				scene->GetPhysicsWorld()->TeleportToEntity(entity);
		}

		void SetWorldPosition(const PyEntity& handle, const glm::vec3& position)
		{
			Entity entity = handle.Resolve();
			Scene* scene = entity.GetScene();
			glm::mat4 world = scene->GetWorldTransform(entity);
			world[3] = glm::vec4(position, 1.0f);
			scene->SetWorldTransform(entity, world);
			TeleportIfSimulated(entity);
		}

		void LookAt(const PyEntity& handle, const glm::vec3& target, const glm::vec3& up)
		{
			Entity entity = handle.Resolve();
			Scene* scene = entity.GetScene();
			const glm::mat4 world = scene->GetWorldTransform(entity);
			glm::vec3 position, scale;
			glm::quat rotation;
			Math::DecomposeTransform(world, position, rotation, scale);
			glm::vec3 direction = target - position;
			if (glm::length(direction) < 1e-6f)
				return;
			direction = glm::normalize(direction);
			glm::vec3 worldUp = up;
			if (std::abs(glm::dot(direction, glm::normalize(worldUp))) > 0.999f)
				worldUp = std::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
			const glm::quat look = glm::quatLookAtRH(direction, worldUp);
			scene->SetWorldTransform(entity, Math::ComposeTransform(position, look, scale));
			TeleportIfSimulated(entity);
		}

		PhysicsWorld* GetPhysics(Entity entity)
		{
			return entity.GetScene()->GetPhysicsWorld();
		}

		SceneAudio* GetAudio(Entity entity)
		{
			return entity.GetScene()->GetAudio();
		}

		py::object ScriptFieldToPython(const ScriptFieldValue& value)
		{
			return std::visit([](const auto& v) -> py::object { return py::cast(v); }, value);
		}

	}

	namespace {

		// Pushes edited RigidBody settings to the simulated body (no-op outside play mode).
		void ApplyBodySettings(Entity entity)
		{
			if (PhysicsWorld* physics = GetPhysics(entity))
				physics->ApplyBodySettings(entity);
		}

		// Assigns (or clears, for None) one texture slot of a material from an asset path.
		void SetMaterialTexture(Material& material, const py::object& value, std::string& pathSlot, TextureColorSpace colorSpace,
			void (Material::*setter)(Ref<Texture2D>))
		{
			if (value.is_none() || (py::isinstance<py::str>(value) && value.cast<std::string>().empty()))
			{
				pathSlot.clear();
				(material.*setter)(nullptr);
				return;
			}
			const std::string path = value.cast<std::string>();
			Ref<Texture2D> texture = AssetManager::GetTexture(path, colorSpace);
			if (!texture)
				throw py::value_error("Texture '" + path + "' could not be loaded");
			pathSlot = path;
			(material.*setter)(std::move(texture));
		}

	}

	// -------------------------------------------------------------------------
	// Module definition
	// -------------------------------------------------------------------------

	void RegisterBindings(py::module_& m)
	{
		m.doc() = "MageCap Bellows native scripting API. Import 'bellows' instead of using this module directly.";

		// --- Math -------------------------------------------------------------
		py::class_<glm::vec2>(m, "Vec2", "2D vector")
			.def(py::init<>())
			.def(py::init<float>(), py::arg("scalar"))
			.def(py::init<float, float>(), py::arg("x"), py::arg("y"))
			.def(py::init([](const py::sequence& s) { return SequenceToVector<glm::vec2>(s); }))
			.def_readwrite("x", &glm::vec2::x)
			.def_readwrite("y", &glm::vec2::y)
			.def(py::self + py::self).def(py::self - py::self).def(py::self * float()).def(float() * py::self)
			.def(py::self / float()).def(py::self * py::self).def(-py::self).def(py::self == py::self)
			.def("length", [](const glm::vec2& v) { return glm::length(v); })
			.def("normalized", [](const glm::vec2& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; })
			.def("dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); })
			.def("copy", [](const glm::vec2& v) { return v; })
			.def("__getitem__", [](const glm::vec2& v, int i) { if (i < 0 || i > 1) throw py::index_error(); return v[i]; })
			.def("__len__", [](const glm::vec2&) { return 2; })
			.def("__iter__", [](const glm::vec2& v) { return py::iter(py::make_tuple(v.x, v.y)); })
			.def("__repr__", [](const glm::vec2& v) { return fmt::format("Vec2({:.4g}, {:.4g})", v.x, v.y); });

		py::class_<glm::vec3>(m, "Vec3", "3D vector")
			.def(py::init<>())
			.def(py::init<float>(), py::arg("scalar"))
			.def(py::init<float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"))
			.def(py::init([](const py::sequence& s) { return SequenceToVector<glm::vec3>(s); }))
			.def_readwrite("x", &glm::vec3::x)
			.def_readwrite("y", &glm::vec3::y)
			.def_readwrite("z", &glm::vec3::z)
			.def(py::self + py::self).def(py::self - py::self).def(py::self * float()).def(float() * py::self)
			.def(py::self / float()).def(py::self * py::self).def(-py::self).def(py::self == py::self)
			.def(py::self += py::self).def(py::self -= py::self).def(py::self *= float())
			.def("length", [](const glm::vec3& v) { return glm::length(v); })
			.def("length_squared", [](const glm::vec3& v) { return glm::dot(v, v); })
			.def("normalized", [](const glm::vec3& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; })
			.def("dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); })
			.def("cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); })
			.def("distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); })
			.def("lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); })
			.def("copy", [](const glm::vec3& v) { return v; })
			.def("__getitem__", [](const glm::vec3& v, int i) { if (i < 0 || i > 2) throw py::index_error(); return v[i]; })
			.def("__len__", [](const glm::vec3&) { return 3; })
			.def("__iter__", [](const glm::vec3& v) { return py::iter(py::make_tuple(v.x, v.y, v.z)); })
			.def("__repr__", [](const glm::vec3& v) { return fmt::format("Vec3({:.4g}, {:.4g}, {:.4g})", v.x, v.y, v.z); });

		py::class_<glm::vec4>(m, "Vec4", "4D vector (also used for RGBA colors)")
			.def(py::init<>())
			.def(py::init<float>(), py::arg("scalar"))
			.def(py::init<float, float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"))
			.def(py::init([](const py::sequence& s) { return SequenceToVector<glm::vec4>(s); }))
			.def_readwrite("x", &glm::vec4::x)
			.def_readwrite("y", &glm::vec4::y)
			.def_readwrite("z", &glm::vec4::z)
			.def_readwrite("w", &glm::vec4::w)
			.def_property("r", [](const glm::vec4& v) { return v.r; }, [](glm::vec4& v, float value) { v.r = value; })
			.def_property("g", [](const glm::vec4& v) { return v.g; }, [](glm::vec4& v, float value) { v.g = value; })
			.def_property("b", [](const glm::vec4& v) { return v.b; }, [](glm::vec4& v, float value) { v.b = value; })
			.def_property("a", [](const glm::vec4& v) { return v.a; }, [](glm::vec4& v, float value) { v.a = value; })
			.def(py::self + py::self).def(py::self - py::self).def(py::self * float()).def(float() * py::self)
			.def(py::self / float()).def(py::self * py::self).def(py::self == py::self)
			.def("copy", [](const glm::vec4& v) { return v; })
			.def("__getitem__", [](const glm::vec4& v, int i) { if (i < 0 || i > 3) throw py::index_error(); return v[i]; })
			.def("__len__", [](const glm::vec4&) { return 4; })
			.def("__iter__", [](const glm::vec4& v) { return py::iter(py::make_tuple(v.x, v.y, v.z, v.w)); })
			.def("__repr__", [](const glm::vec4& v) { return fmt::format("Vec4({:.4g}, {:.4g}, {:.4g}, {:.4g})", v.x, v.y, v.z, v.w); });

		py::class_<glm::quat>(m, "Quat", "Rotation quaternion")
			.def(py::init([]() { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); }))
			.def(py::init<float, float, float, float>(), py::arg("w"), py::arg("x"), py::arg("y"), py::arg("z"))
			.def_static("from_euler", [](const glm::vec3& radians) { return glm::quat(radians); }, py::arg("radians"))
			.def_static("from_euler_degrees", [](const glm::vec3& degrees) { return glm::quat(glm::radians(degrees)); }, py::arg("degrees"))
			.def_static("from_axis_angle", [](const glm::vec3& axis, float radians) { return glm::angleAxis(radians, glm::normalize(axis)); },
				py::arg("axis"), py::arg("radians"))
			.def_readwrite("w", &glm::quat::w).def_readwrite("x", &glm::quat::x).def_readwrite("y", &glm::quat::y).def_readwrite("z", &glm::quat::z)
			.def(py::self * py::self)
			.def("__mul__", [](const glm::quat& q, const glm::vec3& v) { return q * v; })
			.def("to_euler", [](const glm::quat& q) { return glm::eulerAngles(q); })
			.def("to_euler_degrees", [](const glm::quat& q) { return glm::degrees(glm::eulerAngles(q)); })
			.def("normalized", [](const glm::quat& q) { return glm::normalize(q); })
			.def("inverse", [](const glm::quat& q) { return glm::inverse(q); })
			.def("slerp", [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); })
			.def("__repr__", [](const glm::quat& q) { return fmt::format("Quat(w={:.4g}, x={:.4g}, y={:.4g}, z={:.4g})", q.w, q.x, q.y, q.z); });

		py::implicitly_convertible<py::tuple, glm::vec2>();
		py::implicitly_convertible<py::list, glm::vec2>();
		py::implicitly_convertible<py::tuple, glm::vec3>();
		py::implicitly_convertible<py::list, glm::vec3>();
		py::implicitly_convertible<py::tuple, glm::vec4>();
		py::implicitly_convertible<py::list, glm::vec4>();

		m.def("lerp", [](float a, float b, float t) { return a + (b - a) * t; }, py::arg("a"), py::arg("b"), py::arg("t"));
		m.def("clamp", [](float value, float low, float high) { return std::clamp(value, low, high); }, py::arg("value"), py::arg("low"), py::arg("high"));

		// --- Input ------------------------------------------------------------
		py::enum_<KeyCode> key(m, "Key", "Keyboard keys");
		key.value("Space", KeyCode::Space).value("Apostrophe", KeyCode::Apostrophe).value("Comma", KeyCode::Comma)
			.value("Minus", KeyCode::Minus).value("Period", KeyCode::Period).value("Slash", KeyCode::Slash)
			.value("D0", KeyCode::D0).value("D1", KeyCode::D1).value("D2", KeyCode::D2).value("D3", KeyCode::D3).value("D4", KeyCode::D4)
			.value("D5", KeyCode::D5).value("D6", KeyCode::D6).value("D7", KeyCode::D7).value("D8", KeyCode::D8).value("D9", KeyCode::D9)
			.value("Semicolon", KeyCode::Semicolon).value("Equal", KeyCode::Equal)
			.value("A", KeyCode::A).value("B", KeyCode::B).value("C", KeyCode::C).value("D", KeyCode::D).value("E", KeyCode::E)
			.value("F", KeyCode::F).value("G", KeyCode::G).value("H", KeyCode::H).value("I", KeyCode::I).value("J", KeyCode::J)
			.value("K", KeyCode::K).value("L", KeyCode::L).value("M", KeyCode::M).value("N", KeyCode::N).value("O", KeyCode::O)
			.value("P", KeyCode::P).value("Q", KeyCode::Q).value("R", KeyCode::R).value("S", KeyCode::S).value("T", KeyCode::T)
			.value("U", KeyCode::U).value("V", KeyCode::V).value("W", KeyCode::W).value("X", KeyCode::X).value("Y", KeyCode::Y)
			.value("Z", KeyCode::Z)
			.value("LeftBracket", KeyCode::LeftBracket).value("Backslash", KeyCode::Backslash).value("RightBracket", KeyCode::RightBracket)
			.value("GraveAccent", KeyCode::GraveAccent)
			.value("Escape", KeyCode::Escape).value("Enter", KeyCode::Enter).value("Tab", KeyCode::Tab).value("Backspace", KeyCode::Backspace)
			.value("Insert", KeyCode::Insert).value("Delete", KeyCode::Delete)
			.value("Right", KeyCode::Right).value("Left", KeyCode::Left).value("Down", KeyCode::Down).value("Up", KeyCode::Up)
			.value("PageUp", KeyCode::PageUp).value("PageDown", KeyCode::PageDown).value("Home", KeyCode::Home).value("End", KeyCode::End)
			.value("CapsLock", KeyCode::CapsLock).value("Pause", KeyCode::Pause)
			.value("F1", KeyCode::F1).value("F2", KeyCode::F2).value("F3", KeyCode::F3).value("F4", KeyCode::F4).value("F5", KeyCode::F5)
			.value("F6", KeyCode::F6).value("F7", KeyCode::F7).value("F8", KeyCode::F8).value("F9", KeyCode::F9).value("F10", KeyCode::F10)
			.value("F11", KeyCode::F11).value("F12", KeyCode::F12)
			.value("KP0", KeyCode::KP0).value("KP1", KeyCode::KP1).value("KP2", KeyCode::KP2).value("KP3", KeyCode::KP3).value("KP4", KeyCode::KP4)
			.value("KP5", KeyCode::KP5).value("KP6", KeyCode::KP6).value("KP7", KeyCode::KP7).value("KP8", KeyCode::KP8).value("KP9", KeyCode::KP9)
			.value("KPEnter", KeyCode::KPEnter).value("KPAdd", KeyCode::KPAdd).value("KPSubtract", KeyCode::KPSubtract)
			.value("LeftShift", KeyCode::LeftShift).value("LeftControl", KeyCode::LeftControl).value("LeftAlt", KeyCode::LeftAlt)
			.value("RightShift", KeyCode::RightShift).value("RightControl", KeyCode::RightControl).value("RightAlt", KeyCode::RightAlt);

		py::enum_<MouseButton>(m, "MouseButton", "Mouse buttons")
			.value("Left", MouseButton::Left).value("Right", MouseButton::Right).value("Middle", MouseButton::Middle)
			.value("Button3", MouseButton::Button3).value("Button4", MouseButton::Button4);

		m.def("is_key_down", [](const py::object& k) { return Input::IsKeyDown(ToKeyCode(k)); }, py::arg("key"), "True while the key is held");
		m.def("is_key_pressed", [](const py::object& k) { return Input::IsKeyPressed(ToKeyCode(k)); }, py::arg("key"), "True on the frame the key went down");
		m.def("is_key_released", [](const py::object& k) { return Input::IsKeyReleased(ToKeyCode(k)); }, py::arg("key"), "True on the frame the key went up");
		m.def("is_mouse_down", [](const py::object& b) { return Input::IsMouseButtonDown(ToMouseButton(b)); }, py::arg("button") = MouseButton::Left);
		m.def("is_mouse_pressed", [](const py::object& b) { return Input::IsMouseButtonPressed(ToMouseButton(b)); }, py::arg("button") = MouseButton::Left);
		m.def("is_mouse_released", [](const py::object& b) { return Input::IsMouseButtonReleased(ToMouseButton(b)); }, py::arg("button") = MouseButton::Left);
		m.def("mouse_position", &Input::GetMousePosition, "Mouse position in viewport pixels (top-left origin)");
		m.def("mouse_delta", &Input::GetMouseDelta, "Mouse movement this frame in pixels");
		m.def("scroll_delta", &Input::GetScrollDelta, "Scroll wheel movement this frame");
		m.def("viewport_size", []() { return Input::GetViewportSize(); }, "Size of the game viewport in pixels");

		// --- Time and logging -------------------------------------------------
		m.def("delta_time", []() { return s_DeltaTime; }, "Seconds since the previous frame");
		m.def("time", []() { return s_Time; }, "Seconds since play started");
		m.def("frame", []() { return s_Frame; }, "Frames since play started");

		auto joinArgs = [](const py::args& args)
		{
			std::string text;
			for (const auto& argument : args)
			{
				if (!text.empty())
					text += ' ';
				text += py::str(argument).cast<std::string>();
			}
			return text;
		};
		m.def("log", [joinArgs](const py::args& args) { BW_INFO("{}", joinArgs(args)); }, "Writes an info message to the console");
		m.def("log_warning", [joinArgs](const py::args& args) { BW_WARN("{}", joinArgs(args)); });
		m.def("log_error", [joinArgs](const py::args& args) { BW_ERROR("{}", joinArgs(args)); });
		m.def("_log_line", [](int level, const std::string& line)
		{
			if (level >= 2)
				BW_ERROR("{}", line);
			else
				BW_INFO("{}", line);
		});

		// --- Materials --------------------------------------------------------
		py::class_<PyMaterial>(m, "Material", "PBR material that can be assigned to a MeshRenderer")
			.def(py::init([](const std::string& name, const py::object& color, float metallic, float roughness)
			{
				PyMaterial material;
				material.Instance = CreateRef<Material>(name);
				MaterialProperties& properties = material.Instance->EditProperties();
				if (!color.is_none())
					properties.AlbedoColor = ToColor(color);
				properties.Metallic = metallic;
				properties.Roughness = roughness;
				material.Path = RegisterRuntimeMaterial(material.Instance);
				return material;
			}), py::arg("name") = "Material", py::arg("color") = py::none(), py::arg("metallic") = 0.0f, py::arg("roughness") = 0.5f)
			.def_static("load", [](const std::string& path)
			{
				Ref<Material> material = AssetManager::GetMaterial(path);
				if (!material)
					throw py::value_error("Material '" + path + "' could not be loaded");
				return PyMaterial { material, path };
			}, py::arg("path"))
			.def_property_readonly("path", [](const PyMaterial& m) { return m.Path; })
			.def_property("name", [](const PyMaterial& m) { return m.Instance->GetName(); }, [](PyMaterial& m, const std::string& v) { m.Instance->SetName(v); })
			.def_property("color", [](const PyMaterial& m) { return m.Instance->GetProperties().AlbedoColor; },
				[](PyMaterial& m, const py::object& v) { m.Instance->EditProperties().AlbedoColor = ToColor(v); })
			.def_property("metallic", [](const PyMaterial& m) { return m.Instance->GetProperties().Metallic; },
				[](PyMaterial& m, float v) { m.Instance->EditProperties().Metallic = std::clamp(v, 0.0f, 1.0f); })
			.def_property("roughness", [](const PyMaterial& m) { return m.Instance->GetProperties().Roughness; },
				[](PyMaterial& m, float v) { m.Instance->EditProperties().Roughness = std::clamp(v, 0.0f, 1.0f); })
			.def_property("emissive", [](const PyMaterial& m) { return m.Instance->GetProperties().EmissiveColor; },
				[](PyMaterial& m, const glm::vec3& v) { m.Instance->EditProperties().EmissiveColor = v; })
			.def_property("emissive_intensity", [](const PyMaterial& m) { return m.Instance->GetProperties().EmissiveIntensity; },
				[](PyMaterial& m, float v) { m.Instance->EditProperties().EmissiveIntensity = std::max(v, 0.0f); })
			.def_property("alpha_mode", [](const PyMaterial& m) { return std::string(AlphaModeToString(m.Instance->GetProperties().Mode)); },
				[](PyMaterial& m, const std::string& v) { m.Instance->EditProperties().Mode = AlphaModeFromString(v); })
			.def_property("double_sided", [](const PyMaterial& m) { return m.Instance->GetProperties().DoubleSided; },
				[](PyMaterial& m, bool v) { m.Instance->EditProperties().DoubleSided = v; })
			.def_property("normal_strength", [](const PyMaterial& m) { return m.Instance->GetProperties().NormalStrength; },
				[](PyMaterial& m, float v) { m.Instance->EditProperties().NormalStrength = std::max(v, 0.0f); })
			.def_property("alpha_cutoff", [](const PyMaterial& m) { return m.Instance->GetProperties().AlphaCutoff; },
				[](PyMaterial& m, float v) { m.Instance->EditProperties().AlphaCutoff = std::clamp(v, 0.0f, 1.0f); })
			.def_property("uv_tiling", [](const PyMaterial& m) { return m.Instance->GetProperties().UVTiling; },
				[](PyMaterial& m, const glm::vec2& v) { m.Instance->EditProperties().UVTiling = v; })
			.def_property("uv_offset", [](const PyMaterial& m) { return m.Instance->GetProperties().UVOffset; },
				[](PyMaterial& m, const glm::vec2& v) { m.Instance->EditProperties().UVOffset = v; })
			.def_property("albedo_map", [](const PyMaterial& m) { return m.Instance->AlbedoMapPath; },
				[](PyMaterial& m, const py::object& v) { SetMaterialTexture(*m.Instance, v, m.Instance->AlbedoMapPath, TextureColorSpace::SRGB, &Material::SetAlbedoMap); },
				"Albedo (base color) texture path, or None")
			.def_property("normal_map", [](const PyMaterial& m) { return m.Instance->NormalMapPath; },
				[](PyMaterial& m, const py::object& v) { SetMaterialTexture(*m.Instance, v, m.Instance->NormalMapPath, TextureColorSpace::Linear, &Material::SetNormalMap); },
				"Tangent-space normal map path, or None")
			.def_property("metallic_roughness_map", [](const PyMaterial& m) { return m.Instance->MetallicRoughnessMapPath; },
				[](PyMaterial& m, const py::object& v) { SetMaterialTexture(*m.Instance, v, m.Instance->MetallicRoughnessMapPath, TextureColorSpace::Linear, &Material::SetMetallicRoughnessMap); },
				"glTF metallic (B) / roughness (G) texture path, or None")
			.def_property("occlusion_map", [](const PyMaterial& m) { return m.Instance->OcclusionMapPath; },
				[](PyMaterial& m, const py::object& v) { SetMaterialTexture(*m.Instance, v, m.Instance->OcclusionMapPath, TextureColorSpace::Linear, &Material::SetOcclusionMap); },
				"Ambient occlusion (R) texture path, or None")
			.def_property("emissive_map", [](const PyMaterial& m) { return m.Instance->EmissiveMapPath; },
				[](PyMaterial& m, const py::object& v) { SetMaterialTexture(*m.Instance, v, m.Instance->EmissiveMapPath, TextureColorSpace::SRGB, &Material::SetEmissiveMap); },
				"Emissive texture path, or None")
			.def("save", [](PyMaterial& m, const std::string& path)
			{
				if (!AssetManager::SaveMaterial(m.Instance, path))
					throw std::runtime_error("Could not save material to '" + path + "'");
				m.Path = path;
			}, py::arg("path"), "Writes the material to a .bmat file in the project")
			.def("__repr__", [](const PyMaterial& m) { return "Material('" + m.Instance->GetName() + "')"; });

		// --- Components -------------------------------------------------------
		py::class_<TransformRef>(m, "Transform", "Position, rotation and scale relative to the parent")
			.def_property("translation", [](const TransformRef& r) { return r.Get().Translation; },
				[](const TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; TeleportIfSimulated(r.GetEntity()); })
			.def_property("position", [](const TransformRef& r) { return r.Get().Translation; },
				[](const TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; TeleportIfSimulated(r.GetEntity()); }, "Local position (same as translation)")
			.def_property("rotation", [](const TransformRef& r) { return r.Get().GetRotationEuler(); },
				[](const TransformRef& r, const glm::vec3& v) { r.Get().SetRotationEuler(v); TeleportIfSimulated(r.GetEntity()); }, "Euler angles in radians")
			.def_property("rotation_degrees", [](const TransformRef& r) { return glm::degrees(r.Get().GetRotationEuler()); },
				[](const TransformRef& r, const glm::vec3& v) { r.Get().SetRotationEuler(glm::radians(v)); TeleportIfSimulated(r.GetEntity()); })
			.def_property("quaternion", [](const TransformRef& r) { return r.Get().GetRotation(); },
				[](const TransformRef& r, const glm::quat& q) { r.Get().SetRotation(q); TeleportIfSimulated(r.GetEntity()); })
			.def_property("scale", [](const TransformRef& r) { return r.Get().Scale; }, [](const TransformRef& r, const glm::vec3& v) { r.Get().Scale = v; })
			.def_property("world_position", [](const TransformRef& r) { return GetWorldPosition(r.Owner); },
				[](const TransformRef& r, const glm::vec3& v) { SetWorldPosition(r.Owner, v); })
			.def_property_readonly("forward", [](const TransformRef& r) { return r.Get().GetForward(); })
			.def_property_readonly("right", [](const TransformRef& r) { return r.Get().GetRight(); })
			.def_property_readonly("up", [](const TransformRef& r) { return r.Get().GetUp(); });

		py::class_<MeshRendererRef>(m, "MeshRenderer", "Draws a mesh (glTF file or Builtin/Cube, Sphere, Plane, Capsule, Cylinder, Cone, Quad)")
			.def_property("mesh", [](const MeshRendererRef& r) { return r.Get().MeshPath; }, [](const MeshRendererRef& r, const std::string& v) { r.Get().MeshPath = v; })
			.def_property("material", [](const MeshRendererRef& r) { return r.Get().MaterialPath; },
				[](const MeshRendererRef& r, const py::object& v)
				{
					if (v.is_none())
						r.Get().MaterialPath.clear();
					else if (py::isinstance<PyMaterial>(v))
						r.Get().MaterialPath = v.cast<PyMaterial>().Path;
					else
						r.Get().MaterialPath = v.cast<std::string>();
				}, "Material path (.bmat or runtime material); None uses the mesh's own materials")
			.def_property("color", [](const MeshRendererRef& r)
				{
					MeshRendererComponent& c = r.Get();
					if (c.RuntimeMaterial) return c.RuntimeMaterial->GetProperties().AlbedoColor;
					if (c.RuntimeMesh && !c.RuntimeMesh->GetMaterials().empty()) return c.RuntimeMesh->GetMaterials()[0]->GetProperties().AlbedoColor;
					return glm::vec4(1.0f);
				},
				[](const MeshRendererRef& r, const py::object& v)
				{
					// Gives this entity its own material instance so other entities are unaffected.
					MeshRendererComponent& c = r.Get();
					const std::string uniquePrefix = "Runtime/Entity_" + std::to_string(static_cast<uint64_t>(r.Owner.ID));
					if (c.MaterialPath != uniquePrefix)
					{
						Ref<Material> base = c.RuntimeMaterial;
						if (!base && c.RuntimeMesh && !c.RuntimeMesh->GetMaterials().empty())
							base = c.RuntimeMesh->GetMaterials()[0];
						Ref<Material> instance = base ? base->Clone() : CreateRef<Material>("Default");
						AssetManager::RegisterMaterial(uniquePrefix, instance);
						c.MaterialPath = uniquePrefix;
						c.RuntimeMaterial = instance;
						c.LoadedMaterialPath = uniquePrefix;
					}
					c.RuntimeMaterial->EditProperties().AlbedoColor = ToColor(v);
				}, "Albedo color; setting it gives the entity its own material")
			.def_property("cast_shadows", [](const MeshRendererRef& r) { return r.Get().CastShadows; }, [](const MeshRendererRef& r, bool v) { r.Get().CastShadows = v; })
			.def_property("visible", [](const MeshRendererRef& r) { return r.Get().Visible; }, [](const MeshRendererRef& r, bool v) { r.Get().Visible = v; });

		py::class_<CameraRef>(m, "Camera")
			.def_property("projection", [](const CameraRef& r) { return std::string(r.Get().Projection == ProjectionType::Orthographic ? "Orthographic" : "Perspective"); },
				[](const CameraRef& r, const std::string& v) { r.Get().Projection = v == "Orthographic" ? ProjectionType::Orthographic : ProjectionType::Perspective; })
			.def_property("fov", [](const CameraRef& r) { return glm::degrees(r.Get().FieldOfView); },
				[](const CameraRef& r, float v) { r.Get().FieldOfView = glm::radians(std::clamp(v, 1.0f, 179.0f)); }, "Vertical field of view in degrees")
			.def_property("orthographic_size", [](const CameraRef& r) { return r.Get().OrthographicSize; }, [](const CameraRef& r, float v) { r.Get().OrthographicSize = std::max(v, 0.001f); })
			.def_property("near_clip", [](const CameraRef& r) { return r.Get().NearClip; }, [](const CameraRef& r, float v) { r.Get().NearClip = std::max(v, 0.0001f); })
			.def_property("far_clip", [](const CameraRef& r) { return r.Get().FarClip; }, [](const CameraRef& r, float v) { r.Get().FarClip = v; })
			.def_property("primary", [](const CameraRef& r) { return r.Get().Primary; }, [](const CameraRef& r, bool v) { r.Get().Primary = v; });

		py::class_<DirectionalLightRef>(m, "DirectionalLight", "Sun-like light shining along the entity's forward (-Z)")
			.def_property("color", [](const DirectionalLightRef& r) { return r.Get().Color; }, [](const DirectionalLightRef& r, const glm::vec3& v) { r.Get().Color = v; })
			.def_property("intensity", [](const DirectionalLightRef& r) { return r.Get().Intensity; }, [](const DirectionalLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); })
			.def_property("cast_shadows", [](const DirectionalLightRef& r) { return r.Get().CastShadows; }, [](const DirectionalLightRef& r, bool v) { r.Get().CastShadows = v; });

		py::class_<PointLightRef>(m, "PointLight")
			.def_property("color", [](const PointLightRef& r) { return r.Get().Color; }, [](const PointLightRef& r, const glm::vec3& v) { r.Get().Color = v; })
			.def_property("intensity", [](const PointLightRef& r) { return r.Get().Intensity; }, [](const PointLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); })
			.def_property("range", [](const PointLightRef& r) { return r.Get().Range; }, [](const PointLightRef& r, float v) { r.Get().Range = std::max(v, 0.01f); });

		py::class_<SpotLightRef>(m, "SpotLight", "Cone light shining along the entity's forward (-Z)")
			.def_property("color", [](const SpotLightRef& r) { return r.Get().Color; }, [](const SpotLightRef& r, const glm::vec3& v) { r.Get().Color = v; })
			.def_property("intensity", [](const SpotLightRef& r) { return r.Get().Intensity; }, [](const SpotLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); })
			.def_property("range", [](const SpotLightRef& r) { return r.Get().Range; }, [](const SpotLightRef& r, float v) { r.Get().Range = std::max(v, 0.01f); })
			.def_property("inner_angle", [](const SpotLightRef& r) { return glm::degrees(r.Get().InnerAngle); }, [](const SpotLightRef& r, float v) { r.Get().InnerAngle = glm::radians(v); }, "Degrees")
			.def_property("outer_angle", [](const SpotLightRef& r) { return glm::degrees(r.Get().OuterAngle); }, [](const SpotLightRef& r, float v) { r.Get().OuterAngle = glm::radians(v); }, "Degrees");

		py::class_<SkyLightRef>(m, "SkyLight", "Environment lighting: an HDRI (.hdr) or the procedural sky")
			.def_property("environment", [](const SkyLightRef& r) { return r.Get().EnvironmentPath; }, [](const SkyLightRef& r, const std::string& v) { r.Get().EnvironmentPath = v; })
			.def_property("intensity", [](const SkyLightRef& r) { return r.Get().Intensity; }, [](const SkyLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); })
			.def_property("rotation", [](const SkyLightRef& r) { return glm::degrees(r.Get().Rotation); }, [](const SkyLightRef& r, float v) { r.Get().Rotation = glm::radians(v); }, "Degrees")
			.def_property("skybox_blur", [](const SkyLightRef& r) { return r.Get().SkyboxBlur; }, [](const SkyLightRef& r, float v) { r.Get().SkyboxBlur = std::max(v, 0.0f); });

		py::class_<RigidBodyRef>(m, "RigidBody", "Physics body. Needs a collider component on the same entity.")
			.def_property("type", [](const RigidBodyRef& r) { return std::string(RigidBodyTypeToString(r.Get().Type)); },
				[](const RigidBodyRef& r, const std::string& v) { r.Get().Type = RigidBodyTypeFromString(v); }, "'Static', 'Dynamic' or 'Kinematic' (set before play)")
			.def_property("mass", [](const RigidBodyRef& r) { return r.Get().Mass; }, [](const RigidBodyRef& r, float v) { r.Get().Mass = std::max(v, 0.001f); })
			.def_property("friction", [](const RigidBodyRef& r) { return r.Get().Friction; }, [](const RigidBodyRef& r, float v) { r.Get().Friction = v; ApplyBodySettings(r.GetEntity()); })
			.def_property("restitution", [](const RigidBodyRef& r) { return r.Get().Restitution; }, [](const RigidBodyRef& r, float v) { r.Get().Restitution = v; ApplyBodySettings(r.GetEntity()); })
			.def_property("linear_damping", [](const RigidBodyRef& r) { return r.Get().LinearDamping; }, [](const RigidBodyRef& r, float v) { r.Get().LinearDamping = v; ApplyBodySettings(r.GetEntity()); })
			.def_property("angular_damping", [](const RigidBodyRef& r) { return r.Get().AngularDamping; }, [](const RigidBodyRef& r, float v) { r.Get().AngularDamping = v; ApplyBodySettings(r.GetEntity()); })
			.def_property("gravity_scale", [](const RigidBodyRef& r) { return r.Get().GravityScale; }, [](const RigidBodyRef& r, float v) { r.Get().GravityScale = v; ApplyBodySettings(r.GetEntity()); })
			.def_property("is_trigger", [](const RigidBodyRef& r) { return r.Get().IsTrigger; }, [](const RigidBodyRef& r, bool v) { r.Get().IsTrigger = v; })
			.def_property("lock_rotation_x", [](const RigidBodyRef& r) { return r.Get().LockRotationX; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationX = v; })
			.def_property("lock_rotation_y", [](const RigidBodyRef& r) { return r.Get().LockRotationY; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationY = v; })
			.def_property("lock_rotation_z", [](const RigidBodyRef& r) { return r.Get().LockRotationZ; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationZ = v; })
			.def_property("continuous", [](const RigidBodyRef& r) { return r.Get().ContinuousCollision; }, [](const RigidBodyRef& r, bool v) { r.Get().ContinuousCollision = v; },
				"Continuous collision detection for fast bodies (set before play)")
			.def_property("velocity",
				[](const RigidBodyRef& r) { Entity e = r.GetEntity(); r.Get(); return GetPhysics(e) ? GetPhysics(e)->GetLinearVelocity(e) : glm::vec3(0.0f); },
				[](const RigidBodyRef& r, const glm::vec3& v) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->SetLinearVelocity(e, v); }, "Linear velocity (play mode)")
			.def_property("angular_velocity",
				[](const RigidBodyRef& r) { Entity e = r.GetEntity(); r.Get(); return GetPhysics(e) ? GetPhysics(e)->GetAngularVelocity(e) : glm::vec3(0.0f); },
				[](const RigidBodyRef& r, const glm::vec3& v) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->SetAngularVelocity(e, v); })
			.def("add_force", [](const RigidBodyRef& r, const glm::vec3& f) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddForce(e, f); }, py::arg("force"))
			.def("add_impulse", [](const RigidBodyRef& r, const glm::vec3& i) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddImpulse(e, i); }, py::arg("impulse"))
			.def("add_torque", [](const RigidBodyRef& r, const glm::vec3& t) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddTorque(e, t); }, py::arg("torque"))
			.def("teleport", [](const RigidBodyRef& r, const glm::vec3& position) { r.Get(); SetWorldPosition(r.Owner, position); }, py::arg("position"));

		py::class_<BoxColliderRef>(m, "BoxCollider")
			.def_property("half_extents", [](const BoxColliderRef& r) { return r.Get().HalfExtents; }, [](const BoxColliderRef& r, const glm::vec3& v) { r.Get().HalfExtents = v; })
			.def_property("offset", [](const BoxColliderRef& r) { return r.Get().Offset; }, [](const BoxColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; });

		py::class_<SphereColliderRef>(m, "SphereCollider")
			.def_property("radius", [](const SphereColliderRef& r) { return r.Get().Radius; }, [](const SphereColliderRef& r, float v) { r.Get().Radius = v; })
			.def_property("offset", [](const SphereColliderRef& r) { return r.Get().Offset; }, [](const SphereColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; });

		py::class_<CapsuleColliderRef>(m, "CapsuleCollider")
			.def_property("radius", [](const CapsuleColliderRef& r) { return r.Get().Radius; }, [](const CapsuleColliderRef& r, float v) { r.Get().Radius = v; })
			.def_property("half_height", [](const CapsuleColliderRef& r) { return r.Get().HalfHeight; }, [](const CapsuleColliderRef& r, float v) { r.Get().HalfHeight = v; })
			.def_property("offset", [](const CapsuleColliderRef& r) { return r.Get().Offset; }, [](const CapsuleColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; });

		py::class_<MeshColliderRef>(m, "MeshCollider", "Static triangle-mesh collider built from the MeshRenderer")
			.def_property("use_mesh_renderer", [](const MeshColliderRef& r) { return r.Get().UseMeshRenderer; }, [](const MeshColliderRef& r, bool v) { r.Get().UseMeshRenderer = v; });

		py::class_<ScriptRef>(m, "ScriptComponent", "Attaches a Python script class to the entity")
			.def_property("path", [](const ScriptRef& r) { return r.Get().ScriptPath; }, [](const ScriptRef& r, const std::string& v) { r.Get().ScriptPath = v; })
			.def_property("class_name", [](const ScriptRef& r) { return r.Get().ClassName; }, [](const ScriptRef& r, const std::string& v) { r.Get().ClassName = v; })
			.def_property_readonly("fields", [](const ScriptRef& r)
			{
				py::dict fields;
				for (const auto& [name, value] : r.Get().Fields)
					fields[py::str(name)] = ScriptFieldToPython(value);
				return fields;
			})
			.def("set_field", [](const ScriptRef& r, const std::string& name, const py::object& value)
			{
				ScriptFieldValue field;
				if (py::isinstance<py::bool_>(value)) field = value.cast<bool>();
				else if (py::isinstance<py::int_>(value)) field = value.cast<int64_t>();
				else if (py::isinstance<py::float_>(value)) field = value.cast<double>();
				else if (py::isinstance<py::str>(value)) field = value.cast<std::string>();
				else if (py::isinstance<glm::vec2>(value)) field = value.cast<glm::vec2>();
				else if (py::isinstance<glm::vec3>(value)) field = value.cast<glm::vec3>();
				else if (py::isinstance<glm::vec4>(value)) field = value.cast<glm::vec4>();
				else throw py::type_error("Script fields must be bool, int, float, str, Vec2, Vec3 or Vec4");
				r.Get().Fields[name] = field;
			}, py::arg("name"), py::arg("value"), "Overrides an exposed field (applies when the script instance is created)")
			.def_property_readonly("instance", [](const ScriptRef& r) -> py::object
			{
				r.Get();
				void* instance = ScriptEngine::GetScriptInstance(r.Owner.ID);
				return instance ? py::reinterpret_borrow<py::object>(static_cast<PyObject*>(instance)) : py::none();
			}, "The running script object (play mode), or None");

		py::class_<AudioSourceRef>(m, "AudioSource")
			.def_property("clip", [](const AudioSourceRef& r) { return r.Get().ClipPath; }, [](const AudioSourceRef& r, const std::string& v) { r.Get().ClipPath = v; })
			.def_property("volume", [](const AudioSourceRef& r) { return r.Get().Volume; },
				[](const AudioSourceRef& r, float v) { r.Get().Volume = std::max(v, 0.0f); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); })
			.def_property("pitch", [](const AudioSourceRef& r) { return r.Get().Pitch; },
				[](const AudioSourceRef& r, float v) { r.Get().Pitch = std::max(v, 0.01f); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); })
			.def_property("loop", [](const AudioSourceRef& r) { return r.Get().Loop; },
				[](const AudioSourceRef& r, bool v) { r.Get().Loop = v; Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); })
			.def_property("play_on_start", [](const AudioSourceRef& r) { return r.Get().PlayOnStart; }, [](const AudioSourceRef& r, bool v) { r.Get().PlayOnStart = v; })
			.def_property("spatial", [](const AudioSourceRef& r) { return r.Get().Spatial; }, [](const AudioSourceRef& r, bool v) { r.Get().Spatial = v; })
			.def_property("min_distance", [](const AudioSourceRef& r) { return r.Get().MinDistance; }, [](const AudioSourceRef& r, float v) { r.Get().MinDistance = v; })
			.def_property("max_distance", [](const AudioSourceRef& r) { return r.Get().MaxDistance; }, [](const AudioSourceRef& r, float v) { r.Get().MaxDistance = v; })
			.def("play", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); return GetAudio(e) ? GetAudio(e)->Play(e) : false; })
			.def("stop", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->Stop(e); })
			.def("pause", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->Pause(e); })
			.def_property_readonly("is_playing", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); return GetAudio(e) ? GetAudio(e)->IsPlaying(e) : false; });

		py::class_<AudioListenerRef>(m, "AudioListener")
			.def_property("active", [](const AudioListenerRef& r) { return r.Get().Active; }, [](const AudioListenerRef& r, bool v) { r.Get().Active = v; });

		// --- Entity -----------------------------------------------------------
		py::class_<PyEntity>(m, "Entity", "A handle to an entity in a scene")
			.def_property_readonly("id", [](const PyEntity& e) { return static_cast<uint64_t>(e.ID); })
			.def_property_readonly("valid", &PyEntity::IsValid, "False once the entity has been destroyed")
			.def_property("name", [](const PyEntity& e) { return e.Resolve().GetName(); }, [](const PyEntity& e, const std::string& v) { e.Resolve().SetName(v); })
			.def_property_readonly("transform", [](const PyEntity& e) { e.Resolve(); return TransformRef { e }; })
			.def_property("position", [](const PyEntity& e) { return GetWorldPosition(e); }, [](const PyEntity& e, const glm::vec3& v) { SetWorldPosition(e, v); }, "World-space position")
			.def_property("local_position", [](const PyEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Translation; },
				[](const PyEntity& e, const glm::vec3& v) { Entity entity = e.Resolve(); entity.GetComponent<TransformComponent>().Translation = v; TeleportIfSimulated(entity); })
			.def_property("rotation", [](const PyEntity& e) { return glm::degrees(e.Resolve().GetComponent<TransformComponent>().GetRotationEuler()); },
				[](const PyEntity& e, const glm::vec3& v) { Entity entity = e.Resolve(); entity.GetComponent<TransformComponent>().SetRotationEuler(glm::radians(v)); TeleportIfSimulated(entity); },
				"Local rotation as Euler angles in degrees")
			.def_property("scale", [](const PyEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Scale; },
				[](const PyEntity& e, const glm::vec3& v) { e.Resolve().GetComponent<TransformComponent>().Scale = v; })
			.def_property_readonly("forward", [](const PyEntity& e) { Entity entity = e.Resolve(); return glm::normalize(-glm::vec3(entity.GetScene()->GetWorldTransform(entity)[2])); })
			.def_property_readonly("right", [](const PyEntity& e) { Entity entity = e.Resolve(); return glm::normalize(glm::vec3(entity.GetScene()->GetWorldTransform(entity)[0])); })
			.def_property_readonly("up", [](const PyEntity& e) { Entity entity = e.Resolve(); return glm::normalize(glm::vec3(entity.GetScene()->GetWorldTransform(entity)[1])); })
			.def_property("parent",
				[](const PyEntity& e) { return EntityOrNone(e.Resolve().GetParent()); },
				[](const PyEntity& e, const py::object& parent)
				{
					Entity entity = e.Resolve();
					Entity newParent = parent.is_none() ? Entity() : parent.cast<PyEntity>().Resolve();
					if (!entity.GetScene()->SetParent(entity, newParent, true))
						throw py::value_error("Cannot create a cycle in the hierarchy");
				})
			.def_property_readonly("children", [](const PyEntity& e)
			{
				std::vector<PyEntity> children;
				for (Entity child : e.Resolve().GetChildren())
					children.emplace_back(child);
				return children;
			})
			.def("find_child", [](const PyEntity& e, const std::string& name) -> py::object
			{
				std::function<Entity(Entity)> search = [&](Entity entity) -> Entity
				{
					for (Entity child : entity.GetChildren())
					{
						if (child.GetName() == name)
							return child;
						if (Entity found = search(child))
							return found;
					}
					return {};
				};
				return EntityOrNone(search(e.Resolve()));
			}, py::arg("name"), "Finds a descendant by name")
			.def("has_component", [](const PyEntity& e, const py::object& component) { return FindComponentInfo(component).Has(e.Resolve()); }, py::arg("component"))
			.def("get_component", [](const PyEntity& e, const py::object& component) -> py::object
			{
				const ComponentInfo& info = FindComponentInfo(component);
				return info.Has(e.Resolve()) ? info.MakeRef(e) : py::none();
			}, py::arg("component"), "Returns the component or None, e.g. entity.get_component('RigidBody')")
			.def("add_component", [](const PyEntity& e, const py::object& component)
			{
				const ComponentInfo& info = FindComponentInfo(component);
				info.Add(e.Resolve());
				return info.MakeRef(e);
			}, py::arg("component"), "Adds the component (if missing) and returns it")
			.def("remove_component", [](const PyEntity& e, const py::object& component)
			{
				const ComponentInfo& info = FindComponentInfo(component);
				if (std::string(info.Name) == "Transform")
					throw py::value_error("The Transform component cannot be removed");
				Entity entity = e.Resolve();
				if (std::string(info.Name) == "RigidBody" && GetPhysics(entity))
					GetPhysics(entity)->DestroyBody(entity);
				info.Remove(entity);
			}, py::arg("component"))
			.def_property_readonly("components", [](const PyEntity& e)
			{
				Entity entity = e.Resolve();
				std::vector<std::string> names;
				for (const ComponentInfo& info : GetComponentInfos())
				{
					if (info.Has(entity))
						names.emplace_back(info.Name);
				}
				return names;
			}, "Names of the components on this entity")
			.def_property_readonly("script", [](const PyEntity& e) -> py::object
			{
				e.Resolve();
				void* instance = ScriptEngine::GetScriptInstance(e.ID);
				return instance ? py::reinterpret_borrow<py::object>(static_cast<PyObject*>(instance)) : py::none();
			}, "The entity's running script object, or None")
			.def("destroy", [](const PyEntity& e) { if (Entity entity = e.TryResolve()) entity.GetScene()->DestroyEntity(entity); }, "Destroys the entity (at the end of the frame during play)")
			.def("look_at", &LookAt, py::arg("target"), py::arg("up") = glm::vec3(0.0f, 1.0f, 0.0f), "Rotates the entity so -Z faces the target")
			.def("translate", [](const PyEntity& e, const glm::vec3& delta) { SetWorldPosition(e, GetWorldPosition(e) + delta); }, py::arg("delta"))
			.def("rotate", [](const PyEntity& e, const glm::vec3& degrees)
			{
				Entity entity = e.Resolve();
				TransformComponent& transform = entity.GetComponent<TransformComponent>();
				transform.SetRotation(transform.GetRotation() * glm::quat(glm::radians(degrees)));
				TeleportIfSimulated(entity);
			}, py::arg("degrees"), "Rotates by Euler angles (degrees) in local space")
			.def("__eq__", [](const PyEntity& a, const py::object& b)
			{
				return py::isinstance<PyEntity>(b) && a.ID == b.cast<PyEntity>().ID && a.SceneID == b.cast<PyEntity>().SceneID;
			})
			.def("__hash__", [](const PyEntity& e) { return std::hash<uint64_t>()(static_cast<uint64_t>(e.ID)); })
			.def("__bool__", &PyEntity::IsValid)
			.def("__repr__", [](const PyEntity& e)
			{
				Entity entity = e.TryResolve();
				return entity ? "Entity('" + entity.GetName() + "', id=" + std::to_string(static_cast<uint64_t>(e.ID)) + ")" : std::string("Entity(<destroyed>)");
			});

		// --- Physics ----------------------------------------------------------
		py::class_<RaycastHit>(m, "RaycastHit")
			.def_property_readonly("entity", [](const RaycastHit& hit) -> py::object
			{
				Scene* scene = GetContextScene();
				return scene ? EntityOrNone(scene->TryGetEntityByUUID(hit.Entity)) : py::none();
			})
			.def_readonly("point", &RaycastHit::Point)
			.def_readonly("normal", &RaycastHit::Normal)
			.def_readonly("distance", &RaycastHit::Distance)
			.def("__repr__", [](const RaycastHit& hit) { return fmt::format("RaycastHit(distance={:.3f})", hit.Distance); });

		m.def("raycast", [](const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> py::object
		{
			Scene& scene = RequireContextScene();
			if (!scene.GetPhysicsWorld())
				return py::none();
			RaycastHit hit;
			if (!scene.GetPhysicsWorld()->Raycast(origin, direction, maxDistance, hit))
				return py::none();
			return py::cast(hit);
		}, py::arg("origin"), py::arg("direction"), py::arg("max_distance") = 1000.0f, "Casts a ray against physics colliders (play mode); returns RaycastHit or None");

		m.def("get_gravity", []() { return RequireContextScene().GetSettings().Gravity; });
		m.def("set_gravity", [](const glm::vec3& gravity) { RequireContextScene().GetSettings().Gravity = gravity; }, py::arg("gravity"));

		// --- Scene ------------------------------------------------------------
		m.def("create_entity", [](const std::string& name, const py::object& parent)
		{
			Scene& scene = RequireContextScene();
			Entity parentEntity = parent.is_none() ? Entity() : parent.cast<PyEntity>().Resolve();
			return PyEntity(scene.CreateChildEntity(parentEntity, name));
		}, py::arg("name") = "Entity", py::arg("parent") = py::none(), "Creates an entity in the active scene");

		m.def("destroy", [](const PyEntity& entity) { if (Entity e = entity.TryResolve()) e.GetScene()->DestroyEntity(e); }, py::arg("entity"));
		m.def("find_entity", [](const std::string& name) { return EntityOrNone(RequireContextScene().FindEntityByName(name)); }, py::arg("name"));
		m.def("get_entity", [](uint64_t id) { return EntityOrNone(RequireContextScene().TryGetEntityByUUID(UUID(id))); }, py::arg("id"));
		m.def("get_entities", [](const py::object& component)
		{
			Scene& scene = RequireContextScene();
			std::vector<PyEntity> result;
			const ComponentInfo* info = component.is_none() ? nullptr : &FindComponentInfo(component);
			for (Entity entity : scene.GetAllEntities())
			{
				if (scene.IsPendingDestroy(entity))
					continue;
				if (!info || info->Has(entity))
					result.emplace_back(entity);
			}
			return result;
		}, py::arg("component") = py::none(), "All entities (optionally only those with a component), in hierarchy order");

		m.def("instantiate", [](const std::string& prefab, const py::object& position, const py::object& parent) -> py::object
		{
			Scene& scene = RequireContextScene();
			Entity parentEntity = parent.is_none() ? Entity() : parent.cast<PyEntity>().Resolve();
			Entity root = SceneSerializer::InstantiatePrefab(scene, prefab, parentEntity);
			if (!root)
				throw py::value_error("Prefab '" + prefab + "' could not be instantiated");
			if (!position.is_none())
				root.GetComponent<TransformComponent>().Translation = position.cast<glm::vec3>();
			return py::cast(PyEntity(root));
		}, py::arg("prefab"), py::arg("position") = py::none(), py::arg("parent") = py::none(), "Spawns a .bprefab and returns its root entity");

		m.def("primary_camera", []() { return EntityOrNone(RequireContextScene().GetPrimaryCameraEntity()); });

		m.def("screen_to_world_ray", [](const glm::vec2& screenPosition) -> py::object
		{
			Scene& scene = RequireContextScene();
			const glm::vec2 viewport = Input::GetViewportSize().x > 0 ? Input::GetViewportSize()
				: glm::vec2(static_cast<float>(scene.GetViewportWidth()), static_cast<float>(scene.GetViewportHeight()));
			RenderCamera camera;
			if (!scene.GetPrimaryCamera(viewport.x / std::max(viewport.y, 1.0f), camera))
				return py::none();
			glm::vec3 origin, direction;
			Math::ScreenPointToRay(screenPosition, viewport, glm::inverse(camera.Projection * camera.View), origin, direction);
			return py::make_tuple(origin, direction);
		}, py::arg("screen_position"), "Returns (origin, direction) of a ray through a viewport pixel, or None without a camera");

		m.def("world_to_screen", [](const glm::vec3& worldPosition) -> py::object
		{
			Scene& scene = RequireContextScene();
			const glm::vec2 viewport = Input::GetViewportSize().x > 0 ? Input::GetViewportSize()
				: glm::vec2(static_cast<float>(scene.GetViewportWidth()), static_cast<float>(scene.GetViewportHeight()));
			RenderCamera camera;
			glm::vec2 result;
			if (!scene.GetPrimaryCamera(viewport.x / std::max(viewport.y, 1.0f), camera)
				|| !Math::WorldToScreenPoint(worldPosition, camera.Projection * camera.View, viewport, result))
				return py::none();
			return py::cast(result);
		}, py::arg("world_position"));

		m.def("scene_name", []() { return RequireContextScene().GetName(); });
		m.def("is_playing", []() { Scene* scene = GetContextScene(); return scene && scene->IsRunning(); });
		m.def("load_scene", [](const std::string& path) { return RequestLoadScene(path); }, py::arg("path"), "Switches to another scene at the end of the frame");
		m.def("quit", [](int exitCode) { RequestQuit(exitCode); }, py::arg("exit_code") = 0, "Quits the game (stops play mode in the editor)");

		// --- Audio ------------------------------------------------------------
		m.def("play_sound", [](const std::string& path, float volume) { return AudioEngine::PlayOneShot(path, volume); },
			py::arg("path"), py::arg("volume") = 1.0f, "Plays a one-shot 2D sound");
		m.def("set_master_volume", &AudioEngine::SetMasterVolume, py::arg("volume"));
		m.def("get_master_volume", &AudioEngine::GetMasterVolume);

		m.attr("VERSION") = BW_VERSION;
	}

}

PYBIND11_EMBEDDED_MODULE(_bellows, module)
{
	Bellows::Scripting::RegisterBindings(module);
}
