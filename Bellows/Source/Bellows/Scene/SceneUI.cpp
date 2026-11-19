#include "bwpch.h"
#include "Bellows/Scene/Scene.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"

// The game UI: layout, drawing and input. Kept apart from Scene.cpp, which runs everything else.

namespace Bellows {

	namespace {

		// Where an entity sits in the hierarchy: its index among the roots, then among each parent's
		// children down to it. Comparing these orders entities as the hierarchy panel lists them.
		std::vector<size_t> GetHierarchyPath(Entity entity, const std::vector<UUID>& roots)
		{
			std::vector<size_t> path;
			for (Entity current = entity; current;)
			{
				const UUID id = current.GetUUID();
				Entity parent = current.GetParent();
				const std::vector<UUID>& siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : roots;
				path.push_back(static_cast<size_t>(std::find(siblings.begin(), siblings.end(), id) - siblings.begin()));
				current = parent;
			}
			std::reverse(path.begin(), path.end());
			return path;
		}

	}

	void Scene::UpdateUILayout(const glm::vec2& screenSize)
	{
		m_UIElements.clear();
		m_UIElementIndex.clear();
		m_UIEntities.clear();
		m_UIModalCanvas = entt::null;

		// Canvases draw by SortOrder, and in hierarchy order where the sort orders are equal.
		struct CanvasEntry
		{
			entt::entity Handle;
			int SortOrder;
			std::vector<size_t> Path;
		};
		std::vector<CanvasEntry> canvases;
		auto canvasView = m_Registry.view<UICanvasComponent>();
		for (entt::entity handle : canvasView)
			canvases.push_back({ handle, canvasView.get<UICanvasComponent>(handle).SortOrder, GetHierarchyPath(Entity(handle, this), m_RootEntities) });
		std::sort(canvases.begin(), canvases.end(), [](const CanvasEntry& a, const CanvasEntry& b)
		{
			return a.SortOrder != b.SortOrder ? a.SortOrder < b.SortOrder : a.Path < b.Path;
		});

		const UIRectangle screen { glm::vec2(0.0f), glm::max(screenSize, glm::vec2(0.0f)) };
		std::function<void(entt::entity, entt::entity, const UIRectangle&, float, bool, bool)> visit =
			[&](entt::entity handle, entt::entity canvas, const UIRectangle& parentRect, float scale, bool visible, bool interactable)
		{
			m_UIEntities.insert(handle);
			UIRectangle rect = parentRect;
			if (auto* ui = m_Registry.try_get<UIRectComponent>(handle))
			{
				rect = ResolveUIRect(ui->AnchorMin, ui->AnchorMax, ui->OffsetMin, ui->OffsetMax, parentRect, scale);
				ui->RuntimeRect = rect;
				visible = visible && ui->Visible;
				interactable = interactable && ui->Interactable;
			}
			if (visible)
			{
				m_UIElementIndex[handle] = m_UIElements.size();
				m_UIElements.push_back({ handle, canvas, rect, scale, interactable });
			}
			for (UUID childID : m_Registry.get<RelationshipComponent>(handle).Children)
			{
				// A canvas below starts a UI of its own.
				auto it = m_EntityMap.find(childID);
				if (it != m_EntityMap.end() && !m_Registry.all_of<UICanvasComponent>(it->second))
					visit(it->second, canvas, rect, scale, visible, interactable);
			}
		};

		for (const CanvasEntry& entry : canvases)
		{
			const UICanvasComponent& canvas = m_Registry.get<UICanvasComponent>(entry.Handle);
			const float scale = ComputeUIScale(canvas.ReferenceSize, canvas.ScaleMode, screen.GetSize());
			m_UIEntities.insert(entry.Handle);
			if (canvas.Visible)
			{
				m_UIElementIndex[entry.Handle] = m_UIElements.size();
				m_UIElements.push_back({ entry.Handle, entry.Handle, screen, scale, true });
				if (canvas.Modal)
					m_UIModalCanvas = entry.Handle;	// Canvases are in drawing order, so the last one is on top
			}
			for (UUID childID : m_Registry.get<RelationshipComponent>(entry.Handle).Children)
			{
				auto it = m_EntityMap.find(childID);
				if (it != m_EntityMap.end() && !m_Registry.all_of<UICanvasComponent>(it->second))
					visit(it->second, entry.Handle, screen, scale, canvas.Visible, true);
			}
		}
	}

	bool Scene::IsUIEntity(Entity entity) const
	{
		return entity && m_UIEntities.contains(static_cast<entt::entity>(entity));
	}

	std::vector<Entity> Scene::GetVisibleUIEntities()
	{
		std::vector<Entity> entities;
		entities.reserve(m_UIElements.size());
		for (const UIElement& element : m_UIElements)
			entities.emplace_back(element.Handle, this);
		return entities;
	}

	bool Scene::IsInsideUICanvas(Entity entity)
	{
		for (Entity current = entity; current; current = current.GetParent())
		{
			if (current.HasComponent<UICanvasComponent>())
				return true;
		}
		return false;
	}

