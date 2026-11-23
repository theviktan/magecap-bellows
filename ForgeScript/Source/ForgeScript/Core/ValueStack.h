#pragma once

#include "ForgeScript/Core/Value.h"

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>

// The VM's main loop is too big for compilers to inline even small calls into it on their own.
#if defined(_MSC_VER)
	#define FORGESCRIPT_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
	#define FORGESCRIPT_ALWAYS_INLINE inline __attribute__((always_inline))
#else
	#define FORGESCRIPT_ALWAYS_INLINE inline
#endif

namespace ForgeScript
{
	// The values a task is working on: its locals and the middle of every expression. It acts like
	// std::vector<Value> for the parts the VM uses, but adding and removing a value are written here,
	// small enough to be inlined into the VM's main loop. With std::vector, push_back was a function
	// call on nearly every instruction, about a sixth of a script's run time.
	static_assert(std::is_trivially_copyable_v<Value> && std::is_trivially_destructible_v<Value>,
		"ValueStack keeps Values in raw memory, which only works while a Value is plain data.");

	class ValueStack
	{
	public:
		using value_type = Value;
		using iterator = Value*;
		using const_iterator = const Value*;

		ValueStack() = default;
		ValueStack(const ValueStack&) = delete;
		ValueStack& operator=(const ValueStack&) = delete;

		size_t size() const { return m_Size; }
		size_t capacity() const { return m_Capacity; }
		bool empty() const { return m_Size == 0; }

		Value& operator[](size_t index) { assert(index < m_Size); return m_Data.get()[index]; }
		const Value& operator[](size_t index) const { assert(index < m_Size); return m_Data.get()[index]; }
		Value& back() { assert(m_Size > 0); return m_Data.get()[m_Size - 1]; }
		const Value& back() const { assert(m_Size > 0); return m_Data.get()[m_Size - 1]; }

		Value* begin() { return m_Data.get(); }
		Value* end() { return m_Data.get() + m_Size; }
		const Value* begin() const { return m_Data.get(); }
		const Value* end() const { return m_Data.get() + m_Size; }

		FORGESCRIPT_ALWAYS_INLINE void push_back(const Value& value)
		{
			if (m_Size == m_Capacity)
			{
				// Copy first: value may live in this stack, and growing moves it.
				const Value copy = value;
				Grow(m_Size + 1);
				m_Data.get()[m_Size++] = copy;
				return;
			}
			m_Data.get()[m_Size++] = value;
		}

		void emplace_back(const Value& value) { push_back(value); }
		void pop_back() { assert(m_Size > 0); m_Size--; }
		void clear() { m_Size = 0; }

		void reserve(size_t count)
		{
			if (count > m_Capacity)
				Grow(count);
		}

		// New places hold nothing, as with std::vector.
		void resize(size_t count)
		{
			if (count > m_Capacity)
				Grow(count);
			for (size_t i = m_Size; i < count; i++)
				m_Data.get()[i] = Value();
			m_Size = count;
		}

		// Adds [first, last) at the end; the VM only ever inserts there. The values must not come
		// from this stack.
		template<typename It>
		void insert(const Value* at, It first, It last)
		{
			assert(at == end());
			(void)at;
			const size_t count = static_cast<size_t>(std::distance(first, last));
			reserve(m_Size + count);
			for (; first != last; ++first)
				m_Data.get()[m_Size++] = *first;
		}

		template<typename It>
		void assign(It first, It last)
		{
			clear();
			insert(end(), first, last);
		}

	private:
		// At least doubles, so pushes stay cheap on average. Kept out of line: it is rare.
		void Grow(size_t atLeast);

		// Raw memory: a Value needs no constructing or destroying, and leaving the spare places
		// unwritten keeps starting a task (which reserves room for 256) as cheap as with std::vector.
		struct FreeMemory
		{
			void operator()(Value* data) const { ::operator delete(data); }
		};
		std::unique_ptr<Value, FreeMemory> m_Data;
		size_t m_Size = 0;
		size_t m_Capacity = 0;
	};
}
