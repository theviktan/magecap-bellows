#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Bellows;

namespace {

	bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
	}

	// Gives an entity every component with non-default values.
	void AddEveryComponent(Entity entity)
	{
		TransformComponent& transform = entity.GetComponent<TransformComponent>();
		transform.Translation = { 1.0f, 2.0f, 3.0f };
		transform.SetRotationEuler(glm::radians(glm::vec3(10.0f, 20.0f, 30.0f)));
		transform.Scale = { 2.0f, 2.0f, 2.0f };

		auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
		meshRenderer.MeshPath = "Builtin/Sphere";
		meshRenderer.MaterialPath = "Materials/Gold.bmat";
		meshRenderer.CastShadows = false;

		auto& camera = entity.AddComponent<CameraComponent>();
		camera.Projection = ProjectionType::Orthographic;
		camera.OrthographicSize = 12.0f;
		camera.Primary = false;

		entity.AddComponent<DirectionalLightComponent>().Intensity = 5.0f;
		entity.AddComponent<PointLightComponent>().Range = 7.0f;
		entity.AddComponent<SpotLightComponent>().OuterAngle = glm::radians(40.0f);
		auto& sky = entity.AddComponent<SkyLightComponent>();
		sky.EnvironmentPath = "Environments/Studio.hdr";
		sky.Rotation = glm::radians(90.0f);

		auto& rigidBody = entity.AddComponent<RigidBodyComponent>();
		rigidBody.Type = RigidBodyType::Kinematic;
		rigidBody.Mass = 3.0f;
		rigidBody.LockRotationY = true;
		entity.AddComponent<BoxColliderComponent>().HalfExtents = { 1.0f, 2.0f, 3.0f };
		entity.AddComponent<SphereColliderComponent>().Radius = 0.75f;
		entity.AddComponent<CapsuleColliderComponent>().HalfHeight = 1.25f;
		entity.AddComponent<MeshColliderComponent>();

		auto& script = entity.AddComponent<ScriptComponent>();
		script.ScriptPath = "Scripts/Player.py";
		script.ClassName = "Player";
		script.Fields["speed"] = 4.5;
		script.Fields["lives"] = int64_t(3);
		script.Fields["god_mode"] = true;
		script.Fields["title"] = std::string("Hero");
		script.Fields["spawn"] = glm::vec3(1.0f, 2.0f, 3.0f);

		auto& audio = entity.AddComponent<AudioSourceComponent>();
		audio.ClipPath = "Audio/Theme.ogg";
		audio.Loop = true;
		audio.Bus = "Music";
		entity.AddComponent<AudioListenerComponent>();

		auto& text = entity.AddComponent<TextComponent>();
		text.Text = "Hello, w\xC3\xB6rld\nSecond line";
		text.FontPath = "Fonts/Title.ttf";
		text.Size = 0.75f;
		text.Color = glm::vec4(1.0f, 0.5f, 0.25f, 0.8f);
		text.Alignment = TextAlignment::Right;
		text.VerticalAlignment = TextVerticalAlignment::Bottom;
		text.BoxSize = glm::vec2(4.0f, 1.5f);
		text.Wrap = false;
		text.Overflow = TextOverflow::Ellipsis;
		text.LineSpacing = 1.25f;
		text.OutlineWidth = 0.05f;
		text.OutlineColor = glm::vec4(0.0f, 0.0f, 0.2f, 1.0f);
		text.FaceCamera = true;
		text.Visible = false;

		auto& canvas = entity.AddComponent<UICanvasComponent>();
		canvas.ReferenceSize = glm::vec2(1280.0f, 720.0f);
		canvas.ScaleMode = UIScaleMode::FitWidth;
		canvas.SortOrder = -3;
		canvas.Visible = false;
		canvas.Modal = true;
		canvas.Theme = "Themes/Night.btheme";
		auto& rect = entity.AddComponent<UIRectComponent>();
		rect.AnchorMin = glm::vec2(0.0f, 1.0f);
		rect.AnchorMax = glm::vec2(1.0f, 1.0f);
		rect.Pivot = glm::vec2(0.25f, 1.0f);
		rect.OffsetMin = glm::vec2(12.0f, -80.0f);
		rect.OffsetMax = glm::vec2(-12.0f, -8.0f);
		rect.Visible = false;
		rect.Interactable = false;
		rect.Focusable = true;
		rect.StyleClass = "danger";
		rect.WidthSizing = UISizing::Fill;
		auto& image = entity.AddComponent<UIImageComponent>();
		image.TexturePath = "Textures/Panel.png";
		image.Color = glm::vec4(0.1f, 0.2f, 0.3f, 0.9f);
		image.CornerRadius = 14.0f;
		image.BorderWidth = 3.0f;
		image.BorderColor = glm::vec4(0.96f, 0.53f, 0.23f, 1.0f);
		image.Visible = false;
		image.SliceBorder = glm::vec4(8.0f, 6.0f, 8.0f, 10.0f);
		auto& layout = entity.AddComponent<UILayoutComponent>();
		layout.Direction = UILayoutDirection::Grid;
		layout.Spacing = 12.0f;
		layout.Padding = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
		layout.MainAlignment = UIAlignment::End;
		layout.CrossAlignment = UIAlignment::Center;
		layout.Columns = 4;
		auto& scroll = entity.AddComponent<UIScrollComponent>();
		scroll.Horizontal = true;
		scroll.Vertical = false;
		scroll.ScrollPosition = glm::vec2(30.0f, 0.0f);
		scroll.WheelSpeed = 90.0f;
		scroll.ShowScrollbars = false;
		auto& button = entity.AddComponent<UIButtonComponent>();
		button.HoverColor = glm::vec4(0.5f, 0.6f, 0.7f, 1.0f);
		button.DisabledColor = glm::vec4(0.1f, 0.1f, 0.1f, 0.3f);
		entity.AddComponent<UIToggleComponent>().On = true;
		auto& slider = entity.AddComponent<UISliderComponent>();
		slider.Min = -5.0f;
		slider.Max = 15.0f;
		slider.Step = 0.5f;
		slider.Value = 7.5f;
		auto& dropdown = entity.AddComponent<UIDropdownComponent>();
		dropdown.Options = { "Low", "Medium", "High \xE2\x98\x85" };
		dropdown.Selected = 2;
		auto& textInput = entity.AddComponent<UITextInputComponent>();
		textInput.Placeholder = "Your name";
		textInput.MultiLine = true;
		textInput.Password = true;
		textInput.MaxLength = 24;
		entity.AddComponent<PrefabComponent>().PrefabPath = "Prefabs/Hero.bprefab";
	}

}

