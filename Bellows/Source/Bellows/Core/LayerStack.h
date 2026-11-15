#pragma once

#include "Bellows/Core/Layer.h"

#include <vector>

namespace Bellows {

	// Ordered collection of layers. Overlays always sit after regular layers.
	// The stack owns its layers and detaches/deletes them on destruction.
	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack();

		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);
		void Clear();

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }
		size_t Size() const { return m_Layers.size(); }
	private:
		std::vector<Layer*> m_Layers;
		size_t m_LayerInsertIndex = 0;
	};

}
