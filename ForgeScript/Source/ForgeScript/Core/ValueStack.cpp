#include "ForgeScript/Core/ValueStack.h"

#include <algorithm>
#include <cstring>

namespace ForgeScript
{
	void ValueStack::Grow(size_t atLeast)
	{
		const size_t capacity = std::max<size_t>({ atLeast, m_Capacity * 2, 16 });
		std::unique_ptr<Value, FreeMemory> data(static_cast<Value*>(::operator new(capacity * sizeof(Value))));
		if (m_Size > 0)
			std::memcpy(static_cast<void*>(data.get()), m_Data.get(), m_Size * sizeof(Value));
		m_Data = std::move(data);
		m_Capacity = capacity;
	}
}