TEST_CASE("Entities are created, found and destroyed")
{
	Ref<Scene> scene = CreateRef<Scene>("Test");
	Entity a = scene->CreateEntity("A");
	Entity b = scene->CreateEntity("B");
	CHECK(scene->GetEntityCount() == 2);
	CHECK(scene->FindEntityByName("B") == b);
	CHECK(scene->TryGetEntityByUUID(a.GetUUID()) == a);
	CHECK(scene->GetEntityByRenderID(a.GetRenderID()) == a);
	CHECK(a.HasComponent<TransformComponent>());
	CHECK(a.HasComponent<RelationshipComponent>());

	const UUID uuid = a.GetUUID();
	scene->DestroyEntity(a);
	CHECK_FALSE(a.IsValid());
	CHECK_FALSE(scene->TryGetEntityByUUID(uuid));
	CHECK(scene->GetEntityCount() == 1);
	CHECK_FALSE(scene->FindEntityByName("A"));
}

TEST_CASE("Hierarchy keeps world transforms, order and prevents cycles")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity parent = scene->CreateEntity("Parent");
	Entity child = scene->CreateEntity("Child");
	parent.GetTransform().Translation = { 10.0f, 0.0f, 0.0f };
	parent.GetTransform().Scale = { 2.0f, 2.0f, 2.0f };
	child.GetTransform().Translation = { 1.0f, 1.0f, 1.0f };

	REQUIRE(scene->SetParent(child, parent, true));
	CHECK(child.GetParent() == parent);
	CHECK(Near(scene->GetWorldPosition(child), { 1.0f, 1.0f, 1.0f }));
	CHECK(Near(child.GetTransform().Translation, { -4.5f, 0.5f, 0.5f }));
	CHECK(scene->GetRootEntities().size() == 1);

	CHECK_FALSE(scene->SetParent(parent, child, true));
	CHECK(parent.GetParent() == Entity());

	Entity second = scene->CreateChildEntity(parent, "Second");
	CHECK(parent.GetChildren().size() == 2);
	scene->SetSiblingIndex(second, 0);
	CHECK(parent.GetChildren()[0] == second);
	CHECK(second.IsDescendantOf(parent));

	scene->Unparent(child, true);
	CHECK(Near(scene->GetWorldPosition(child), { 1.0f, 1.0f, 1.0f }));
	CHECK(scene->GetRootEntities().size() == 2);

	// Destroying a parent destroys its children.
	scene->DestroyEntity(parent);
	CHECK_FALSE(second.IsValid());
	CHECK(scene->GetEntityCount() == 1);
}

