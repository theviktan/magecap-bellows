#include "bwpch.h"
#include "Bellows/Scene/Scene.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/UI/UIWidgets.h"

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
		struct VisitState
		{
			entt::entity Canvas = entt::null;
			UIRectangle Clip;				// What the element may draw in and be pointed at in
			float Scale = 1.0f;
			bool Visible = true;
			bool Interactable = true;
		};
		std::function<void(entt::entity, const UIRectangle&, const VisitState&)> visitChildren;
		const auto visit = [&](entt::entity handle, const UIRectangle& parentArea, const UIRectangle* placed, VisitState state)
		{
			m_UIEntities.insert(handle);
			UIRectangle rect = parentArea;
			if (auto* ui = m_Registry.try_get<UIRectComponent>(handle))
			{
				rect = placed ? *placed : ResolveUIRect(ui->AnchorMin, ui->AnchorMax, ui->OffsetMin, ui->OffsetMax, parentArea, state.Scale);
				ui->RuntimeRect = rect;
				state.Visible = state.Visible && ui->Visible;
				state.Interactable = state.Interactable && ui->Interactable;
			}
			const size_t index = m_UIElements.size();
			if (state.Visible)
			{
				m_UIElementIndex[handle] = index;
				m_UIElements.push_back({ handle, state.Canvas, rect, state.Clip, state.Scale, state.Interactable });
			}
			visitChildren(handle, rect, state);
			if (state.Visible)
				m_UIElements[index].SubtreeEnd = m_UIElements.size();
		};

		// Lays out an entity's children in its rect: placed by its UILayout, or by their own anchors;
		// moved by its UIScroll and clipped to it. A canvas below starts a UI of its own.
		visitChildren = [&](entt::entity handle, const UIRectangle& rect, const VisitState& state)
		{
			std::vector<entt::entity> children;
			for (UUID childID : m_Registry.get<RelationshipComponent>(handle).Children)
			{
				auto it = m_EntityMap.find(childID);
				if (it != m_EntityMap.end() && !m_Registry.all_of<UICanvasComponent>(it->second))
					children.push_back(it->second);
			}
			if (children.empty())
				return;

			VisitState childState = state;
			UIScrollComponent* scroll = m_Registry.try_get<UIScrollComponent>(handle);
			if (scroll)
				childState.Clip = state.Clip.Intersect(rect);

			std::vector<UIRectangle> placed;
			std::vector<int> placedIndex(children.size(), -1);
			glm::vec2 content(0.0f);
			if (const auto* layout = m_Registry.try_get<UILayoutComponent>(handle))
			{
				std::vector<UILayoutItem> items;
				for (size_t i = 0; i < children.size(); i++)
				{
					const UIRectComponent* ui = m_Registry.try_get<UIRectComponent>(children[i]);
					if (!ui || !ui->Visible)
						continue;
					placedIndex[i] = static_cast<int>(items.size());
					items.push_back({ GetUIRectSize(ui->OffsetMin, ui->OffsetMax), ui->WidthSizing, ui->HeightSizing });
				}
				UILayoutSettings settings;
				settings.Direction = layout->Direction;
				settings.Spacing = layout->Spacing;
				settings.Padding = layout->Padding;
				settings.MainAlignment = layout->MainAlignment;
				settings.CrossAlignment = layout->CrossAlignment;
				settings.Columns = layout->Columns;
				placed = ArrangeUILayout(settings, rect, items, state.Scale, &content);
			}
			else if (scroll)
			{
				// Without a layout, the content reaches as far as the children's rects do.
				for (entt::entity child : children)
				{
					if (const UIRectComponent* ui = m_Registry.try_get<UIRectComponent>(child); ui && ui->Visible)
					{
						const UIRectangle childRect = ResolveUIRect(ui->AnchorMin, ui->AnchorMax, ui->OffsetMin, ui->OffsetMax, rect, state.Scale);
						content = glm::max(content, childRect.Max - rect.Min);
					}
				}
			}

			glm::vec2 offset(0.0f);
			if (scroll)
			{
				const glm::vec2 viewport = rect.GetSize();
				glm::vec2 maxScroll = glm::max(content - viewport, glm::vec2(0.0f)) / state.Scale;
				if (!scroll->Horizontal)
					maxScroll.x = 0.0f;
				if (!scroll->Vertical)
					maxScroll.y = 0.0f;
				scroll->ScrollPosition = glm::clamp(scroll->ScrollPosition, glm::vec2(0.0f), maxScroll);
				scroll->RuntimeContentSize = content / state.Scale;
				scroll->RuntimeViewportSize = viewport / state.Scale;
				offset = -scroll->ScrollPosition * state.Scale;
			}

			const UIRectangle area { rect.Min + offset, rect.Max + offset };
			for (size_t i = 0; i < children.size(); i++)
			{
				if (placedIndex[i] >= 0)
				{
					const UIRectangle& at = placed[static_cast<size_t>(placedIndex[i])];
					const UIRectangle moved { at.Min + offset, at.Max + offset };
					visit(children[i], area, &moved, childState);
				}
				else
				{
					visit(children[i], area, nullptr, childState);
				}
			}
		};

		for (const CanvasEntry& entry : canvases)
		{
			const UICanvasComponent& canvas = m_Registry.get<UICanvasComponent>(entry.Handle);
			VisitState state;
			state.Canvas = entry.Handle;
			state.Clip = screen;
			state.Scale = ComputeUIScale(canvas.ReferenceSize, canvas.ScaleMode, screen.GetSize());
			state.Visible = canvas.Visible;
			m_UIEntities.insert(entry.Handle);
			const size_t index = m_UIElements.size();
			if (canvas.Visible)
			{
				m_UIElementIndex[entry.Handle] = index;
				m_UIElements.push_back({ entry.Handle, entry.Handle, screen, screen, state.Scale, true });
				if (canvas.Modal)
					m_UIModalCanvas = entry.Handle;	// Canvases are in drawing order, so the last one is on top
			}
			visitChildren(entry.Handle, screen, state);
			if (canvas.Visible)
				m_UIElements[index].SubtreeEnd = m_UIElements.size();
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

	namespace {

		// A 9-slice image: the texture's borders keep their size (one texture pixel per reference
		// pixel) and only the middle stretches. Borders wider than the rect shrink to fit it.
		void SubmitSlicedImage(SceneRenderer& renderer, const UIRectangle& rect, const Ref<Texture2D>& texture, const glm::vec4& border,
			const glm::vec4& color, float scale, uint32_t entityID, const glm::vec4& clip)
		{
			const glm::vec2 textureSize(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
			const glm::vec4 source = glm::clamp(border, glm::vec4(0.0f), glm::vec4(textureSize, textureSize));
			glm::vec4 target = source * scale;
			const glm::vec2 size = rect.GetSize();
			for (int axis = 0; axis < 2; axis++)
			{
				const float total = target[axis] + target[axis + 2];
				if (total > size[axis] && total > 0.0f)
				{
					target[axis] *= size[axis] / total;
					target[axis + 2] *= size[axis] / total;
				}
			}
			const float xs[4] = { rect.Min.x, rect.Min.x + target.x, rect.Max.x - target.z, rect.Max.x };
			const float ys[4] = { rect.Min.y, rect.Min.y + target.y, rect.Max.y - target.w, rect.Max.y };
			// Each piece's texture range stops half a texel short of the seams, so filtering never blends a
			// border texel into the stretched middle (or the middle into a border).
			const auto ranges = [](float size, float before, float after, float (&starts)[3], float (&ends)[3])
			{
				const float half = 0.5f / size;
				starts[0] = 0.0f;
				ends[0] = std::max(before / size - half, 0.0f);
				starts[1] = std::min(before / size + half, 1.0f - after / size);
				ends[1] = std::max(1.0f - after / size - half, starts[1]);
				starts[2] = std::min(1.0f - after / size + half, 1.0f);
				ends[2] = 1.0f;
			};
			float uStart[3], uEnd[3], vStart[3], vEnd[3];
			ranges(textureSize.x, source.x, source.z, uStart, uEnd);
			ranges(textureSize.y, source.y, source.w, vStart, vEnd);
			UIRectStyle style;
			style.Color = color;
			for (int row = 0; row < 3; row++)
			{
				for (int column = 0; column < 3; column++)
				{
					renderer.SubmitUIRect(glm::vec2(xs[column], ys[row]), glm::vec2(xs[column + 1], ys[row + 1]), style, texture, entityID, &clip,
						glm::vec4(uStart[column], vStart[row], uEnd[column], vEnd[row]));
				}
			}
		}

		// Thin bars along the right and bottom edges showing how much of the content is in view.
		void SubmitScrollbars(SceneRenderer& renderer, const UIRectangle& rect, const UIScrollComponent& scroll, float scale, const glm::vec4& clip)
		{
			const float thickness = 6.0f * scale, inset = 3.0f * scale;
			UIRectStyle track, thumb;
			track.Color = glm::vec4(1.0f, 1.0f, 1.0f, 0.12f);
			thumb.Color = glm::vec4(1.0f, 1.0f, 1.0f, 0.55f);
			track.CornerRadius = thumb.CornerRadius = thickness * 0.5f;
			for (int axis = 0; axis < 2; axis++)
			{
				const bool enabled = axis == 0 ? scroll.Horizontal : scroll.Vertical;
				const float content = scroll.RuntimeContentSize[axis], viewport = scroll.RuntimeViewportSize[axis];
				if (!enabled || content <= viewport + 0.5f || viewport <= 0.0f)
					continue;
				const int across = 1 - axis;
				glm::vec2 trackMin, trackMax;
				trackMin[axis] = rect.Min[axis] + inset;
				trackMax[axis] = rect.Max[axis] - inset;
				trackMax[across] = rect.Max[across] - inset;
				trackMin[across] = trackMax[across] - thickness;
				const float length = trackMax[axis] - trackMin[axis];
				const float thumbLength = std::max(length * viewport / content, thickness * 2.0f);
				const float travel = std::max(content - viewport, 1e-3f);
				glm::vec2 thumbMin = trackMin, thumbMax = trackMax;
				thumbMin[axis] = trackMin[axis] + (length - thumbLength) * std::clamp(scroll.ScrollPosition[axis] / travel, 0.0f, 1.0f);
				thumbMax[axis] = thumbMin[axis] + thumbLength;
				renderer.SubmitUIRect(trackMin, trackMax, track, nullptr, 0, &clip);
				renderer.SubmitUIRect(thumbMin, thumbMax, thumb, nullptr, 0, &clip);
			}
		}

	}

	namespace {

		void SubmitToggleBox(SceneRenderer& renderer, const UIRectangle& rect, bool on, float scale, uint32_t entityID, const glm::vec4& clip)
		{
			const UIRectangle box = ComputeUIToggleBox(rect, scale);
			UIRectStyle frame;
			frame.Color = UIColors::Navy;
			frame.CornerRadius = 5.0f * scale;
			frame.BorderWidth = 2.0f * scale;
			frame.BorderColor = UIColors::OffWhite;
			renderer.SubmitUIRect(box.Min, box.Max, frame, nullptr, entityID, &clip);
			if (on)
			{
				const float inset = std::max(box.GetSize().x * 0.25f, 4.0f * scale);
				UIRectStyle check;
				check.Color = UIColors::Accent;
				check.CornerRadius = 2.0f * scale;
				renderer.SubmitUIRect(box.Min + inset, box.Max - inset, check, nullptr, entityID, &clip);
			}
		}

		void SubmitSlider(SceneRenderer& renderer, const UIRectangle& rect, const UISliderComponent& slider, bool active, float scale,
			uint32_t entityID, const glm::vec4& clip)
		{
			const UISliderGeometry geometry = ComputeUISliderGeometry(rect, slider.Value, slider.Min, slider.Max, scale);
			const float halfTrack = geometry.Track.GetSize().y * 0.5f;
			UIRectStyle track;
			track.Color = UIColors::Slate;
			track.CornerRadius = halfTrack;
			renderer.SubmitUIRect(geometry.Track.Min, geometry.Track.Max, track, nullptr, entityID, &clip);
			UIRectStyle fill = track;
			fill.Color = UIColors::Accent;
			renderer.SubmitUIRect(geometry.Track.Min, glm::vec2(geometry.Knob.x, geometry.Track.Max.y), fill, nullptr, entityID, &clip);
			UIRectStyle knob;
			knob.Color = active ? UIColors::AccentLight : UIColors::OffWhite;
			knob.CornerRadius = geometry.KnobRadius;
			knob.BorderWidth = 2.0f * scale;
			knob.BorderColor = UIColors::Accent;
			renderer.SubmitUIRect(geometry.Knob - geometry.KnobRadius, geometry.Knob + geometry.KnobRadius, knob, nullptr, entityID, &clip);
		}

		// A downward-pointing arrow at the right of a dropdown, built from shrinking bars.
		void SubmitDropdownArrow(SceneRenderer& renderer, const UIRectangle& rect, bool open, float scale, uint32_t entityID, const glm::vec4& clip)
		{
			const float size = std::min(rect.GetSize().y * 0.3f, 12.0f * scale);
			const glm::vec2 center(rect.Max.x - std::min(rect.GetSize().y, 48.0f * scale) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f);
			constexpr int c_Rows = 5;
			const float rowHeight = size / static_cast<float>(c_Rows);
			UIRectStyle style;
			style.Color = UIColors::OffWhite;
			for (int i = 0; i < c_Rows; i++)
			{
				const int widthIndex = open ? i : c_Rows - 1 - i;
				const float halfWidth = size * (static_cast<float>(widthIndex) + 1.0f) / static_cast<float>(c_Rows);
				const float top = center.y - size * 0.5f + rowHeight * static_cast<float>(i);
				renderer.SubmitUIRect(glm::vec2(center.x - halfWidth, top), glm::vec2(center.x + halfWidth, top + rowHeight), style, nullptr, entityID, &clip);
			}
		}

		void SubmitFocusRing(SceneRenderer& renderer, const UIRectangle& rect, float cornerRadius, float scale, const glm::vec4& clip)
		{
			const float gap = 3.0f * scale;
			UIRectStyle ring;
			ring.Color = glm::vec4(0.0f);
			ring.CornerRadius = cornerRadius + gap;
			ring.BorderWidth = 2.0f * scale;
			ring.BorderColor = UIColors::Accent;
			renderer.SubmitUIRect(rect.Min - gap, rect.Max + gap, ring, nullptr, 0, &clip);
		}

	}

	void Scene::SubmitUI(SceneRenderer& renderer)
	{
		// Scroll bars go over the scrolled content, so each waits until its scroll area's last descendant.
		std::vector<size_t> pendingScrollbars;
		const auto flushScrollbars = [&](size_t index)
		{
			while (!pendingScrollbars.empty() && m_UIElements[pendingScrollbars.back()].SubtreeEnd <= index)
			{
				const UIElement& owner = m_UIElements[pendingScrollbars.back()];
				const glm::vec4 clip(owner.Clip.Min, owner.Clip.Max);
				SubmitScrollbars(renderer, owner.Rect, m_Registry.get<UIScrollComponent>(owner.Handle), owner.Scale, clip);
				pendingScrollbars.pop_back();
			}
		};

		for (size_t elementIndex = 0; elementIndex < m_UIElements.size(); elementIndex++)
		{
			flushScrollbars(elementIndex);
			const UIElement& element = m_UIElements[elementIndex];
			Entity entity(element.Handle, this);
			const UUID id = entity.GetUUID();
			const glm::vec4 clip(element.Clip.Min, element.Clip.Max);
			const auto* button = m_Registry.try_get<UIButtonComponent>(element.Handle);
			auto* dropdown = m_Registry.try_get<UIDropdownComponent>(element.Handle);
			const UIImageComponent* imageForRing = m_Registry.try_get<UIImageComponent>(element.Handle);
			if (auto* image = m_Registry.try_get<UIImageComponent>(element.Handle); image && image->Visible)
			{
				// A button (or a dropdown, with the button's colors) shows its state through its image.
				glm::vec4 color = image->Color;
				if (button || dropdown)
				{
					static const UIButtonComponent c_DropdownColors;
					const UIButtonComponent& colors = button ? *button : c_DropdownColors;
					if (!element.Interactable)
						color = colors.DisabledColor;
					else if (m_UIPressed == id || m_UIOpenDropdown == id)
						color = colors.PressedColor;
					else if (m_UIHovered == id)
						color = colors.HoverColor;
					else
						color = colors.NormalColor;
				}
				if (image->LoadedTexturePath != image->TexturePath)
				{
					// UI is drawn into the finished (sRGB) image, so image pixels are used as they are.
					image->RuntimeTexture = image->TexturePath.empty() ? nullptr : AssetManager::GetTexture(image->TexturePath, TextureColorSpace::Linear);
					image->LoadedTexturePath = image->TexturePath;
				}
				const bool sliced = image->RuntimeTexture && glm::any(glm::greaterThan(image->SliceBorder, glm::vec4(0.0f)));
				if (sliced)
				{
					SubmitSlicedImage(renderer, element.Rect, image->RuntimeTexture, image->SliceBorder, color, element.Scale,
						entity.GetRenderID(), clip);
				}
				else
				{
					UIRectStyle style;
					style.Color = color;
					style.CornerRadius = image->CornerRadius * element.Scale;
					style.BorderWidth = image->BorderWidth * element.Scale;
					style.BorderColor = image->BorderColor;
					renderer.SubmitUIRect(element.Rect.Min, element.Rect.Max, style, image->RuntimeTexture, entity.GetRenderID(), &clip);
				}
			}

			// Widget parts, and the area their label (Text) is laid out in.
			UIRectangle labelArea = element.Rect;
			if (const auto* toggle = m_Registry.try_get<UIToggleComponent>(element.Handle))
			{
				SubmitToggleBox(renderer, element.Rect, toggle->On, element.Scale, entity.GetRenderID(), clip);
				labelArea = ComputeUIToggleLabelArea(element.Rect, element.Scale);
			}
			if (const auto* slider = m_Registry.try_get<UISliderComponent>(element.Handle))
				SubmitSlider(renderer, element.Rect, *slider, m_UIPressed == id, element.Scale, entity.GetRenderID(), clip);
			if (dropdown)
			{
				SubmitDropdownArrow(renderer, element.Rect, m_UIOpenDropdown == id, element.Scale, entity.GetRenderID(), clip);
				labelArea = ComputeUIDropdownLabelArea(element.Rect, element.Scale);
				// The dropdown's Text shows its selected option.
				auto* label = m_Registry.try_get<TextComponent>(element.Handle);
				if (label && dropdown->Selected >= 0 && dropdown->Selected < static_cast<int>(dropdown->Options.size()))
					label->Text = dropdown->Options[static_cast<size_t>(dropdown->Selected)];
			}

			if (auto* text = m_Registry.try_get<TextComponent>(element.Handle); text && text->Visible && !text->Text.empty())
			{
				if (const TextLayout* layout = LayoutTextComponent(*text, text->Size * element.Scale, labelArea.GetSize()))
				{
					TextStyle style;
					style.Color = text->Color;
					style.OutlineColor = text->OutlineColor;
					style.OutlineWidth = text->OutlineWidth;
					const UIRectangle textClip = text->Overflow == TextOverflow::Clip ? element.Clip.Intersect(labelArea) : element.Clip;
					const glm::vec4 clipRect(textClip.Min, textClip.Max);
					renderer.SubmitUIText(text->RuntimeFont, *layout, labelArea.Min, style, entity.GetRenderID(), &clipRect);
				}
			}

			// Keyboard and gamepad focus shows as a ring around the focused UI.
			if (m_UIFocused == id)
				SubmitFocusRing(renderer, element.Rect, imageForRing ? imageForRing->CornerRadius * element.Scale : 0.0f, element.Scale, clip);

			if (const auto* scroll = m_Registry.try_get<UIScrollComponent>(element.Handle); scroll && scroll->ShowScrollbars)
				pendingScrollbars.push_back(elementIndex);
		}
		flushScrollbars(m_UIElements.size());
		SubmitUIDropdownList(renderer);
	}

	void Scene::SubmitUIDropdownList(SceneRenderer& renderer)
	{
		const UIElement* element = FindUIElement(m_UIOpenDropdown);
		Entity entity = TryGetEntityByUUID(m_UIOpenDropdown);
		const auto* dropdown = entity ? entity.TryGetComponent<UIDropdownComponent>() : nullptr;
		if (!element || !dropdown || dropdown->Options.empty())
			return;

		// The list draws over everything, as tall as the dropdown per row, in the dropdown's font.
		const glm::vec2 screen(static_cast<float>(renderer.GetViewportWidth()), static_cast<float>(renderer.GetViewportHeight()));
		const size_t count = dropdown->Options.size();
		const UIRectangle list = ComputeUIDropdownList(element->Rect, count, screen);
		const glm::vec4 clip(glm::vec2(0.0f), screen);
		const uint32_t entityID = entity.GetRenderID();
		UIRectStyle background;
		background.Color = UIColors::Navy;
		background.CornerRadius = 8.0f * element->Scale;
		background.BorderWidth = 1.0f * element->Scale;
		background.BorderColor = UIColors::Slate;
		renderer.SubmitUIRect(list.Min, list.Max, background, nullptr, entityID, &clip);

		const TextComponent* label = entity.TryGetComponent<TextComponent>();
		const Ref<Font> font = AssetManager::GetFont(label ? label->FontPath : std::string());
		const float rowHeight = list.GetSize().y / static_cast<float>(count);
		for (size_t i = 0; i < count; i++)
		{
			const UIRectangle row { glm::vec2(list.Min.x, list.Min.y + rowHeight * static_cast<float>(i)),
				glm::vec2(list.Max.x, list.Min.y + rowHeight * static_cast<float>(i + 1)) };
			if (static_cast<int>(i) == m_UIDropdownHighlight)
			{
				UIRectStyle highlight;
				highlight.Color = UIColors::Slate;
				highlight.CornerRadius = 6.0f * element->Scale;
				const float inset = 3.0f * element->Scale;
				renderer.SubmitUIRect(row.Min + inset, row.Max - inset, highlight, nullptr, entityID, &clip);
			}
			if (!font)
				continue;
			TextLayoutSettings settings;
			settings.Size = (label ? label->Size : TextComponent::DefaultScreenSize) * element->Scale;
			const UIRectangle area = ComputeUIDropdownLabelArea(row, element->Scale);
			settings.BoxSize = area.GetSize();
			settings.Wrap = false;
			settings.Alignment = label ? label->Alignment : TextAlignment::Left;
			settings.VerticalAlignment = TextVerticalAlignment::Middle;
			settings.Overflow = TextOverflow::Ellipsis;
			const TextLayout layout = LayoutText(*font, dropdown->Options[i], settings);
			TextStyle style;
			style.Color = static_cast<int>(i) == dropdown->Selected ? UIColors::AccentLight : (label ? label->Color : UIColors::OffWhite);
			renderer.SubmitUIText(font, layout, area.Min, style, entityID, &clip);
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
		return rect && (rect->Focusable || IsUIWidget(element->Handle));
	}

	bool Scene::IsUIWidget(entt::entity handle) const
	{
		return m_Registry.any_of<UIButtonComponent, UIToggleComponent, UISliderComponent, UIDropdownComponent>(handle);
	}

	void Scene::ActivateUIWidget(UUID id, std::vector<UIEvent>& events)
	{
		Entity entity = TryGetEntityByUUID(id);
		if (!entity)
			return;
		if (auto* toggle = entity.TryGetComponent<UIToggleComponent>())
		{
			toggle->On = !toggle->On;
			events.push_back({ "changed", id, toggle->On });
		}
		if (auto* dropdown = entity.TryGetComponent<UIDropdownComponent>(); dropdown && !dropdown->Options.empty())
		{
			m_UIOpenDropdown = id;
			m_UIDropdownHighlight = std::clamp(dropdown->Selected, 0, static_cast<int>(dropdown->Options.size()) - 1);
		}
	}

	void Scene::SetUISliderValue(UUID id, float value, std::vector<UIEvent>& events)
	{
		Entity entity = TryGetEntityByUUID(id);
		auto* slider = entity ? entity.TryGetComponent<UISliderComponent>() : nullptr;
		if (!slider)
			return;
		value = SnapUISliderValue(value, slider->Min, slider->Max, slider->Step);
		if (value == slider->Value)
			return;
		slider->Value = value;
		events.push_back({ "changed", id, static_cast<double>(value) });
	}

	void Scene::SelectUIDropdownOption(UUID id, int index, std::vector<UIEvent>& events)
	{
		Entity entity = TryGetEntityByUUID(id);
		auto* dropdown = entity ? entity.TryGetComponent<UIDropdownComponent>() : nullptr;
		if (!dropdown || index < 0 || index >= static_cast<int>(dropdown->Options.size()) || index == dropdown->Selected)
			return;
		dropdown->Selected = index;
		if (auto* text = entity.TryGetComponent<TextComponent>())
			text->Text = dropdown->Options[static_cast<size_t>(index)];
		events.push_back({ "changed", id, static_cast<double>(index) });
	}

	void Scene::UpdateUIDropdownList(const glm::vec2& pointer, const glm::vec2& screen, std::vector<UIEvent>& events)
	{
		const UIElement* element = FindUIElement(m_UIOpenDropdown);
		Entity entity = TryGetEntityByUUID(m_UIOpenDropdown);
		const auto* dropdown = entity ? entity.TryGetComponent<UIDropdownComponent>() : nullptr;
		if (!element || !element->Interactable || !dropdown || dropdown->Options.empty())
		{
			m_UIOpenDropdown = 0;
			return;
		}

		const size_t count = dropdown->Options.size();
		const UIRectangle list = ComputeUIDropdownList(element->Rect, count, screen);
		const int row = UIDropdownRowAt(list, count, pointer);
		if (row >= 0 && Input::GetMouseDelta() != glm::vec2(0.0f))
			m_UIDropdownHighlight = row;

		const auto pressed = [](const char* action) { return InputMap::IsActionPressed(action, InputAccess::Raw); };
		const UUID id = m_UIOpenDropdown;
		if (Input::IsMouseButtonPressed(MouseButton::Left, InputAccess::Raw))
		{
			// A press anywhere closes the list: on a row it picks that option first. The press is the UI's
			// until it is let go, and clicks nothing else.
			if (row >= 0)
				SelectUIDropdownOption(id, row, events);
			m_UIOpenDropdown = 0;
			m_UIPressed = 0;
			m_UIPointerOwner = UIPointerOwner::UI;
			return;
		}
		if (pressed("ui_up"))
			m_UIDropdownHighlight = std::max(m_UIDropdownHighlight - 1, 0);
		if (pressed("ui_down"))
			m_UIDropdownHighlight = std::min(m_UIDropdownHighlight + 1, static_cast<int>(count) - 1);
		if (pressed("ui_accept"))
		{
			SelectUIDropdownOption(id, m_UIDropdownHighlight, events);
			m_UIOpenDropdown = 0;
		}
		else if (pressed("ui_cancel"))
		{
			m_UIOpenDropdown = 0;
		}
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
		// UI events carry the UI entity as their last value (after the new value, for 'changed') and climb
		// through its parents.
		for (const UIEvent& event : events)
		{
			Entity source = TryGetEntityByUUID(event.Source);
			if (!source)
				continue;
			Scripting::ScriptValueList values;
			if (const bool* on = std::get_if<bool>(&event.Value))
				values.emplace_back(*on);
			else if (const double* number = std::get_if<double>(&event.Value))
				values.emplace_back(*number);
			values.emplace_back(Scripting::ScriptEntity(source));
			Scripting::EmitScriptEventFrom(Scripting::ScriptEntity(source), event.Name, values);
		}
	}

	void Scene::ResetUIInput()
	{
		m_UIOpenDropdown = 0;
		m_UIDropdownHighlight = -1;
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
		const UUID focusedBefore = m_UIFocused;

		// Focus on something hidden, disabled or behind a modal canvas is let go.
		if (m_UIFocused && !CanTakeUIFocus(m_UIFocused))
			ChangeUIFocus(0, events);

		// The pointer: the frontmost interactable rect under it that stops it (a panel with an image, a
		// focusable rect or a scroll area). Text and bare rects let it through, and so does UI scrolled
		// out of view. Under a modal canvas only its UI counts.
		glm::vec2 pointer = Input::GetMousePosition();
		const glm::vec2 pointerSpace = Input::GetViewportSize();
		if (pointerSpace.x > 0.0f && pointerSpace.y > 0.0f)
			pointer *= screen / pointerSpace;
		UUID target = 0;
		for (auto it = m_UIElements.rbegin(); it != m_UIElements.rend(); ++it)
		{
			if (!it->Interactable || (modal && it->Canvas != m_UIModalCanvas) || !it->Rect.Contains(pointer) || !it->Clip.Contains(pointer))
				continue;
			const UIRectComponent* rect = m_Registry.try_get<UIRectComponent>(it->Handle);
			const UIImageComponent* image = m_Registry.try_get<UIImageComponent>(it->Handle);
			if ((rect && rect->Focusable) || (image && image->Visible) || m_Registry.all_of<UIScrollComponent>(it->Handle) || IsUIWidget(it->Handle))
			{
				target = m_Registry.get<IDComponent>(it->Handle).ID;
				break;
			}
		}
		m_UIHovered = target;

		// An open dropdown list sits over everything and takes every input until it closes.
		if (m_UIOpenDropdown)
		{
			m_UIHovered = 0;
			UpdateUIDropdownList(pointer, screen, events);
			if (!Input::IsMouseButtonDown(MouseButton::Left, InputAccess::Raw))
				m_UIPointerOwner = UIPointerOwner::None;
			m_UICapture = InputCapture { true, true, true };
			Input::SetCapture(m_UICapture);
			EmitUIEvents(events);
			return;
		}

		// The wheel scrolls the frontmost scroll area under the pointer that has somewhere to go that way.
		if (const glm::vec2 wheel = Input::GetScrollDelta(InputAccess::Raw); wheel != glm::vec2(0.0f))
		{
			for (auto it = m_UIElements.rbegin(); it != m_UIElements.rend(); ++it)
			{
				UIScrollComponent* scroll = m_Registry.try_get<UIScrollComponent>(it->Handle);
				if (!scroll || !it->Interactable || (modal && it->Canvas != m_UIModalCanvas) || !it->Rect.Contains(pointer) || !it->Clip.Contains(pointer))
					continue;
				const glm::vec2 room = glm::max(scroll->RuntimeContentSize - scroll->RuntimeViewportSize, glm::vec2(0.0f));
				// A scroll area that only goes sideways takes the ordinary (vertical) wheel too.
				const glm::vec2 amount = scroll->Vertical ? glm::vec2(wheel.x, wheel.y) : glm::vec2(wheel.x + wheel.y, 0.0f);
				const bool canMove = (scroll->Horizontal && room.x > 0.0f && amount.x != 0.0f) || (scroll->Vertical && room.y > 0.0f && amount.y != 0.0f);
				if (!canMove)
					continue;
				scroll->ScrollPosition = glm::clamp(scroll->ScrollPosition - amount * scroll->WheelSpeed, glm::vec2(0.0f), room);
				break;
			}
		}

		if (Input::IsMouseButtonPressed(MouseButton::Left, InputAccess::Raw))
		{
			// A press belongs to whatever it started on until the button is let go, so a drag that starts
			// in the game and ends over a panel still reaches the game.
			m_UIPointerOwner = target || modal ? UIPointerOwner::UI : UIPointerOwner::Game;
			m_UIPressed = target;
			ChangeUIFocus(target && CanTakeUIFocus(target) ? target : UUID(0), events);
		}

		// A slider follows the pointer while the press that started on it is held.
		if (m_UIPressed && Input::IsMouseButtonDown(MouseButton::Left, InputAccess::Raw))
		{
			const UIElement* element = FindUIElement(m_UIPressed);
			if (const auto* slider = element ? m_Registry.try_get<UISliderComponent>(element->Handle) : nullptr)
				SetUISliderValue(m_UIPressed, UISliderValueAt(element->Rect, pointer.x, slider->Min, slider->Max, slider->Step, element->Scale), events);
		}

		InputCapture capture;
		capture.Pointer = modal || m_UIPointerOwner == UIPointerOwner::UI || (m_UIPointerOwner == UIPointerOwner::None && target);
		capture.Keyboard = modal;
		capture.Gamepad = modal;

		if (Input::IsMouseButtonReleased(MouseButton::Left, InputAccess::Raw))
		{
			if (m_UIPressed && m_UIPressed == target)
			{
				events.push_back({ "clicked", target });
				ActivateUIWidget(target, events);
			}
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
			else if (const UIElement* focused = FindUIElement(m_UIFocused);
				focused && m_Registry.all_of<UISliderComponent>(focused->Handle) && (pressed("ui_left") || pressed("ui_right")))
			{
				// Left and right move a focused slider instead of focus.
				const auto& slider = m_Registry.get<UISliderComponent>(focused->Handle);
				const float direction = (pressed("ui_right") ? 1.0f : 0.0f) - (pressed("ui_left") ? 1.0f : 0.0f);
				const float sign = slider.Max >= slider.Min ? 1.0f : -1.0f;
				SetUISliderValue(m_UIFocused, slider.Value + direction * sign * UISliderKeyStep(slider.Min, slider.Max, slider.Step), events);
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
			{
				events.push_back({ "clicked", m_UIFocused });
				ActivateUIWidget(m_UIFocused, events);
			}
			if (pressed("ui_cancel"))
			{
				if (m_UIFocused)
					events.push_back({ "cancelled", m_UIFocused });
				else if (modal)
					events.push_back({ "cancelled", m_Registry.get<IDComponent>(m_UIModalCanvas).ID });
			}
		}

		if (m_UIFocused != focusedBefore)
			ScrollUIIntoView(m_UIFocused);
		m_UICapture = capture;
		Input::SetCapture(capture);
		EmitUIEvents(events);
	}

	void Scene::ScrollUIIntoView(UUID id)
	{
		const UIElement* element = FindUIElement(id);
		if (!element)
			return;
		// Each scroll area above it moves just far enough to show it (its top-left first when it is larger).
		UIRectangle target = element->Rect;
		for (Entity ancestor = TryGetEntityByUUID(id).GetParent(); ancestor; ancestor = ancestor.GetParent())
		{
			auto* scroll = ancestor.TryGetComponent<UIScrollComponent>();
			const UIElement* view = scroll ? FindUIElement(ancestor.GetUUID()) : nullptr;
			if (!view)
				continue;
			// The scroll area's layout padding comes into view too, so the first and last items don't end
			// flush against its edges.
			const auto* layout = ancestor.TryGetComponent<UILayoutComponent>();
			const glm::vec4 margin = layout ? layout->Padding * view->Scale : glm::vec4(0.0f);
			glm::vec2 shift(0.0f);
			for (int axis = 0; axis < 2; axis++)
			{
				if (!(axis == 0 ? scroll->Horizontal : scroll->Vertical))
					continue;
				const float viewMin = view->Rect.Min[axis] + margin[axis], viewMax = view->Rect.Max[axis] - margin[axis + 2];
				if (target.Max[axis] > viewMax)
					shift[axis] = target.Max[axis] - viewMax;
				if (target.Min[axis] - shift[axis] < viewMin)
					shift[axis] = target.Min[axis] - viewMin;
			}
			const glm::vec2 room = glm::max(scroll->RuntimeContentSize - scroll->RuntimeViewportSize, glm::vec2(0.0f));
			const glm::vec2 before = scroll->ScrollPosition;
			scroll->ScrollPosition = glm::clamp(scroll->ScrollPosition + shift / view->Scale, glm::vec2(0.0f), room);
			const glm::vec2 moved = (scroll->ScrollPosition - before) * view->Scale;
			target.Min -= moved;
			target.Max -= moved;
		}
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
		if (id)
			ScrollUIIntoView(id);
		EmitUIEvents(events);
		return true;
	}

}