	void Scene::SubmitUI(SceneRenderer& renderer)
	{
		for (const UIElement& element : m_UIElements)
		{
			Entity entity(element.Handle, this);
			if (auto* image = m_Registry.try_get<UIImageComponent>(element.Handle); image && image->Visible)
			{
				if (image->LoadedTexturePath != image->TexturePath)
				{
					// UI is drawn into the finished (sRGB) image, so image pixels are used as they are.
					image->RuntimeTexture = image->TexturePath.empty() ? nullptr : AssetManager::GetTexture(image->TexturePath, TextureColorSpace::Linear);
					image->LoadedTexturePath = image->TexturePath;
				}
				UIRectStyle style;
				style.Color = image->Color;
				style.CornerRadius = image->CornerRadius * element.Scale;
				style.BorderWidth = image->BorderWidth * element.Scale;
				style.BorderColor = image->BorderColor;
				renderer.SubmitUIRect(element.Rect.Min, element.Rect.Max, style, image->RuntimeTexture, entity.GetRenderID());
			}

			if (auto* text = m_Registry.try_get<TextComponent>(element.Handle); text && text->Visible && !text->Text.empty())
			{
				const TextLayout* layout = LayoutTextComponent(*text, text->Size * element.Scale, element.Rect.GetSize());
				if (!layout)
					continue;
				TextStyle style;
				style.Color = text->Color;
				style.OutlineColor = text->OutlineColor;
				style.OutlineWidth = text->OutlineWidth;
				const glm::vec4 clip(element.Rect.Min, element.Rect.Max);
				renderer.SubmitUIText(text->RuntimeFont, *layout, element.Rect.Min, style, entity.GetRenderID(),
					text->Overflow == TextOverflow::Clip ? &clip : nullptr);
			}
		}
	}

	const Scene::UIElement* Scene::FindUIElement(UUID id) const
	{
		auto entity = m_EntityMap.find(id);
		if (entity == m_EntityMap.end())
			return nullptr;
		auto element = m_UIElementIndex.find(entity->second);
		return element != m_UIElementIndex.end() ? &m_UIElements[element->second] : nullptr;
	}

	bool Scene::CanTakeUIFocus(UUID id) const
	{
		const UIElement* element = FindUIElement(id);
		if (!element || !element->Interactable)
			return false;
		if (m_UIModalCanvas != entt::null && element->Canvas != m_UIModalCanvas)
			return false;
		const UIRectComponent* rect = m_Registry.try_get<UIRectComponent>(element->Handle);
		return rect && rect->Focusable;
	}

	void Scene::ChangeUIFocus(UUID id, std::vector<UIEvent>& events)
	{
		if (id == m_UIFocused)
			return;
		if (m_UIFocused)
			events.push_back({ "unfocused", m_UIFocused });
		m_UIFocused = id;
		if (m_UIFocused)
			events.push_back({ "focused", m_UIFocused });
	}

	void Scene::EmitUIEvents(const std::vector<UIEvent>& events)
	{
		// UI events carry the UI entity as their last (here only) value and climb through its parents.
		for (const UIEvent& event : events)
		{
			if (Entity source = TryGetEntityByUUID(event.Source))
				Scripting::EmitScriptEventFrom(Scripting::ScriptEntity(source), event.Name, { Scripting::ScriptValue(Scripting::ScriptEntity(source)) });
		}
	}

	void Scene::ResetUIInput()
	{
		m_UIHovered = 0;
		m_UIPressed = 0;
		m_UIFocused = 0;
		m_UIPointerOwner = UIPointerOwner::None;
		m_UICapture = InputCapture();
		Input::SetCapture(m_UICapture);
	}