TEST_CASE("DuplicateEntity deep-copies with new IDs next to the original")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity first = scene->CreateEntity("First");
	Entity root = scene->CreateEntity("Root");
	Entity last = scene->CreateEntity("Last");
	Entity child = scene->CreateChildEntity(root, "Child");
	child.AddComponent<PointLightComponent>().Intensity = 42.0f;

	Entity copy = scene->DuplicateEntity(root);
	REQUIRE(copy);
	CHECK(copy.GetUUID() != root.GetUUID());
	CHECK(copy.GetName() == "Root");
	REQUIRE(copy.GetChildren().size() == 1);
	Entity childCopy = copy.GetChildren()[0];
	CHECK(childCopy.GetUUID() != child.GetUUID());
	CHECK(childCopy.GetComponent<PointLightComponent>().Intensity == 42.0f);

	const std::vector<Entity> roots = scene->GetRootEntities();
	REQUIRE(roots.size() == 4);
	CHECK(roots[0] == first);
	CHECK(roots[1] == root);
	CHECK(roots[2] == copy);
	CHECK(roots[3] == last);
}

TEST_CASE("Scene::Copy keeps IDs, components and hierarchy")
{
	Ref<Scene> source = CreateRef<Scene>("Source");
	Entity parent = source->CreateEntity("Parent");
	Entity child = source->CreateChildEntity(parent, "Child");
	AddEveryComponent(child);
	source->GetSettings().Gravity = { 0.0f, -3.0f, 0.0f };

	Ref<Scene> copy = Scene::Copy(source);
	CHECK(copy->GetEntityCount() == 2);
	Entity copiedChild = copy->TryGetEntityByUUID(child.GetUUID());
	REQUIRE(copiedChild);
	CHECK(copiedChild.GetParent().GetUUID() == parent.GetUUID());
	CHECK(copiedChild.GetComponent<RigidBodyComponent>().Mass == 3.0f);
	CHECK(copy->GetSettings().Gravity.y == -3.0f);

	// The copy is independent.
	copiedChild.GetComponent<RigidBodyComponent>().Mass = 9.0f;
	CHECK(child.GetComponent<RigidBodyComponent>().Mass == 3.0f);
}

