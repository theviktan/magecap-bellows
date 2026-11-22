#include "bwpch.h"
#include "Bellows/Scripting/TweenProperties.h"

#include "Bellows/Scripting/ScriptApiDefinition.h"

#include <algorithm>
#include <type_traits>

namespace Bellows::Scripting {

	namespace {

		template<typename F>
		struct GetterResult : GetterResult<decltype(&F::operator())> {};
		template<typename C, typename R, typename A>
		struct GetterResult<R (C::*)(A) const> { using Type = std::decay_t<R>; };

		// The tweenable kinds of value, and how each reads and writes as a vec4.
		template<typename T> struct TweenValue { static constexpr bool Supported = false; };
		template<> struct TweenValue<float>
		{
			static constexpr bool Supported = true;
			static constexpr TweenValueType Type = TweenValueType::Number;
			static glm::vec4 ToVec4(float v) { return glm::vec4(v, 0.0f, 0.0f, 0.0f); }
			static float From(const glm::vec4& v) { return v.x; }
		};
		template<> struct TweenValue<double>
		{
			static constexpr bool Supported = true;
			static constexpr TweenValueType Type = TweenValueType::Number;
			static glm::vec4 ToVec4(double v) { return glm::vec4(static_cast<float>(v), 0.0f, 0.0f, 0.0f); }
			static double From(const glm::vec4& v) { return v.x; }
		};
		template<> struct TweenValue<glm::vec2>
		{
			static constexpr bool Supported = true;
			static constexpr TweenValueType Type = TweenValueType::Vec2;
			static glm::vec4 ToVec4(const glm::vec2& v) { return glm::vec4(v, 0.0f, 0.0f); }
			static glm::vec2 From(const glm::vec4& v) { return glm::vec2(v); }
		};
		template<> struct TweenValue<glm::vec3>
		{
			static constexpr bool Supported = true;
			static constexpr TweenValueType Type = TweenValueType::Vec3;
			static glm::vec4 ToVec4(const glm::vec3& v) { return glm::vec4(v, 0.0f); }
			static glm::vec3 From(const glm::vec4& v) { return glm::vec3(v); }
		};
		template<> struct TweenValue<glm::vec4>
		{
			static constexpr bool Supported = true;
			static constexpr TweenValueType Type = TweenValueType::Vec4;
			static glm::vec4 ToVec4(const glm::vec4& v) { return v; }
			static glm::vec4 From(const glm::vec4& v) { return v; }
		};

		template<> struct TweenValue<ColorValue>
		{
			static constexpr bool Supported = true;
			static constexpr TweenValueType Type = TweenValueType::Vec4;
			static glm::vec4 ToVec4(const ColorValue& v) { return v.Value; }
			static ColorValue From(const glm::vec4& v) { return ColorValue { v }; }
		};

		// The handle types tweens can act through: the entity itself, or one of its components.
		template<typename T> struct TweenTarget { static constexpr bool Supported = false; };
		template<> struct TweenTarget<ScriptEntity>
		{
			static constexpr bool Supported = true;
			static ScriptEntity Make(Entity entity) { return ScriptEntity(entity); }
		};
		template<typename C> struct TweenTarget<ComponentRef<C>>
		{
			static constexpr bool Supported = true;
			static ComponentRef<C> Make(Entity entity) { return ComponentRef<C> { ScriptEntity(entity) }; }
		};

		// Euler-angle properties, which turn as rotations rather than as three separate numbers.
		TweenRotation RotationOf(const std::string& name)
		{
			if (name == "rotation" || name == "Transform.rotation_degrees")
				return TweenRotation::Degrees;
			if (name == "Transform.rotation")
				return TweenRotation::Radians;
			return TweenRotation::None;
		}

		template<typename T>
		class TweenClass
		{
		public:
			TweenClass(std::vector<TweenProperty>& properties, std::string prefix) : m_Properties(properties), m_Prefix(std::move(prefix)) {}

			template<typename Getter, typename Setter>
			TweenClass& Property(const char* name, Getter getter, Setter setter, const char*)
			{
				using Value = typename GetterResult<Getter>::Type;
				if constexpr (TweenTarget<T>::Supported && TweenValue<Value>::Supported)
				{
					TweenProperty property;
					property.Name = m_Prefix + name;
					property.Type = TweenValue<Value>::Type;
					property.Rotation = RotationOf(property.Name);
					property.Get = [getter](Entity entity) { return TweenValue<Value>::ToVec4(getter(TweenTarget<T>::Make(entity))); };
					property.Set = [setter](Entity entity, const glm::vec4& value) { setter(TweenTarget<T>::Make(entity), TweenValue<Value>::From(value)); };
					m_Properties.push_back(std::move(property));
				}
				return *this;
			}

			template<typename Getter>
			TweenClass& ReadOnly(const char*, Getter, const char*) { return *this; }
			template<typename Callable, typename... Args>
			TweenClass& Method(const char*, Callable, const char*, const Args&...) { return *this; }
			template<typename Callable, typename... Args>
			TweenClass& Constructor(Callable, const Args&...) { return *this; }
			template<typename Callable, typename... Args>
			TweenClass& Static(const char*, Callable, const char*, const Args&...) { return *this; }

		private:
			std::vector<TweenProperty>& m_Properties;
			std::string m_Prefix;
		};

		class TweenPropertyApi
		{
		public:
			template<typename Callable, typename... Args>
			void Function(const char*, Callable, const char*, const Args&...) {}

			template<typename T>
			TweenClass<T> Class(const char* name, const char*)
			{
				// The entity's own properties go by their plain names; a component's by "Component.name".
				return TweenClass<T>(m_Properties, std::is_same_v<T, ScriptEntity> ? std::string() : std::string(name) + ".");
			}

			std::vector<TweenProperty> Take() { return std::move(m_Properties); }

		private:
			std::vector<TweenProperty> m_Properties;
		};

	}

	const std::vector<TweenProperty>& GetTweenProperties()
	{
		static const std::vector<TweenProperty> s_Properties = []
		{
			TweenPropertyApi api;
			DefineScriptApi(api);
			return api.Take();
		}();
		return s_Properties;
	}

	const TweenProperty& FindTweenProperty(const std::string& name)
	{
		const std::vector<TweenProperty>& properties = GetTweenProperties();
		const auto it = std::find_if(properties.begin(), properties.end(), [&](const TweenProperty& property) { return property.Name == name; });
		if (it != properties.end())
			return *it;
		std::string names;
		for (const TweenProperty& property : properties)
			names += (names.empty() ? "" : ", ") + property.Name;
		throw ScriptValueError("'" + name + "' can't be tweened. Tweens move number and vector properties: " + names);
	}

}
