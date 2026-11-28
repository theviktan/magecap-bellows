#pragma once

#include "Bellows/Animation/AnimationPlayer.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Scene/ScriptField.h"
#include "Bellows/Text/Font.h"
#include "Bellows/Text/TextLayout.h"
#include "Bellows/UI/UILayout.h"
#include "Bellows/UI/UIStyle.h"
#include "Bellows/UI/UITextEdit.h"
#include "Bellows/UI/UITheme.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <map>
#include <optional>
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
		std::string Tag;				// The entity's name
		bool KeepAcrossScenes = false;	// The entity and its children stay when load_scene changes the scene
		std::vector<std::string> Tags;	// Labels it has ("enemy"); see Scene/EntityTags.h for the rules
		// The Hierarchy group a top-level entity is filed under (Scene/SceneGroups.h), 0 for none. A
		// child's is always 0: it follows its top-level ancestor (Scene::GetEntityGroup).
		UUID Group = 0;
		bool Pinned = false;			// Editor-only: listed at the top of the Hierarchy
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

	// Plays the animation clips of the entity's skinned MeshRenderer mesh.
	struct AnimatorComponent
	{
		std::string Clip;			// Played when the game starts; empty = the mesh's first clip
		bool PlayOnStart = true;
		bool Loop = true;
		float Speed = 1.0f;			// Times as fast; -10 to 10, negative plays backward

		// Runtime state (not serialized)
		AnimationPlayer RuntimePlayer;
		Ref<Mesh> RuntimeMesh;						// The mesh RuntimePlayer's clips come from
		std::vector<glm::mat4> RuntimeSkinMatrices;	// The pose drawn; empty = the rest pose
		bool RuntimePreview = false;				// The editor plays it while the game isn't running
	};

	// Walks the entity over the scene's navmesh to a destination, steering around walls and other
	// agents. It moves the entity's transform, so it suits an entity with no rigid body or a kinematic
	// one; physics would fight it on a dynamic body, so an agent there stands still (with a warning).
	struct NavAgentComponent
	{
		float Speed = 3.5f;				// Top speed, m/s
		float Acceleration = 8.0f;		// m/s²
		float Radius = 0.4f;			// Meters it keeps from other agents
		float Height = 1.8f;			// Meters
		float BaseOffset = 0.0f;		// The entity's origin above the ground, meters (half a capsule's height, say)
		float StoppingDistance = 0.1f;	// It arrives this close to its destination, meters
		float TurnSpeed = 720.0f;		// Degrees per second it turns to face where it walks; 0 = it doesn't turn
		bool AvoidOthers = true;		// Steers around other agents

		static constexpr float MaxSpeed = 100.0f;
		static constexpr float MinAcceleration = 0.1f, MaxAcceleration = 1000.0f;
		static constexpr float MinRadius = 0.05f, MaxRadius = 5.0f;
		static constexpr float MinHeight = 0.1f, MaxHeight = 20.0f;
		static constexpr float MaxBaseOffset = 20.0f;	// Either way
		static constexpr float MinStoppingDistance = 0.01f, MaxStoppingDistance = 100.0f;
		static constexpr float MaxTurnSpeed = 10000.0f;

		// Holds every value to its range (and a NaN to its default), as a loaded or edited agent needs.
		void Clamp();

		// Runtime state (not serialized)
		std::optional<glm::vec3> RuntimeDestination;	// Where it's walking; none = it stands
		bool RuntimeDestinationChanged = false;			// Not yet handed to the navigation update
		glm::vec3 RuntimeVelocity { 0.0f };				// m/s, as of the last navigation update
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
		bool RunWhilePaused = false;	// Keeps updating while the game is paused (pause menus, their animations)
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

	// Text drawn with a font's distance-field atlas, crisp at any size. In the world, the text's box is
	// centered on the entity, lies in its XY plane (readable from +Z) and follows its transform, or
	// turns to face the camera. Inside a UI canvas it is screen text: Size is in reference pixels and the
	// box is the entity's UI rectangle (BoxSize and FaceCamera are not used).
	struct TextComponent
	{
		// A good starting size for screen text, in reference pixels (the editor uses it for new labels).
		static constexpr float DefaultScreenSize = 32.0f;

		std::string Text = "Text";
		std::string FontPath;			// A .ttf or .otf in the assets; empty = the built-in Inter
		float Size = 0.5f;				// One em, in meters (the height of a line is about 1.2 em)
		glm::vec4 Color { 1.0f };		// sRGB, with alpha
		TextAlignment Alignment = TextAlignment::Center;
		TextVerticalAlignment VerticalAlignment = TextVerticalAlignment::Middle;
		glm::vec2 BoxSize { 0.0f };		// Meters; 0 = sized to the text (no wrapping in that direction)
		bool Wrap = true;
		TextOverflow Overflow = TextOverflow::Visible;
		float LineSpacing = 1.0f;		// A multiple of the font's line height
		float OutlineWidth = 0.0f;		// Ems, up to 0.1
		glm::vec4 OutlineColor { 0.0f, 0.0f, 0.0f, 1.0f };
		bool FaceCamera = false;		// Always turned toward the camera (a label over a character's head)
		// Screen text only: take the color from the canvas's theme (its widget's state, or the theme's text
		// color) instead of Color. The editor's UI templates turn it on.
		bool ThemeColor = false;
		bool Visible = true;

		// Runtime cache (not serialized)
		Ref<Font> RuntimeFont;
		std::string LoadedFontPath;
		TextLayout RuntimeLayout;
		uint64_t RuntimeLayoutKey = 0;
	};

	enum class ParticleShape : uint8_t
	{
		Point = 0,	// Every particle starts at the entity
		Sphere,		// Anywhere inside a sphere of radius ShapeSize.x
		Box,		// Anywhere inside a box of half extents ShapeSize
	};

	enum class ParticleBlend : uint8_t
	{
		Additive = 0,	// Adds light: fire, sparks, magic. Order doesn't matter and it blooms.
		Alpha,			// Covers what's behind: smoke, dust, leaves
	};

	// One live particle. Position and velocity are in world space, or in the emitter's local space
	// when it isn't WorldSpace.
	struct Particle
	{
		glm::vec3 Position { 0.0f };
		glm::vec3 Velocity { 0.0f };	// m/s
		float Age = 0.0f;				// Seconds since it was born
		float Lifetime = 1.0f;			// Seconds it lives
	};

	// Sends out camera-facing particles from the entity (see ParticleSystem). New particles leave in
	// a cone around the entity's local +Y.
	struct ParticleEmitterComponent
	{
		bool PlayOnStart = true;
		bool Looping = true;
		float Duration = 5.0f;			// Seconds one cycle lasts (a burst starts each)
		float Rate = 20.0f;				// Particles per second while playing
		uint32_t Burst = 0;				// Particles sent at once when a cycle starts
		uint32_t MaxParticles = 1000;	// Alive at once; more aren't born

		float Lifetime = 2.0f;			// Seconds
		float LifetimeVariation = 0.25f;	// 0-1: each particle's lifetime varies this share either way
		float Speed = 2.0f;				// m/s
		float SpeedVariation = 0.25f;	// 0-1, like LifetimeVariation
		float SpreadAngle = 25.0f;		// Degrees from +Y: 0 a straight line, 180 every way

		ParticleShape Shape = ParticleShape::Point;
		glm::vec3 ShapeSize { 0.5f };	// Sphere: x is the radius; Box: half extents (meters)

		float GravityScale = 0.0f;		// How much the scene's gravity pulls them (0 none, 1 full)
		float Drag = 0.0f;				// Slows them by this share per second (air resistance)

		float StartSize = 0.25f;		// Meters across, when born
		float EndSize = 0.05f;			// Meters across, when it dies
		glm::vec4 StartColor { 1.0f, 0.6f, 0.2f, 1.0f };
		glm::vec4 EndColor { 1.0f, 0.2f, 0.05f, 0.0f };
		float Intensity = 1.0f;			// Brightness; above 1 glows (blooms)
		ParticleBlend Blend = ParticleBlend::Additive;
		std::string TexturePath;		// Empty: a soft round dot
		bool WorldSpace = true;			// Born particles stay where they are when the entity moves

		static constexpr uint32_t MaxMaxParticles = 10000;
		static constexpr uint32_t MaxBurst = 10000;
		static constexpr float MaxRate = 10000.0f;
		static constexpr float MinDuration = 0.05f, MaxDuration = 3600.0f;
		static constexpr float MinLifetime = 0.01f, MaxLifetime = 600.0f;
		static constexpr float MaxSpeed = 1000.0f;
		static constexpr float MaxShapeSize = 1000.0f;
		static constexpr float MaxGravityScale = 100.0f;
		static constexpr float MaxDrag = 100.0f;
		static constexpr float MaxSize = 1000.0f;
		static constexpr float MaxIntensity = 1000.0f;

		// Holds every value to its range (and a NaN to its default), as a loaded or edited emitter needs.
		void Clamp();

		// Runtime state (not serialized)
		std::vector<Particle> RuntimeParticles;
		bool RuntimeStarted = false;		// PlayOnStart has been applied
		bool RuntimePlaying = false;
		float RuntimeTime = 0.0f;			// Seconds into the current cycle
		float RuntimeEmitCarry = 0.0f;		// The part of a particle owed by Rate
		uint32_t RuntimePending = 0;		// Asked for (bursts, emit()) but not yet born
		uint32_t RuntimeRandom = 0;			// Random state; 0 = not yet seeded
		uint64_t RuntimeSeed = 0;			// What RuntimeRandom was seeded from (the entity's UUID)
		Ref<Texture2D> RuntimeTexture;
		std::string LoadedTexturePath;
	};

	// -------------------------------------------------------------------------
	// Game UI
	// -------------------------------------------------------------------------

	// The root of a screen UI, drawn over the game at the screen's resolution. It covers the whole screen
	// whatever its own parent is; its descendants with a UIRectComponent are laid out inside it, and a
	// TextComponent among them is screen text. Canvases with a higher SortOrder draw on top.
	struct UICanvasComponent
	{
		glm::vec2 ReferenceSize { 1920.0f, 1080.0f };	// The screen size the UI is designed for, in reference pixels
		UIScaleMode ScaleMode = UIScaleMode::Expand;
		int SortOrder = 0;
		bool Visible = true;
		// While a modal canvas is shown, every key, gamepad button and pointer press goes to it: the game
		// and the UI under it get nothing (a pause menu, a dialog).
		bool Modal = false;
		// The .btheme asset its UI is drawn with; empty is the built-in "Builtin/MageCap.btheme".
		std::string Theme;
	};

	// Where a UI entity sits inside its parent's rectangle (the canvas's screen for the top level).
	// Anchors are fractions of the parent ((0, 0) its top-left, (1, 1) its bottom-right); offsets are
	// in reference pixels from the anchored corners. Hidden rects hide their descendants too.
	struct UIRectComponent
	{
		glm::vec2 AnchorMin { 0.5f };
		glm::vec2 AnchorMax { 0.5f };
		glm::vec2 Pivot { 0.5f };					// The point that stays put when the size changes
		glm::vec2 OffsetMin { -100.0f, -25.0f };
		glm::vec2 OffsetMax { 100.0f, 25.0f };
		bool Visible = true;
		bool Interactable = true;				// Off: the pointer and focus pass it by, and everything under it
		bool Focusable = false;					// Can take keyboard and gamepad focus (widgets are by default)
		std::string StyleClass;					// A named style of the canvas's theme ("primary"); empty for the plain one
		// Inside a UILayoutComponent: keep the size from the offsets, or share the space left over. The
		// layout places the rect and ignores its anchors and offsets otherwise.
		UISizing WidthSizing = UISizing::Fixed;
		UISizing HeightSizing = UISizing::Fixed;

		// Runtime (not serialized): the rectangle on screen in pixels, from the last layout.
		UIRectangle RuntimeRect;
	};

	// A filled rectangle with optional rounded corners, border and texture, sized by the entity's
	// UIRectComponent (or filling its parent's rectangle without one).
	struct UIImageComponent
	{
		std::string TexturePath;					// An image in the assets; empty = plain color
		glm::vec4 Color { 1.0f };					// sRGB with alpha; multiplies the texture
		float CornerRadius = 0.0f;					// Reference pixels
		float BorderWidth = 0.0f;					// Reference pixels, drawn inside the rectangle
		glm::vec4 BorderColor { 0.0f, 0.0f, 0.0f, 1.0f };
		// 9-slice: the texture's edges (left, top, right, bottom, in texture pixels) keep their size at any
		// rect size (one texture pixel per reference pixel), and only the middle stretches. 0 stretches all.
		glm::vec4 SliceBorder { 0.0f };
		bool Visible = true;

		// Runtime cache (not serialized)
		Ref<Texture2D> RuntimeTexture;
		std::string LoadedTexturePath;
	};

	// Arranges the entity's children that have a UIRectComponent (and are shown): one under another,
	// side by side, or in a grid, inside the entity's rect.
	struct UILayoutComponent
	{
		UILayoutDirection Direction = UILayoutDirection::Vertical;
		float Spacing = 8.0f;						// Reference pixels between children
		glm::vec4 Padding { 0.0f };					// Left, top, right, bottom, reference pixels
		UIAlignment MainAlignment = UIAlignment::Start;
		UIAlignment CrossAlignment = UIAlignment::Start;
		int Columns = 2;							// Grid only
	};

	// Scrolls and clips the entity's children: they are laid out in its rect (or by its UILayout) and
	// moved by the scroll position, and nothing under it shows outside its rect. The mouse wheel
	// scrolls it, and focus moving to something hidden inside scrolls it into view.
	struct UIScrollComponent
	{
		bool Horizontal = false;
		bool Vertical = true;
		glm::vec2 ScrollPosition { 0.0f };			// Reference pixels from the content's top-left; kept in range
		float WheelSpeed = 60.0f;					// Reference pixels per wheel notch
		bool ShowScrollbars = true;					// Thin bars along the edges when the content doesn't fit

		// Runtime (not serialized), from the last layout, in reference pixels.
		glm::vec2 RuntimeContentSize { 0.0f };
		glm::vec2 RuntimeViewportSize { 0.0f };
	};

	// --- Widgets. Each one can take focus and stops the pointer; clicking it (or ui_accept while it has
	// focus) raises 'clicked'. A widget whose value changes raises 'changed' (value, widget). ---------

	// Colors its UIImage by state: normal, under the pointer, held down, or not interactable. A color it
	// doesn't set comes from its canvas's theme (the Button style's background for that state).
	struct UIButtonComponent
	{
		std::optional<glm::vec4> NormalColor;
		std::optional<glm::vec4> HoverColor;
		std::optional<glm::vec4> PressedColor;
		std::optional<glm::vec4> DisabledColor;

		// The color it shows in 'state' with 'style' (its theme's Button style); focus looks like normal
		// unless the theme says otherwise.
		glm::vec4 GetColor(UIWidgetState state, const UIWidgetStyle& style) const
		{
			const glm::vec4& themed = style.Get(state).Background;
			switch (state)
			{
				case UIWidgetState::Hover:		return HoverColor.value_or(themed);
				case UIWidgetState::Pressed:	return PressedColor.value_or(themed);
				case UIWidgetState::Disabled:	return DisabledColor.value_or(themed);
				case UIWidgetState::Focused:	return NormalColor.value_or(themed);
				case UIWidgetState::Normal:		break;
			}
			return NormalColor.value_or(themed);
		}
	};

	// A check box on the left of its rect (the rest is for its label, a Text on the same entity).
	// Clicking flips On and raises 'changed' (on, widget).
	struct UIToggleComponent
	{
		bool On = false;
	};

	// A horizontal track with a knob. Dragging, clicking the track, or ui_left / ui_right while it has
	// focus change Value (snapped to Step, kept between Min and Max) and raise 'changed' (value, widget).
	struct UISliderComponent
	{
		float Min = 0.0f;
		float Max = 1.0f;
		float Step = 0.0f;					// 0: any value; arrow keys then move a twentieth of the range
		float Value = 0.0f;
	};

	// A button that opens a list of options. Its Text shows the selected option. Picking one raises
	// 'changed' (index, widget).
	struct UIDropdownComponent
	{
		std::vector<std::string> Options;
		int Selected = 0;					// Index into Options; -1 shows nothing
	};

	// A field the player types into. Its text is the entity's TextComponent's (which also sets the font,
	// size and color); the field draws the caret, the selection and the placeholder.
	struct UITextInputComponent
	{
		std::string Placeholder;			// Shown, dimmed, while the text is empty
		bool MultiLine = false;				// Enter starts a new line (Ctrl+Enter submits)
		bool Password = false;				// Shows a bullet per character; copying is off
		int MaxLength = 0;					// In characters; 0 for no limit

		UITextSelection RuntimeSelection;	// Kept inside the text when it changes from elsewhere
		glm::vec2 RuntimeScroll { 0.0f };	// Screen pixels the text is moved to keep the caret in view

		// The text as shown, its rows (wrapped when multi-line) and their layout, kept between frames and
		// made again only when the text, size, width or font changes: a long text isn't wrapped every frame.
		struct ViewCache
		{
			std::string Display;							// The text as shown (bullets for a password)
			float Size = 0.0f;
			float WrapWidth = 0.0f;
			Ref<Font> TextFont;
			bool MultiLine = false;
			bool Valid = false;
			TextLayout Layout;								// The rows joined by line breaks
			std::vector<std::pair<size_t, size_t>> Rows;	// Each row's byte range in Display
			std::vector<size_t> RowFirstCharacter;			// Each row's first character index in Display
		};
		ViewCache RuntimeView;
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

	using AllComponents = ComponentGroup<TransformComponent, RelationshipComponent, MeshRendererComponent, AnimatorComponent, NavAgentComponent, CameraComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent,
		RigidBodyComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent,
		ScriptComponent, AudioSourceComponent, AudioListenerComponent, TextComponent, ParticleEmitterComponent,
		UICanvasComponent, UIRectComponent, UIImageComponent, UILayoutComponent, UIScrollComponent,
		UIButtonComponent, UIToggleComponent, UISliderComponent, UIDropdownComponent, UITextInputComponent, PrefabComponent>;

	const char* RigidBodyTypeToString(RigidBodyType type);
	RigidBodyType RigidBodyTypeFromString(std::string_view text);

}