TEST_CASE("Scene serialization round-trips every component")
{
	Ref<Scene> scene = CreateRef<Scene>("RoundTrip");
	scene->GetSettings().Gravity = { 0.0f, -20.0f, 0.0f };
	scene->GetSettings().Rendering.Exposure = 1.7f;
	scene->GetSettings().Rendering.ToneMapper = Tonemapper::AgX;
	Entity root = scene->CreateEntity("Root");
	Entity child = scene->CreateChildEntity(root, "Child");
	scene->CreateChildEntity(root, "Second Child");
	scene->CreateEntity("Other Root");
	AddEveryComponent(child);

	const std::filesystem::path path = BellowsTests::GetTestOutputDirectory() / "RoundTrip.bscene";
	REQUIRE(SceneSerializer::Serialize(*scene, path));

	Ref<Scene> loaded = CreateRef<Scene>();
	REQUIRE(SceneSerializer::Deserialize(*loaded, path));

	CHECK(loaded->GetName() == "RoundTrip");
	CHECK(loaded->GetEntityCount() == 4);
	CHECK(loaded->GetSettings().Gravity.y == -20.0f);
	CHECK(loaded->GetSettings().Rendering.ToneMapper == Tonemapper::AgX);
	CHECK(loaded->GetSettings().Rendering.Exposure == doctest::Approx(1.7f));

	// Serializing the loaded scene reproduces the original document exactly.
	CHECK(SceneSerializer::SerializeToJson(*loaded).dump() == SceneSerializer::SerializeToJson(*scene).dump());

	Entity loadedChild = loaded->TryGetEntityByUUID(child.GetUUID());
	REQUIRE(loadedChild);
	CHECK(loadedChild.GetParent().GetName() == "Root");
	CHECK(loadedChild.GetParent().GetChildren()[0] == loadedChild);
	const ScriptComponent& script = loadedChild.GetComponent<ScriptComponent>();
	CHECK(std::get<double>(script.Fields.at("speed")) == doctest::Approx(4.5));
	CHECK(std::get<int64_t>(script.Fields.at("lives")) == 3);
	CHECK(std::get<bool>(script.Fields.at("god_mode")));
	CHECK(std::get<std::string>(script.Fields.at("title")) == "Hero");
	CHECK(std::get<glm::vec3>(script.Fields.at("spawn")) == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(Near(glm::degrees(loadedChild.GetTransform().GetRotationEuler()), { 10.0f, 20.0f, 30.0f }, 1e-3f));
	CHECK(loadedChild.GetComponent<AudioSourceComponent>().Bus == "Music");
	const TextComponent& text = loadedChild.GetComponent<TextComponent>();
	CHECK(text.Text == "Hello, w\xC3\xB6rld\nSecond line");
	CHECK(text.FontPath == "Fonts/Title.ttf");
	CHECK(text.Size == doctest::Approx(0.75f));
	CHECK(text.Color == glm::vec4(1.0f, 0.5f, 0.25f, 0.8f));
	CHECK(text.Alignment == TextAlignment::Right);
	CHECK(text.VerticalAlignment == TextVerticalAlignment::Bottom);
	CHECK(text.BoxSize == glm::vec2(4.0f, 1.5f));
	CHECK_FALSE(text.Wrap);
	CHECK(text.Overflow == TextOverflow::Ellipsis);
	CHECK(text.LineSpacing == doctest::Approx(1.25f));
	CHECK(text.OutlineWidth == doctest::Approx(0.05f));
	CHECK(text.OutlineColor == glm::vec4(0.0f, 0.0f, 0.2f, 1.0f));
	CHECK(text.FaceCamera);
	CHECK_FALSE(text.Visible);

	const UICanvasComponent& canvas = loadedChild.GetComponent<UICanvasComponent>();
	CHECK(canvas.ReferenceSize == glm::vec2(1280.0f, 720.0f));
	CHECK(canvas.ScaleMode == UIScaleMode::FitWidth);
	CHECK(canvas.SortOrder == -3);
	CHECK_FALSE(canvas.Visible);
	CHECK(canvas.Modal);
	CHECK(canvas.Theme == "Themes/Night.btheme");
	const UIRectComponent& rect = loadedChild.GetComponent<UIRectComponent>();
	CHECK(rect.AnchorMin == glm::vec2(0.0f, 1.0f));
	CHECK(rect.AnchorMax == glm::vec2(1.0f, 1.0f));
	CHECK(rect.Pivot == glm::vec2(0.25f, 1.0f));
	CHECK(rect.OffsetMin == glm::vec2(12.0f, -80.0f));
	CHECK(rect.OffsetMax == glm::vec2(-12.0f, -8.0f));
	CHECK_FALSE(rect.Visible);
	CHECK_FALSE(rect.Interactable);
	CHECK(rect.Focusable);
	CHECK(rect.StyleClass == "danger");
	CHECK(rect.WidthSizing == UISizing::Fill);
	CHECK(rect.HeightSizing == UISizing::Fixed);
	const UIImageComponent& image = loadedChild.GetComponent<UIImageComponent>();
	CHECK(image.TexturePath == "Textures/Panel.png");
	CHECK(image.Color == glm::vec4(0.1f, 0.2f, 0.3f, 0.9f));
	CHECK(image.CornerRadius == doctest::Approx(14.0f));
	CHECK(image.BorderWidth == doctest::Approx(3.0f));
	CHECK(image.BorderColor == glm::vec4(0.96f, 0.53f, 0.23f, 1.0f));
	CHECK_FALSE(image.Visible);
	CHECK(image.SliceBorder == glm::vec4(8.0f, 6.0f, 8.0f, 10.0f));
	const UILayoutComponent& layout = loadedChild.GetComponent<UILayoutComponent>();
	CHECK(layout.Direction == UILayoutDirection::Grid);
	CHECK(layout.Spacing == doctest::Approx(12.0f));
	CHECK(layout.Padding == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
	CHECK(layout.MainAlignment == UIAlignment::End);
	CHECK(layout.CrossAlignment == UIAlignment::Center);
	CHECK(layout.Columns == 4);
	const UIScrollComponent& scroll = loadedChild.GetComponent<UIScrollComponent>();
	CHECK(scroll.Horizontal);
	CHECK_FALSE(scroll.Vertical);
	CHECK(scroll.ScrollPosition == glm::vec2(30.0f, 0.0f));
	CHECK(scroll.WheelSpeed == doctest::Approx(90.0f));
	CHECK_FALSE(scroll.ShowScrollbars);
	const UIButtonComponent& button = loadedChild.GetComponent<UIButtonComponent>();
	CHECK(button.HoverColor == glm::vec4(0.5f, 0.6f, 0.7f, 1.0f));
	CHECK(button.DisabledColor == glm::vec4(0.1f, 0.1f, 0.1f, 0.3f));
	CHECK(button.NormalColor == UIButtonComponent().NormalColor);
	CHECK(loadedChild.GetComponent<UIToggleComponent>().On);
	const UISliderComponent& slider = loadedChild.GetComponent<UISliderComponent>();
	CHECK(slider.Min == doctest::Approx(-5.0f));
	CHECK(slider.Max == doctest::Approx(15.0f));
	CHECK(slider.Step == doctest::Approx(0.5f));
	CHECK(slider.Value == doctest::Approx(7.5f));
	const UIDropdownComponent& dropdown = loadedChild.GetComponent<UIDropdownComponent>();
	CHECK(dropdown.Options == std::vector<std::string> { "Low", "Medium", "High \xE2\x98\x85" });
	CHECK(dropdown.Selected == 2);
	const UITextInputComponent& textInput = loadedChild.GetComponent<UITextInputComponent>();
	CHECK(textInput.Placeholder == "Your name");
	CHECK(textInput.MultiLine);
	CHECK(textInput.Password);
	CHECK(textInput.MaxLength == 24);
}

TEST_CASE("Deserializing rejects malformed scene files")
{
	const std::filesystem::path path = BellowsTests::GetTestOutputDirectory() / "Broken.bscene";
	FileSystem::WriteTextFile(path, "{ this is not json");
	Ref<Scene> scene = CreateRef<Scene>();
	CHECK_FALSE(SceneSerializer::Deserialize(*scene, path));
	CHECK_FALSE(SceneSerializer::Deserialize(*scene, BellowsTests::GetTestOutputDirectory() / "Missing.bscene"));
	CHECK(scene->GetEntityCount() == 0);
}

TEST_CASE("Prefabs instantiate with fresh IDs and keep their hierarchy")
{
	const std::filesystem::path projectAssets = BellowsTests::GetTestOutputDirectory() / "PrefabProject";
	AssetManager::SetAssetDirectory(projectAssets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity tower = scene->CreateEntity("Tower");
	Entity top = scene->CreateChildEntity(tower, "Top");
	top.GetTransform().Translation = { 0.0f, 5.0f, 0.0f };
	top.AddComponent<PointLightComponent>();
	REQUIRE(SceneSerializer::SavePrefab(tower, projectAssets / "Prefabs" / "Tower.bprefab"));

	Entity first = SceneSerializer::InstantiatePrefab(*scene, "Prefabs/Tower.bprefab");
	Entity second = SceneSerializer::InstantiatePrefab(*scene, "Prefabs/Tower.bprefab", tower);
	REQUIRE(first);
	REQUIRE(second);
	CHECK(first.GetUUID() != tower.GetUUID());
	CHECK(first.GetUUID() != second.GetUUID());
	CHECK(first.GetComponent<PrefabComponent>().PrefabPath == "Prefabs/Tower.bprefab");
	REQUIRE(first.GetChildren().size() == 1);
	CHECK(first.GetChildren()[0].GetTransform().Translation.y == 5.0f);
	CHECK(first.GetChildren()[0].HasComponent<PointLightComponent>());
	CHECK(second.GetParent() == tower);
	CHECK(scene->GetEntityCount() == 6);

	CHECK_FALSE(SceneSerializer::InstantiatePrefab(*scene, "Prefabs/Missing.bprefab"));
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Destruction during play is deferred to the end of the frame")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity entity = scene->CreateEntity("Doomed");
	Entity child = scene->CreateChildEntity(entity, "Child");
	scene->OnRuntimeStart();

	scene->DestroyEntity(entity);
	CHECK(entity.IsValid());
	CHECK(scene->IsPendingDestroy(entity));
	CHECK(scene->IsPendingDestroy(child));
	CHECK_FALSE(scene->FindEntityByName("Doomed"));

	scene->OnUpdateRuntime(1.0f / 60.0f);
	CHECK_FALSE(entity.IsValid());
	CHECK_FALSE(child.IsValid());
	scene->OnRuntimeStop();
}
