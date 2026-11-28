#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/Timestep.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Navigation/NavMesh.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/CameraShake.h"
#include "Bellows/Scene/SceneGroups.h"
#include "Bellows/UI/UILayout.h"
#include "Bellows/UI/UITheme.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <functional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace Bellows {

	class Entity;
	class Mesh;
	struct AnimatorComponent;
	class SceneNavAgents;
	class PhysicsWorld;
	class SceneAudio;
	class SceneTweens;
	struct TextComponent;

	struct SceneSettings
	{
		glm::vec3 Gravity { 0.0f, -9.81f, 0.0f };
		float FixedTimestep = 1.0f / 60.0f;		// Physics step
		glm::vec3 AmbientColor { 0.03f, 0.035f, 0.04f };
		SceneRendererSettings Rendering;
		NavMeshSettings Navigation;				// The agents its navmesh is baked for
	};

	// A collection of entities plus the systems that animate them during play.
	class Scene
	{
	public:
		explicit Scene(std::string name = "Untitled");
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Deep copy (entities keep their UUIDs). Used to enter play mode without touching the edited scene.
		static Ref<Scene> Copy(const Ref<Scene>& source);

		Entity CreateEntity(const std::string& name = "Entity");
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
		Entity CreateChildEntity(Entity parent, const std::string& name = "Entity");

		// In play mode destruction is deferred to the end of the frame; in edit mode it is immediate.
		// Children are destroyed with their parent.
		void DestroyEntity(Entity entity);
		bool IsPendingDestroy(Entity entity) const;

		// Copies an entity and its children with new UUIDs, as a sibling of the original.
		Entity DuplicateEntity(Entity entity);

		Entity FindEntityByName(std::string_view name);
		Entity TryGetEntityByUUID(UUID uuid);
		Entity GetEntityByRenderID(uint32_t renderID);
		std::vector<Entity> GetRootEntities();
		std::vector<Entity> GetAllEntities();
		size_t GetEntityCount() const { return m_EntityMap.size(); }

		// Re-parents 'child'. With keepWorldTransform the entity stays where it is in the world.
		// Returns false if the change would create a cycle.
		bool SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
		void Unparent(Entity child, bool keepWorldTransform = true);
		// Moves a root entity (or a child within its parent) to a new index in its sibling list.
		void SetSiblingIndex(Entity entity, size_t index);

		// Hierarchy groups (Scene/SceneGroups.h), in the order the Hierarchy shows them. Top-level entities
		// are filed under a group; their children follow them. A group never moves anything.
		const std::vector<SceneGroup>& GetGroups() const { return m_Groups; }
		SceneGroup* FindGroup(UUID id);
		// By name, in any letter case; null when there is none.
		SceneGroup* FindGroup(std::string_view name);
		// Adds a group at the end and returns it. Throws std::invalid_argument with the reason when the
		// name can't be used or another group has it, or the scene has MaxGroups already.
		SceneGroup& CreateGroup(std::string_view name);
		// Throws std::invalid_argument as CreateGroup does; renaming a group to its own name in another
		// letter case is allowed.
		void RenameGroup(UUID id, std::string_view name);
		// Removes a group; its entities stay where they are, in no group. False when there is no such group.
		bool DeleteGroup(UUID id);
		// Moves a group to a new place in the list.
		void MoveGroup(UUID id, size_t index);
		// The group of a top-level entity, or of a child's top-level ancestor; 0 for none.
		UUID GetEntityGroup(Entity entity);
		// Files a top-level entity under a group (0 for none). False, with nothing changed, when the entity
		// has a parent (it follows its parent's) or the group doesn't exist.
		bool SetEntityGroup(Entity entity, UUID group);
		// The top-level entities filed under a group, in hierarchy order, leaving out those destroyed this
		// frame; with descendants, each one's children follow it.
		std::vector<Entity> GetGroupEntities(UUID group, bool withDescendants = false);
		// Replaces every group (scene files, undo); entities filed under a group that is gone are in none.
		void SetGroups(std::vector<SceneGroup> groups);
		// Whether the entity's group (its top-level ancestor's) is hidden or locked in the editor.
		bool IsHiddenInEditor(Entity entity);
		bool IsLockedInEditor(Entity entity);

		glm::mat4 GetWorldTransform(Entity entity);
		void SetWorldTransform(Entity entity, const glm::mat4& worldTransform);
		glm::vec3 GetWorldPosition(Entity entity);

		// Play mode
		void OnRuntimeStart();
		void OnRuntimeStop();
		// load_scene while running: loads the scene in json (from path) into this one. Everything that isn't
		// kept (TagComponent::KeepAcrossScenes, with its children) is destroyed; the new scene's settings and
		// entities come in, except trees whose root is already here, so a kept entity also placed in the new
		// scene stays one entity. Kept entities keep their scripts, physics bodies and sounds; time() keeps
		// counting; the game starts unpaused. Then 'scene loaded' goes to every script. False, with nothing
		// changed, when json isn't a scene or the scene isn't running.
		bool ChangeScene(const nlohmann::ordered_json& json, const std::string& path);
		void OnUpdateRuntime(Timestep timestep);
		void OnUpdateEditor(Timestep timestep);
		bool IsRunning() const { return m_IsRunning; }
		// The editor's Pause: nothing runs until it resumes or steps.
		bool IsPaused() const { return m_IsPaused; }
		void SetPaused(bool paused) { m_IsPaused = paused; }
		// The game's own pause (set_paused): physics, time() and scripts stop, except scripts marked Run
		// While Paused, which get real frame times; the UI still answers, so a pause menu works. A scene
		// starts unpaused, including one loaded while paused.
		bool IsGamePaused() const { return m_GamePaused; }
		void SetGamePaused(bool paused) { m_GamePaused = paused; }
		// While paused, advance the given number of frames.
		void Step(uint32_t frames = 1) { m_StepFrames += frames; }

		// The text's world layout (Size and BoxSize as they are), laid out again only when it changed;
		// null when no font could be loaded.
		static const TextLayout* UpdateTextLayout(TextComponent& text);
		// The size the text takes at its own Size, wrapped at BoxSize.x when that is set (meters for
		// world text, reference pixels for screen text). Leaves the cached layout alone.
		static glm::vec2 MeasureTextSize(TextComponent& text);
		// Submits every renderable, light, the environment and the screen UI. Uses 'camera' as the view.
		// 'submitExtras' runs before the frame is finished (editor overlays, debug lines). With
		// 'editorView', groups hidden in the editor leave out their entities' meshes, lights and world text.
		void OnRender(SceneRenderer& renderer, const RenderCamera& camera, const std::function<void(SceneRenderer&)>& submitExtras = {},
			bool editorView = false);

		// Lays out every UI canvas for a screen of 'screenSize' pixels: sets each UIRectComponent's
		// RuntimeRect and the order UI elements are drawn in (OnRender does this every frame).
		void UpdateUILayout(const glm::vec2& screenSize);
		// True for a canvas and everything under it (until another canvas starts), as of the last layout.
		bool IsUIEntity(Entity entity) const;
		// The UI entities that are shown, in drawing order (back to front), as of the last layout.
		std::vector<Entity> GetVisibleUIEntities();
		// True for an entity that is a canvas or has one above it, from the hierarchy alone.
		bool IsInsideUICanvas(Entity entity);
		// Where a shown UI rect was laid out last frame, for editing it on screen: its rect, the area its
		// anchors are in, the canvas scale, and whether a layout placed it. Empty when it isn't shown.
		struct UIRectPlacement
		{
			UIRectangle Rect;
			UIRectangle ParentArea;
			float Scale = 1.0f;
			bool PlacedByLayout = false;
		};
		std::optional<UIRectPlacement> GetUIRectPlacement(Entity entity) const;
		// The front-most shown UI rect under 'pixel' (screen pixels of the last layout), for picking UI in
		// the editor: interactable or not, canvases excluded. Null when there is none.
		Entity FindUIRectAt(const glm::vec2& pixel);

		// The game UI's input for this frame, run by OnUpdateRuntime before scripts: what the pointer is
		// over and pressing, keyboard and gamepad focus and navigation, the events they raise (clicked,
		// focused, unfocused, cancelled, sent up from the UI entity through its parents), and the input
		// the UI takes from the game (Input::SetCapture).
		void UpdateUIInput();
		// The UI entity under the pointer that the pointer can use, the one a press started on (until
		// the button is let go), and the one with keyboard and gamepad focus. Empty when there is none.
		Entity GetUIHovered();
		Entity GetUIPressed();
		Entity GetUIFocused();
		// Gives focus to a UI entity that can take it (focusable, shown, interactable, not behind a modal
		// canvas), or clears focus with an empty entity. False (and nothing changes) when it cannot.
		bool SetUIFocus(Entity entity);
		// The theme an entity's UI is drawn with: its canvas's (the nearest ancestor with a UICanvas), or the default.
		Ref<const UITheme> GetUIThemeFor(Entity entity) const;
		// Whether the UI is using the pointer (over a panel, or held after a press on one) or the
		// keyboard (a modal canvas is shown) this frame, so the game should leave them alone.
		bool UIWantsPointer() const { return m_UICapture.Pointer; }
		bool UIWantsKeyboard() const { return m_UICapture.Keyboard; }
		// Finds the primary camera entity and builds a RenderCamera for the given aspect ratio.
		// The primary camera's view. 'withShake' adds a running camera shake: for the rendered game view
		// only, so scripts' screen_to_world_ray and world_to_screen stay steady.
		bool GetPrimaryCamera(float aspectRatio, RenderCamera& outCamera, bool withShake = false);
		Entity GetPrimaryCameraEntity();

		void OnViewportResize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }
		SceneSettings& GetSettings() { return m_Settings; }
		const SceneSettings& GetSettings() const { return m_Settings; }

		PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
		SceneAudio* GetAudio() const { return m_Audio.get(); }
		// The running tweens; null when the scene isn't playing.
		SceneTweens* GetTweens() const { return m_Tweens.get(); }
		// The game camera's shake (shake_camera); cleared when play starts and stops.
		CameraShake& GetCameraShake() { return m_CameraShake; }
		const CameraShake& GetCameraShake() const { return m_CameraShake; }

		// Readies an Animator for 'mesh' (a new mesh, or the game's start): its clip starts when Play On
		// Start is on (always in the editor's preview), otherwise it holds the rest pose. The scene's update
		// calls it, and so do scripts that reach an Animator before that, so both start it the same way.
		void StartAnimator(Entity entity, AnimatorComponent& animator, const Ref<Mesh>& mesh, bool warnUnknownClip = true);
		// What agents walk on, or null before a bake. Loaded with the scene from its .bnav file, shared
		// with copies (Play), and replaced by BakeNavigation.
		const Ref<NavMesh>& GetNavMesh() const { return m_NavMesh; }
		void SetNavMesh(Ref<NavMesh> navMesh) { m_NavMesh = std::move(navMesh); }
		// Bakes a navmesh from the static colliders with the scene's Navigation settings. False, with the
		// reason in 'error' and the old navmesh kept, when nothing could be baked.
		bool BakeNavigation(std::string& error);
		// The NavAgents being moved during play; null while the game isn't running.
		SceneNavAgents* GetNavAgents() const { return m_NavAgents.get(); }
		float GetTime() const { return m_Time; }
		uint64_t GetFrameCount() const { return m_FrameCount; }

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

		entt::registry& GetRegistry() { return m_Registry; }

		// Unique for the lifetime of the process; lets external handles (Python) detect deleted scenes.
		uint64_t GetID() const { return m_SceneID; }
		static Scene* GetSceneByID(uint64_t id);

		// The scene currently running scripts (play mode). Null when nothing is playing.
		static Scene* GetActiveScene() { return s_ActiveScene; }
	private:
		void DestroyEntityImmediate(Entity entity);
		void FlushPendingDestroys();
		void RemoveFromParentList(Entity entity);
		void UpdateRuntimeCaches();
		// Moves every Animator on by 'seconds' and poses its mesh. During play they all run; otherwise only
		// those the editor previews do, and the rest go back to their rest pose.
		void UpdateAnimators(float seconds);
		// Moves every particle emitter on by 'seconds' (in the editor too, so effects show while editing).
		void UpdateParticles(float seconds);
		static const TextLayout* LayoutTextComponent(TextComponent& text, float size, const glm::vec2& boxSize);
		void SubmitUI(SceneRenderer& renderer);

		// A visible UI entity and where it is drawn, in drawing order.
		struct UIElement
		{
			entt::entity Handle = entt::null;
			entt::entity Canvas = entt::null;	// The canvas it belongs to
			UIRectangle Rect;
			UIRectangle Clip;					// Where it may draw and be pointed at (scroll areas cut it)
			float Scale = 1.0f;					// Screen pixels per reference pixel
			bool Interactable = true;			// Itself and everything above it up to the canvas
			size_t SubtreeEnd = 0;				// The index after its last shown descendant
			UIRectangle ParentArea;				// The rect its anchors are in (its parent's, scrolled)
			bool PlacedByLayout = false;		// Its parent's UILayout placed it, not its anchors and offsets
		};
		struct UIEvent
		{
			std::string Name;
			UUID Source = 0;
			std::variant<std::monostate, bool, double, std::string> Value;	// Sent before the entity when set ('changed', 'submitted')
		};
		enum class UIPointerOwner : uint8_t { None = 0, UI, Game };

		const UIElement* FindUIElement(UUID id) const;
		bool CanTakeUIFocus(UUID id) const;
		void ChangeUIFocus(UUID id, std::vector<UIEvent>& events);
		void ScrollUIIntoView(UUID id);
		bool IsUIWidget(entt::entity handle) const;
		// What a click or ui_accept does to a widget beyond 'clicked': flips a toggle, opens a dropdown.
		void ActivateUIWidget(UUID id, std::vector<UIEvent>& events);
		void SetUISliderValue(UUID id, float value, std::vector<UIEvent>& events);
		void SelectUIDropdownOption(UUID id, int index, std::vector<UIEvent>& events);
		// Runs the open dropdown list, which takes every input while it is open.
		void UpdateUIDropdownList(const glm::vec2& pointer, const glm::vec2& screen, std::vector<UIEvent>& events);
		void SubmitUIDropdownList(SceneRenderer& renderer);
		// The theme of the element's canvas, its style class (from its UIRect), and the state it shows.
		const UITheme& GetUITheme(const UIElement& element) const;
		const std::string& GetUIStyleClass(const UIElement& element) const;
		UIWidgetState GetUIWidgetState(const UIElement& element) const;
		// A label's color: its own, dimmed when its widget is off, or its theme's for its widget's state.
		glm::vec4 GetUITextColor(const UIElement& element, const TextComponent& text) const;
		// Text fields: typing and editing keys for the focused one, the caret placed (or a selection
		// extended) by the pointer, the caret kept in view, and drawing.
		void UpdateUITextInput(const UIElement& element, std::vector<UIEvent>& events);
		void PlaceUITextCaret(const UIElement& element, const glm::vec2& point, bool extend);
		void KeepUITextCaretVisible(const UIElement& element);
		void SubmitUITextInput(SceneRenderer& renderer, const UIElement& element, const glm::vec4& clip);
		void EmitUIEvents(const std::vector<UIEvent>& events);
		void ResetUIInput();
	private:
		uint64_t m_SceneID = 0;
		std::string m_Name;
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<UUID> m_RootEntities;
		std::vector<UUID> m_PendingDestroy;
		std::vector<SceneGroup> m_Groups;
		SceneSettings m_Settings;

		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;

		std::vector<UIElement> m_UIElements;
		std::unordered_map<entt::entity, size_t> m_UIElementIndex;
		std::unordered_set<entt::entity> m_UIEntities;
		std::unordered_map<entt::entity, Ref<const UITheme>> m_UICanvasThemes;	// Each shown canvas's theme
		entt::entity m_UIModalCanvas = entt::null;		// The top shown modal canvas
		UUID m_UIHovered = 0;
		UUID m_UIPressed = 0;
		UUID m_UIFocused = 0;
		UIPointerOwner m_UIPointerOwner = UIPointerOwner::None;	// Who a held pointer press belongs to
		UUID m_UIOpenDropdown = 0;								// The dropdown whose list is open
		int m_UIDropdownHighlight = -1;							// The option row the keys or pointer are on
		std::chrono::steady_clock::time_point m_UICaretMoved;	// The caret shows steadily for a moment after it moves
		InputCapture m_UICapture;

		bool m_IsRunning = false;
		bool m_IsPaused = false;
		bool m_GamePaused = false;
		uint32_t m_StepFrames = 0;
		float m_Time = 0.0f;
		uint64_t m_FrameCount = 0;
		float m_PhysicsAccumulator = 0.0f;

		Scope<PhysicsWorld> m_PhysicsWorld;
		Scope<SceneAudio> m_Audio;
		Scope<SceneTweens> m_Tweens;
		CameraShake m_CameraShake;
		Ref<NavMesh> m_NavMesh;
		Scope<SceneNavAgents> m_NavAgents;

		static Scene* s_ActiveScene;

		friend class Entity;
		friend class SceneSerializer;
	};

}
