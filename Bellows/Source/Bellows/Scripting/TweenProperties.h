#pragma once

#include "Bellows/Scene/Tweens.h"

#include <string>
#include <vector>

namespace Bellows::Scripting {

	// Every property a tween can move: the Entity's number and vector properties ("position") and
	// each component's ("UIImage.color"), found by walking the script API definition, so a property
	// added there can be tweened with nothing more to do.
	const std::vector<TweenProperty>& GetTweenProperties();

	// The property scripts name; throws ScriptValueError listing the names when there is none.
	const TweenProperty& FindTweenProperty(const std::string& name);

}
