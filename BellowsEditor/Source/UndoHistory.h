#pragma once

#include "Bellows/Core/UUID.h"

#include <string>
#include <vector>

namespace Bellows {

	// Snapshot-based undo: each entry is the complete scene document before a change, plus the selection.
	// Scenes are small JSON documents, which makes this simple and always correct (no per-command inverse logic).
	class UndoHistory
	{
	public:
		struct Snapshot
		{
			std::string SceneJson;
			std::vector<UUID> Selection;
			std::string Description;
		};

		explicit UndoHistory(size_t capacity = 128)
			: m_Capacity(capacity) {}

		// Records the state *before* a change.
		void Push(Snapshot snapshot);
		// Swaps 'current' with the most recent undo/redo state. Returns false when there is nothing to do.
		bool Undo(Snapshot& current);
		bool Redo(Snapshot& current);
		void Clear();

		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }
		const std::string& GetUndoDescription() const;
		const std::string& GetRedoDescription() const;
		size_t GetUndoCount() const { return m_Undo.size(); }
	private:
		std::vector<Snapshot> m_Undo;
		std::vector<Snapshot> m_Redo;
		size_t m_Capacity;
	};

}