	void Scene::UpdateUIInput()
	{
		const glm::vec2 screen(static_cast<float>(m_ViewportWidth), static_cast<float>(m_ViewportHeight));
		UpdateUILayout(screen);
		std::vector<UIEvent> events;
		const bool modal = m_UIModalCanvas != entt::null;

		// Focus on something hidden, disabled or behind a modal canvas is let go.
		if (m_UIFocused && !CanTakeUIFocus(m_UIFocused))
			ChangeUIFocus(0, events);

		// The pointer: the frontmost interactable rect under it that stops it (a panel with an image, or
		// a focusable rect). Text and bare rects let it through. Under a modal canvas only its UI counts.
		glm::vec2 pointer = Input::GetMousePosition();
		const glm::vec2 pointerSpace = Input::GetViewportSize();
		if (pointerSpace.x > 0.0f && pointerSpace.y > 0.0f)
			pointer *= screen / pointerSpace;
		UUID target = 0;
		for (auto it = m_UIElements.rbegin(); it != m_UIElements.rend(); ++it)
		{
			if (!it->Interactable || (modal && it->Canvas != m_UIModalCanvas) || !it->Rect.Contains(pointer))
				continue;
			const UIRectComponent* rect = m_Registry.try_get<UIRectComponent>(it->Handle);
			const UIImageComponent* image = m_Registry.try_get<UIImageComponent>(it->Handle);
			if ((rect && rect->Focusable) || (image && image->Visible))
			{
				target = m_Registry.get<IDComponent>(it->Handle).ID;
				break;
			}
		}
		m_UIHovered = target;

		if (Input::IsMouseButtonPressed(MouseButton::Left, InputAccess::Raw))
		{
			// A press belongs to whatever it started on until the button is let go, so a drag that starts
			// in the game and ends over a panel still reaches the game.
			m_UIPointerOwner = target || modal ? UIPointerOwner::UI : UIPointerOwner::Game;
			m_UIPressed = target;
			ChangeUIFocus(target && CanTakeUIFocus(target) ? target : UUID(0), events);
		}

		InputCapture capture;
		capture.Pointer = modal || m_UIPointerOwner == UIPointerOwner::UI || (m_UIPointerOwner == UIPointerOwner::None && target);
		capture.Keyboard = modal;
		capture.Gamepad = modal;

		if (Input::IsMouseButtonReleased(MouseButton::Left, InputAccess::Raw))
		{
			if (m_UIPressed && m_UIPressed == target)
				events.push_back({ "clicked", target });
			m_UIPressed = 0;
			m_UIPointerOwner = UIPointerOwner::None;
		}
		else if (!Input::IsMouseButtonDown(MouseButton::Left, InputAccess::Raw))
		{
			// The press ended without a release this frame (input was reset), so nobody holds it.
			m_UIPressed = 0;
			m_UIPointerOwner = UIPointerOwner::None;
		}

		// Keyboard and gamepad navigation, while something has focus or a modal canvas is shown (so the
		// arrow keys stay the game's otherwise).
		if (m_UIFocused || modal)
		{
			std::vector<UUID> focusables;
			std::vector<UIRectangle> rects;
			int current = -1;
			for (const UIElement& element : m_UIElements)
			{
				const UUID id = m_Registry.get<IDComponent>(element.Handle).ID;
				if (!CanTakeUIFocus(id))
					continue;
				if (id == m_UIFocused)
					current = static_cast<int>(focusables.size());
				focusables.push_back(id);
				rects.push_back(element.Rect);
			}

			const auto pressed = [](const char* action) { return InputMap::IsActionPressed(action, InputAccess::Raw); };
			const bool shift = Input::IsKeyDown(KeyCode::LeftShift, InputAccess::Raw) || Input::IsKeyDown(KeyCode::RightShift, InputAccess::Raw);
			const bool nextPressed = pressed("ui_next");
			const bool previous = pressed("ui_previous") || (nextPressed && shift);
			const bool next = nextPressed && !previous;
			const int count = static_cast<int>(focusables.size());
			if ((next || previous) && count > 0)
			{
				const int index = current < 0 ? (next ? 0 : count - 1) : (current + (next ? 1 : count - 1)) % count;
				ChangeUIFocus(focusables[static_cast<size_t>(index)], events);
			}
			else if (count > 0)
			{
				static constexpr std::pair<const char*, UINavigationDirection> c_Directions[] = {
					{ "ui_up", UINavigationDirection::Up }, { "ui_down", UINavigationDirection::Down },
					{ "ui_left", UINavigationDirection::Left }, { "ui_right", UINavigationDirection::Right } };
				for (const auto& [action, direction] : c_Directions)
				{
					if (!pressed(action))
						continue;
					// With nothing focused yet (a modal canvas just opened) the first press picks the first one.
					const int index = current < 0 ? 0 : FindUINavigationTarget(rects[static_cast<size_t>(current)], rects, direction, current);
					if (index >= 0)
						ChangeUIFocus(focusables[static_cast<size_t>(index)], events);
					break;
				}
			}

			if (pressed("ui_accept") && m_UIFocused)
				events.push_back({ "clicked", m_UIFocused });
			if (pressed("ui_cancel"))
			{
				if (m_UIFocused)
					events.push_back({ "cancelled", m_UIFocused });
				else if (modal)
					events.push_back({ "cancelled", m_Registry.get<IDComponent>(m_UIModalCanvas).ID });
			}
		}

		m_UICapture = capture;
		Input::SetCapture(capture);
		EmitUIEvents(events);
	}

	Entity Scene::GetUIHovered()
	{
		return TryGetEntityByUUID(m_UIHovered);
	}

	Entity Scene::GetUIPressed()
	{
		return TryGetEntityByUUID(m_UIPressed);
	}

	Entity Scene::GetUIFocused()
	{
		return TryGetEntityByUUID(m_UIFocused);
	}

	bool Scene::SetUIFocus(Entity entity)
	{
		// Lay out again first, so UI a script has just made or shown can take focus straight away.
		UpdateUILayout(glm::vec2(static_cast<float>(m_ViewportWidth), static_cast<float>(m_ViewportHeight)));
		const UUID id = entity ? entity.GetUUID() : UUID(0);
		if (id && !CanTakeUIFocus(id))
			return false;
		std::vector<UIEvent> events;
		ChangeUIFocus(id, events);
		EmitUIEvents(events);
		return true;
	}

}
