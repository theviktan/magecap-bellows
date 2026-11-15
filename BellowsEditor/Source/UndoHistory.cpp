#include "UndoHistory.h"

namespace Bellows {

	void UndoHistory::Push(Snapshot snapshot)
	{
		m_Undo.push_back(std::move(snapshot));
		if (m_Undo.size() > m_Capacity)
			m_Undo.erase(m_Undo.begin());
		m_Redo.clear();
	}

	bool UndoHistory::Undo(Snapshot& current)
	{
		if (m_Undo.empty())
			return false;
		Snapshot previous = std::move(m_Undo.back());
		m_Undo.pop_back();
		current.Description = previous.Description;
		m_Redo.push_back(std::move(current));
		current = std::move(previous);
		return true;
	}

	bool UndoHistory::Redo(Snapshot& current)
	{
		if (m_Redo.empty())
			return false;
		Snapshot next = std::move(m_Redo.back());
		m_Redo.pop_back();
		current.Description = next.Description;
		m_Undo.push_back(std::move(current));
		current = std::move(next);
		return true;
	}

	void UndoHistory::Clear()
	{
		m_Undo.clear();
		m_Redo.clear();
	}

	const std::string& UndoHistory::GetUndoDescription() const
	{
		static const std::string s_Empty;
		return m_Undo.empty() ? s_Empty : m_Undo.back().Description;
	}

	const std::string& UndoHistory::GetRedoDescription() const
	{
		static const std::string s_Empty;
		return m_Redo.empty() ? s_Empty : m_Redo.back().Description;
	}

}
