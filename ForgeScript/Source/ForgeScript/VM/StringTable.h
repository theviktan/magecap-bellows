#pragma once

#include "ForgeScript/Core/Object.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ForgeScript
{
	// Every text a VM makes, found by its contents, so equal texts are one String (interning).
	// An open-addressing table: a lookup is one hash and usually one or two probes, and adding a text
	// allocates nothing beyond the String itself. std::unordered_map needed a second hash and a node
	// allocation for every new text, which was most of the cost of building text in loops.
	class StringTable
	{
	public:
		// The String holding exactly text, or nullptr. hash is std::hash<std::string_view> of text.
		String* Find(std::string_view text, size_t hash) const
		{
			if (m_Slots.empty())
				return nullptr;
			const size_t mask = m_Slots.size() - 1;
			for (size_t i = hash & mask;; i = (i + 1) & mask)
			{
				String* slot = m_Slots[i];
				if (slot == nullptr)
					return nullptr;
				if (slot != Removed() && slot->Hash == hash && slot->Text == text)
					return slot;
			}
		}

		// Adds a String whose text isn't in the table yet.
		void Add(String* string)
		{
			if ((m_Used + 1) * 4 > m_Slots.size() * 3)
				Rehash();
			const size_t mask = m_Slots.size() - 1;
			size_t i = string->Hash & mask;
			while (m_Slots[i] != nullptr && m_Slots[i] != Removed())
				i = (i + 1) & mask;
			if (m_Slots[i] == nullptr)
				m_Used++;
			m_Slots[i] = string;
			m_Count++;
		}

		// Forgets every String that keep says no to (the garbage collector's unmarked texts).
		template<typename Keep>
		void RemoveUnless(Keep keep)
		{
			for (String*& slot : m_Slots)
			{
				if (slot != nullptr && slot != Removed() && !keep(slot))
				{
					slot = Removed();
					m_Count--;
				}
			}
		}

		size_t Count() const { return m_Count; }

	private:
		// A slot whose String was removed: lookups go on past it, and Add can reuse it.
		static String* Removed() { return reinterpret_cast<String*>(static_cast<uintptr_t>(1)); }

		// Grows when the live texts fill half the table, and otherwise just clears out removed slots.
		void Rehash()
		{
			size_t capacity = m_Slots.empty() ? 64 : m_Slots.size();
			while (m_Count * 2 >= capacity)
				capacity *= 2;
			std::vector<String*> old = std::move(m_Slots);
			m_Slots.assign(capacity, nullptr);
			m_Used = 0;
			m_Count = 0;
			for (String* string : old)
			{
				if (string != nullptr && string != Removed())
					Add(string);
			}
		}

		std::vector<String*> m_Slots;
		size_t m_Count = 0; // live Strings
		size_t m_Used = 0;  // slots not empty: live plus removed
	};
}
