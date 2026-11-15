#include "bwpch.h"
#include "Bellows/Core/LayerStack.h"

namespace Bellows {

	LayerStack::~LayerStack()
	{
		Clear();
	}

	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.emplace(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), layer);
		m_LayerInsertIndex++;
		layer->OnAttach();
	}

	void LayerStack::PushOverlay(Layer* overlay)
	{
		m_Layers.emplace_back(overlay);
		overlay->OnAttach();
	}

	void LayerStack::PopLayer(Layer* layer)
	{
		auto end = m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex);
		auto it = std::find(m_Layers.begin(), end, layer);
		if (it != end)
		{
			layer->OnDetach();
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}

	void LayerStack::PopOverlay(Layer* overlay)
	{
		auto it = std::find(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), m_Layers.end(), overlay);
		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}

	void LayerStack::Clear()
	{
		// Detach in reverse order so overlays go first.
		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
		{
			(*it)->OnDetach();
			delete *it;
		}
		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}

}
