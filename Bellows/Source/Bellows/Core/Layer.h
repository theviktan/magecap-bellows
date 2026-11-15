#pragma once

#include "Bellows/Core/Events.h"
#include "Bellows/Core/Timestep.h"

#include <string>

namespace Bellows {

	// A slice of application behaviour (game runtime, editor, debug overlay, ...).
	class Layer
	{
	public:
		explicit Layer(std::string name = "Layer")
			: m_DebugName(std::move(name)) {}
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep timestep) {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event& event) {}

		const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};

}
