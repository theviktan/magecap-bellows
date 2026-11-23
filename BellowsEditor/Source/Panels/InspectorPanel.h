#pragma once

#include "Bellows/Renderer/Material.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace Bellows {

	class EditorLayer;

	// Shows and edits the selected entity's components (or the selected material asset).
	class InspectorPanel
	{
	public:
		explicit InspectorPanel(EditorLayer& editor)
			: m_Editor(editor) {}

		// The panel is drawn as a closable tab; closing it clears *open (View > Panels shows it again).
		void OnImGuiRender(bool* open);
	private:
		void FlushMaterialSave();
		void DrawEntity(Entity entity);
		void DrawAddComponentMenu(Entity entity);
		void DrawTags(Entity entity);
		void DrawScriptComponent(Entity entity, ScriptComponent& component);
		void DrawMeshRenderer(Entity entity, MeshRendererComponent& component);
		void DrawAnimator(Entity entity, AnimatorComponent& component);
		bool DrawMaterialEditor(const Ref<Material>& material, const std::string& assetPath);
		const ScriptClassInfo* GetScriptInfo(const std::string& scriptPath, const std::string& className);
	private:
		struct CachedScriptInfo
		{
			ScriptClassInfo Info;
			std::filesystem::file_time_type WriteTime;
			bool Valid = false;
			std::string Error;
		};

		EditorLayer& m_Editor;
		std::unordered_map<std::string, CachedScriptInfo> m_ScriptInfoCache;
		std::string m_ComponentFilter;
		std::string m_NewTag;		// The tag being typed into the Tags field
		std::string m_TagProblem;	// Why the last tag typed there can't be used
		UUID m_TagEntity = 0;		// The entity they belong to

		// Material edits are written to disk once the edit is finished, not every frame of a drag.
		Ref<Material> m_PendingMaterialSave;
		std::string m_PendingMaterialPath;
	};

}
