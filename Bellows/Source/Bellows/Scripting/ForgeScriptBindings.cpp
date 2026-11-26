#include "bwpch.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"

#include "Bellows/Scripting/ScriptApiDefinition.h"

#include "ForgeScript/Core/Suggest.h"

#include <charconv>
#include <cmath>
#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace FS = ForgeScript;

namespace Bellows::Scripting {

	double ForgeScriptNumber(float value)
	{
		if (!std::isfinite(value))
			return static_cast<double>(value);
		char text[32];
		const std::to_chars_result written = std::to_chars(text, text + sizeof(text), value);
		double number = static_cast<double>(value);
		if (written.ec == std::errc())
			std::from_chars(text, written.ptr, number);
		return number;
	}

	namespace {

		// -------------------------------------------------------------------------
		// Native types
		// -------------------------------------------------------------------------

		// The ForgeScript types of the API classes. They describe C++ types, not one VM, so they are
		// shared by every VM and live until the program ends (after every VM that uses them).
		std::unordered_map<std::type_index, std::unique_ptr<FS::NativeType>>& NativeTypes()
		{
			static std::unordered_map<std::type_index, std::unique_ptr<FS::NativeType>> s_Types;
			return s_Types;
		}

		template<typename T>
		const FS::NativeType& TypeFor()
		{
			auto it = NativeTypes().find(std::type_index(typeid(T)));
			if (it == NativeTypes().end())
				throw std::logic_error(std::string("The script API type '") + typeid(T).name() + "' is not registered for ForgeScript");
			return *it->second;
		}

		std::string Describe(FS::VM& vm, const FS::Value& value)
		{
			return value.IsNothing() ? std::string("nothing") : vm.TypeOf(value);
		}

		[[noreturn]] void Mismatch(FS::VM& vm, const std::string& what, const std::string& expected, const FS::Value& value)
		{
			throw FS::RuntimeError(what + " needs " + expected + ", but got " + Describe(vm, value) + ".");
		}

		// Turns engine errors into ForgeScript errors so scripts can catch them and see the message.
		template<typename Callable>
		auto Guard(Callable&& function) -> decltype(function())
		{
			try
			{
				return function();
			}
			catch (const FS::RuntimeError&)
			{
				throw;
			}
			catch (const std::exception& error)
			{
				throw FS::RuntimeError(error.what());
			}
		}

		// A vector or color must hold ordinary numbers (1e308 * 10 is infinity, and 1e39 becomes one as a float).
		template<typename Vector>
		Vector Finite(const Vector& vector, const std::string& what)
		{
			if (!HasOnlyFiniteNumbers(vector))
				throw FS::RuntimeError(what + " needs ordinary numbers, not NaN or infinity.");
			return vector;
		}

		std::vector<double> NumberList(FS::VM& vm, const FS::Value& value, const std::string& what, size_t minimum, size_t maximum)
		{
			if (!FS::IsObjectType(value, FS::ObjectType::List))
				Mismatch(vm, what, "a list of numbers", value);
			const std::vector<FS::Value>& items = FS::As<FS::List>(value)->Items;
			if (items.size() < minimum || items.size() > maximum)
			{
				throw FS::RuntimeError(what + " needs a list of " + (minimum == maximum ? std::to_string(minimum) : std::to_string(minimum) + " or " + std::to_string(maximum)) +
					" numbers, but the list has " + std::to_string(items.size()) + ".");
			}
			std::vector<double> numbers;
			for (const FS::Value& item : items)
			{
				if (!item.IsNumber())
					Mismatch(vm, what, "a list of numbers", value);
				numbers.push_back(item.AsNumber());
			}
			return numbers;
		}

		FS::Value MakeNumberList(FS::VM& vm, std::initializer_list<double> numbers)
		{
			std::vector<FS::Value> items;
			for (double number : numbers)
				items.push_back(FS::Value::FromNumber(number));
			return vm.MakeList(std::move(items));
		}

		// -------------------------------------------------------------------------
		// Converting between C++ values and ForgeScript values
		// -------------------------------------------------------------------------

		// API classes (entities, components, materials, raycast hits) become native objects that own a copy.
		template<typename T>
		struct Convert
		{
			static FS::Value To(FS::VM& vm, const T& value) { return vm.MakeNative(TypeFor<T>(), new T(value)); }

			static T& Ref(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				const FS::NativeType& type = TypeFor<T>();
				if (!FS::IsObjectType(value, FS::ObjectType::NativeObject) || FS::As<FS::NativeObject>(value)->Type != &type)
					Mismatch(vm, what, "a " + type.Name, value);
				return *static_cast<T*>(FS::As<FS::NativeObject>(value)->Data);
			}

			static T From(FS::VM& vm, const FS::Value& value, const std::string& what) { return Ref(vm, value, what); }
		};

		template<>
		struct Convert<bool>
		{
			static FS::Value To(FS::VM&, bool value) { return FS::Value::FromBool(value); }
			static bool From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (!value.IsBool())
					Mismatch(vm, what, "true or false", value);
				return value.AsBool();
			}
		};

		template<typename T>
			requires std::is_arithmetic_v<T> && (!std::is_same_v<T, bool>) && (!std::is_same_v<T, uint64_t>)
		struct Convert<T>
		{
			static FS::Value To(FS::VM&, T value)
			{
				if constexpr (std::is_same_v<T, float>)
					return FS::Value::FromNumber(ForgeScriptNumber(value));
				else
					return FS::Value::FromNumber(static_cast<double>(value));
			}
			static T From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (!value.IsNumber())
					Mismatch(vm, what, "a number", value);
				const double number = value.AsNumber();
				if constexpr (std::is_integral_v<T>)
				{
					if (number != std::floor(number))
						throw FS::RuntimeError(what + " needs a whole number, but got " + FS::FormatNumber(number) + ".");
				}
				return static_cast<T>(number);
			}
		};

		// 64-bit ids are text: a ForgeScript number cannot hold every 64-bit value exactly.
		template<>
		struct Convert<uint64_t>
		{
			static FS::Value To(FS::VM& vm, uint64_t value) { return vm.MakeText(std::to_string(value)); }
			static uint64_t From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::String))
				{
					const std::string& text = FS::As<FS::String>(value)->Text;
					char* end = nullptr;
					const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
					if (text.empty() || *end != '\0')
						throw FS::RuntimeError(what + " needs an id, but '" + text + "' is not one.");
					return static_cast<uint64_t>(parsed);
				}
				if (value.IsNumber() && value.AsNumber() >= 0 && value.AsNumber() == std::floor(value.AsNumber()))
					return static_cast<uint64_t>(value.AsNumber());
				Mismatch(vm, what, "an id", value);
			}
		};

		template<>
		struct Convert<std::string>
		{
			static FS::Value To(FS::VM& vm, const std::string& value) { return vm.MakeText(value); }
			static std::string From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (!FS::IsObjectType(value, FS::ObjectType::String))
					Mismatch(vm, what, "text", value);
				return FS::As<FS::String>(value)->Text;
			}
		};

		template<>
		struct Convert<glm::vec3>
		{
			static FS::Value To(FS::VM& vm, const glm::vec3& value) { return vm.MakeVector(ForgeScriptNumber(value.x), ForgeScriptNumber(value.y), ForgeScriptNumber(value.z)); }
			static glm::vec3 From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::Vector))
				{
					const FS::Vector* v = FS::As<FS::Vector>(value);
					return Finite(glm::vec3(static_cast<float>(v->X), static_cast<float>(v->Y), static_cast<float>(v->Z)), what);
				}
				if (FS::IsObjectType(value, FS::ObjectType::List))
				{
					std::vector<double> n = NumberList(vm, value, what, 3, 3);
					return Finite(glm::vec3(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])), what);
				}
				Mismatch(vm, what, "a vector", value);
			}
		};

		template<>
		struct Convert<glm::vec2>
		{
			static FS::Value To(FS::VM& vm, const glm::vec2& value) { return vm.MakeVector(ForgeScriptNumber(value.x), ForgeScriptNumber(value.y), 0.0); }
			static glm::vec2 From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::Vector))
				{
					const FS::Vector* v = FS::As<FS::Vector>(value);
					return Finite(glm::vec2(static_cast<float>(v->X), static_cast<float>(v->Y)), what);
				}
				if (FS::IsObjectType(value, FS::ObjectType::List))
				{
					std::vector<double> n = NumberList(vm, value, what, 2, 2);
					return Finite(glm::vec2(static_cast<float>(n[0]), static_cast<float>(n[1])), what);
				}
				Mismatch(vm, what, "a vector", value);
			}
		};

		template<>
		struct Convert<glm::quat>
		{
			static FS::Value To(FS::VM& vm, const glm::quat& q)
			{
				return MakeNumberList(vm, { ForgeScriptNumber(q.w), ForgeScriptNumber(q.x), ForgeScriptNumber(q.y), ForgeScriptNumber(q.z) });
			}
			static glm::quat From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				std::vector<double> n = NumberList(vm, value, what, 4, 4);
				return glm::quat(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]), static_cast<float>(n[3]));
			}
		};

		template<>
		struct Convert<ColorValue>
		{
			static FS::Value To(FS::VM& vm, const ColorValue& color)
			{
				const glm::vec4& c = color.Value;
				return MakeNumberList(vm, { ForgeScriptNumber(c.r), ForgeScriptNumber(c.g), ForgeScriptNumber(c.b), ForgeScriptNumber(c.a) });
			}
			static ColorValue From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::Vector))
					return ColorValue { glm::vec4(Convert<glm::vec3>::From(vm, value, what), 1.0f) };
				if (!FS::IsObjectType(value, FS::ObjectType::List))
					Mismatch(vm, what, "a color like [1, 0.5, 0]", value);
				std::vector<double> n = NumberList(vm, value, what, 3, 4);
				return ColorValue { Finite(glm::vec4(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]), n.size() > 3 ? static_cast<float>(n[3]) : 1.0f), what) };
			}
		};

		template<>
		struct Convert<KeyArg>
		{
			static FS::Value To(FS::VM& vm, const KeyArg& key)
			{
				for (const KeyName& name : GetKeyNames())
				{
					if (name.Code == key.Code)
						return vm.MakeText(name.Name);
				}
				return FS::Value::FromNumber(static_cast<double>(key.Code));
			}
			static KeyArg From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::String))
					return KeyArg { KeyFromName(FS::As<FS::String>(value)->Text) };
				if (value.IsNumber())
					return KeyArg { static_cast<KeyCode>(Convert<int>::From(vm, value, what)) };
				Mismatch(vm, what, "a key name like \"W\" or \"space\"", value);
			}
		};

		template<>
		struct Convert<MouseButtonArg>
		{
			static FS::Value To(FS::VM& vm, const MouseButtonArg& button)
			{
				switch (button.Button)
				{
					case MouseButton::Left:   return vm.MakeText("left");
					case MouseButton::Right:  return vm.MakeText("right");
					case MouseButton::Middle: return vm.MakeText("middle");
					default:                  return FS::Value::FromNumber(static_cast<double>(button.Button));
				}
			}
			static MouseButtonArg From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::String))
					return MouseButtonArg { MouseButtonFromName(FS::As<FS::String>(value)->Text) };
				if (value.IsNumber())
					return MouseButtonArg { static_cast<MouseButton>(Convert<int>::From(vm, value, what)) };
				Mismatch(vm, what, "\"left\", \"right\" or \"middle\"", value);
			}
		};

		template<>
		struct Convert<ComponentName>
		{
			static FS::Value To(FS::VM& vm, const ComponentName& component) { return vm.MakeText(component.Name); }
			static ComponentName From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				return ComponentName { Convert<std::string>::From(vm, value, what) };
			}
		};

		template<typename T>
		struct Convert<std::optional<T>>
		{
			static FS::Value To(FS::VM& vm, const std::optional<T>& value) { return value ? Convert<T>::To(vm, *value) : FS::Value(); }
			static std::optional<T> From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (value.IsNothing())
					return std::nullopt;
				return Convert<T>::From(vm, value, what);
			}
		};

		template<typename T>
		struct Convert<std::vector<T>>
		{
			static FS::Value To(FS::VM& vm, const std::vector<T>& values)
			{
				// Each item is created before the list holds it; the garbage collector only runs between
				// script instructions, so the items are safe until the list is made.
				std::vector<FS::Value> items;
				items.reserve(values.size());
				for (const T& value : values)
					items.push_back(Convert<T>::To(vm, value));
				return vm.MakeList(std::move(items));
			}
			static std::vector<T> From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (!FS::IsObjectType(value, FS::ObjectType::List))
					Mismatch(vm, what, "a list", value);
				std::vector<T> result;
				for (const FS::Value& item : FS::As<FS::List>(value)->Items)
					result.push_back(Convert<T>::From(vm, item, what));
				return result;
			}
		};

		template<typename A, typename B>
		struct Convert<std::pair<A, B>>
		{
			static FS::Value To(FS::VM& vm, const std::pair<A, B>& value)
			{
				FS::Value first = Convert<A>::To(vm, value.first);
				FS::Value second = Convert<B>::To(vm, value.second);
				return vm.MakeList({ first, second });
			}
		};

		template<typename... Ts>
		struct Convert<std::variant<Ts...>>
		{
			static FS::Value To(FS::VM& vm, const std::variant<Ts...>& value)
			{
				return std::visit([&vm](const auto& alternative) { return Convert<std::decay_t<decltype(alternative)>>::To(vm, alternative); }, value);
			}

			static std::variant<Ts...> From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				std::optional<std::variant<Ts...>> result;
				(TryAlternative<Ts>(vm, value, what, result) || ...);
				if (!result)
					Mismatch(vm, what, "a different kind of value", value);
				return *result;
			}

		private:
			template<typename T>
			static bool TryAlternative(FS::VM& vm, const FS::Value& value, const std::string& what, std::optional<std::variant<Ts...>>& result)
			{
				try
				{
					result = Convert<T>::From(vm, value, what);
					return true;
				}
				catch (const FS::RuntimeError&)
				{
					return false;
				}
			}
		};

		// Any value another language can read (save_data's value): nothing, true/false, numbers, text,
		// vectors, entities, and lists and maps of these.
		template<>
		struct Convert<ScriptValue>
		{
			static FS::Value To(FS::VM& vm, const ScriptValue& value) { return FromScriptValue(vm, value); }
			static ScriptValue From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				std::vector<std::string> unsendable;
				ScriptValue result = ToScriptValue(vm, value, unsendable);
				if (!unsendable.empty())
				{
					throw FS::RuntimeError(what + " can hold nothing, true/false, numbers, text, vectors, entities, and lists and maps of these, not " +
						unsendable.front() + ".");
				}
				return result;
			}
		};

		// -------------------------------------------------------------------------
		// Wrapping C++ functions
		// -------------------------------------------------------------------------

		template<typename F>
		struct Signature : Signature<decltype(&F::operator())> {};

		template<typename C, typename R, typename... A>
		struct Signature<R (C::*)(A...) const>
		{
			using Params = std::tuple<A...>;
		};

		template<typename C, typename R, typename... A>
		struct Signature<R (C::*)(A...)>
		{
			using Params = std::tuple<A...>;
		};

		template<typename R, typename... A>
		struct Signature<R (*)(A...)>
		{
			using Params = std::tuple<A...>;
		};

		template<typename Spec>
		constexpr bool IsRequired = std::is_same_v<Spec, ArgName>;

		template<typename... Specs>
		const char* ArgumentName(const std::tuple<Specs...>& specs, size_t index)
		{
			const char* name = nullptr;
			size_t current = 0;
			std::apply([&](const auto&... spec) { ((current++ == index ? (name = spec.Name, 0) : 0), ...); }, specs);
			return name;
		}

		template<typename... Specs>
		std::string ArgumentLabel(const std::string& function, const std::tuple<Specs...>& specs, size_t index)
		{
			const char* name = ArgumentName(specs, index);
			return "'" + function + "' " + (name ? std::string("value '") + name + "'" : "value " + std::to_string(index + 1));
		}

		// Converts argument 'Index' of a call, using the default when the script left it out.
		template<typename Param, size_t Index, typename... Specs>
		std::decay_t<Param> ArgumentAt(FS::CallContext& context, const std::tuple<Specs...>& specs, const std::string& function)
		{
			using Decayed = std::decay_t<Param>;
			if (Index < context.Count())
				return Convert<Decayed>::From(context.GetVM(), context.Arg(Index), ArgumentLabel(function, specs, Index));
			if constexpr (Index < sizeof...(Specs))
			{
				using Spec = std::tuple_element_t<Index, std::tuple<Specs...>>;
				if constexpr (!IsRequired<Spec>)
					return Decayed(std::get<Index>(specs).Default);
			}
			throw FS::RuntimeError("'" + function + "' needs a value for " + ArgumentLabel(function, specs, Index) + ".");
		}

		template<size_t Total, typename... Specs>
		void CheckCount(const FS::CallContext& context, const std::string& function)
		{
			constexpr size_t required = sizeof...(Specs) == 0 ? Total : (0 + ... + (IsRequired<Specs> ? 1 : 0));
			if (context.Count() < required || context.Count() > Total)
			{
				const std::string expected = required == Total ? std::to_string(Total) : std::to_string(required) + " to " + std::to_string(Total);
				throw FS::RuntimeError("'" + function + "' needs " + expected + (Total == 1 ? " value" : " values") +
					", but was given " + std::to_string(context.Count()) + ".");
			}
		}

		template<typename Result>
		FS::Value ReturnValue(FS::VM& vm, Result&& result)
		{
			return Convert<std::decay_t<Result>>::To(vm, result);
		}

		template<typename Params, typename Callable, typename... Specs, size_t... I>
		FS::Value InvokeFunction(FS::CallContext& context, const Callable& function, const std::tuple<Specs...>& specs, const std::string& name, std::index_sequence<I...>)
		{
			using Result = decltype(function(std::declval<std::tuple_element_t<I, Params>>()...));
			if constexpr (std::is_void_v<Result>)
			{
				function(ArgumentAt<std::tuple_element_t<I, Params>, I>(context, specs, name)...);
				return FS::Value();
			}
			else
			{
				return ReturnValue(context.GetVM(), function(ArgumentAt<std::tuple_element_t<I, Params>, I>(context, specs, name)...));
			}
		}

		// For methods, parameter 0 is the object ('self'); script arguments fill the rest.
		template<typename Self, typename Params, typename Callable, typename... Specs, size_t... I>
		FS::Value InvokeMethod(FS::CallContext& context, const Callable& function, const std::tuple<Specs...>& specs, const std::string& name, std::index_sequence<I...>)
		{
			Self& self = Convert<Self>::Ref(context.GetVM(), context.Self(), "'" + name + "'");
			using Result = decltype(function(self, std::declval<std::tuple_element_t<I + 1, Params>>()...));
			if constexpr (std::is_void_v<Result>)
			{
				function(self, ArgumentAt<std::tuple_element_t<I + 1, Params>, I>(context, specs, name)...);
				return FS::Value();
			}
			else
			{
				return ReturnValue(context.GetVM(), function(self, ArgumentAt<std::tuple_element_t<I + 1, Params>, I>(context, specs, name)...));
			}
		}

		template<typename Callable, typename... Specs>
		FS::NativeFn WrapFunction(const std::string& name, Callable function, std::tuple<Specs...> specs)
		{
			using Params = typename Signature<Callable>::Params;
			constexpr size_t total = std::tuple_size_v<Params>;
			static_assert(sizeof...(Specs) == 0 || sizeof...(Specs) == total, "Describe every argument or none");
			return [name, function, specs](FS::CallContext& context)
			{
				CheckCount<total, Specs...>(context, name);
				return Guard([&] { return InvokeFunction<Params>(context, function, specs, name, std::make_index_sequence<total>()); });
			};
		}

		template<typename Self, typename Callable, typename... Specs>
		FS::NativeType::MethodInfo WrapMethod(const std::string& name, Callable function, std::tuple<Specs...> specs)
		{
			using Params = typename Signature<Callable>::Params;
			constexpr size_t total = std::tuple_size_v<Params> - 1;
			static_assert(sizeof...(Specs) == 0 || sizeof...(Specs) == total, "Describe every argument or none");
			FS::NativeType::MethodInfo info;
			info.Arity = FS::AnyArity;
			info.Fn = [name, function, specs](FS::CallContext& context)
			{
				CheckCount<total, Specs...>(context, name);
				return Guard([&] { return InvokeMethod<Self, Params>(context, function, specs, name, std::make_index_sequence<total>()); });
			};
			return info;
		}

		// -------------------------------------------------------------------------
		// Identity and printing of API objects
		// -------------------------------------------------------------------------

		inline std::pair<uint64_t, uint64_t> IdentityOf(const ScriptEntity& entity) { return { entity.SceneID, static_cast<uint64_t>(entity.ID) }; }

		template<typename C>
		std::pair<uint64_t, uint64_t> IdentityOf(const ComponentRef<C>& component) { return IdentityOf(component.Owner); }

		inline std::pair<uint64_t, uint64_t> IdentityOf(const ScriptMaterial& material)
		{
			return { static_cast<uint64_t>(reinterpret_cast<uintptr_t>(material.Instance.get())), 0 };
		}

		inline std::pair<uint64_t, uint64_t> IdentityOf(const ScriptGroup& group) { return { group.SceneID, static_cast<uint64_t>(group.ID) }; }

		template<typename T>
		concept HasIdentity = requires(const T& value) { IdentityOf(value); };

		std::string EntityText(const ScriptEntity& entity)
		{
			Entity resolved = entity.TryResolve();
			return resolved ? "Entity('" + resolved.GetName() + "')" : std::string("Entity(<destroyed>)");
		}

		inline std::string TextOf(const ScriptEntity& entity, const std::string&) { return EntityText(entity); }
		inline std::string TextOf(const ScriptGroup& group, const std::string&)
		{
			Scene* scene = Scene::GetSceneByID(group.SceneID);
			const SceneGroup* found = scene ? scene->FindGroup(group.ID) : nullptr;
			return found ? "Group('" + found->Name + "')" : std::string("Group(<deleted>)");
		}

		template<typename C>
		std::string TextOf(const ComponentRef<C>& component, const std::string& typeName) { return typeName + " of " + EntityText(component.Owner); }

		inline std::string TextOf(const ScriptMaterial& material, const std::string&) { return "Material('" + material.Instance->GetName() + "')"; }
		inline std::string TextOf(const RaycastHit& hit, const std::string&) { return fmt::format("RaycastHit(distance={:.3f})", hit.Distance); }

		// -------------------------------------------------------------------------
		// The ForgeScript binder
		// -------------------------------------------------------------------------

		template<typename T>
		class ForgeScriptClass
		{
		public:
			ForgeScriptClass(FS::VM& vm, FS::NativeType& type) : m_VM(vm), m_Type(type) {}

			template<typename Getter, typename Setter>
			ForgeScriptClass& Property(const char* name, Getter getter, Setter setter, const char*)
			{
				ReadOnly(name, getter, nullptr);
				using Value = std::decay_t<std::tuple_element_t<1, typename Signature<Setter>::Params>>;
				const std::string what = "'" + std::string(name) + "'";
				m_Type.Setters[name] = [setter, what](FS::VM& vm, void* data, const FS::Value& value)
				{
					Guard([&] { setter(*static_cast<T*>(data), Convert<Value>::From(vm, value, what)); });
				};
				return *this;
			}

			template<typename Getter>
			ForgeScriptClass& ReadOnly(const char* name, Getter getter, const char*)
			{
				m_Type.Getters[name] = [getter](FS::VM& vm, void* data)
				{
					return Guard([&] { return ReturnValue(vm, getter(*static_cast<T*>(data))); });
				};
				return *this;
			}

			template<typename Callable, typename... Args>
			ForgeScriptClass& Method(const char* name, Callable function, const char*, const Args&... args)
			{
				m_Type.Methods[name] = WrapMethod<T>(name, function, std::make_tuple(args...));
				return *this;
			}

			// The class name becomes a function that makes new objects: Material("Gold", [1, 0.8, 0]).
			template<typename Callable, typename... Args>
			ForgeScriptClass& Constructor(Callable function, const Args&... args)
			{
				m_Constructor = m_VM.MakeFunction(m_Type.Name, FS::AnyArity, WrapFunction(m_Type.Name, function, std::make_tuple(args...)));
				m_VM.SetSharedGlobal(m_Type.Name, m_Constructor);
				return *this;
			}

			// Static functions hang off the class name: Material.load("Materials/Gold.bmat").
			template<typename Callable, typename... Args>
			ForgeScriptClass& Static(const char* name, Callable function, const char*, const Args&... args)
			{
				if (m_Constructor.IsNothing())
					throw std::logic_error("ForgeScript static functions need a constructor first (" + m_Type.Name + "." + name + ")");
				const std::string fullName = m_Type.Name + "." + name;
				m_VM.SetFunctionMember(m_Constructor, name, m_VM.MakeFunction(fullName, FS::AnyArity, WrapFunction(fullName, function, std::make_tuple(args...))));
				return *this;
			}

		private:
			FS::VM& m_VM;
			FS::NativeType& m_Type;
			FS::Value m_Constructor;
		};

		class ForgeScriptApi
		{
		public:
			explicit ForgeScriptApi(FS::VM& vm) : m_VM(vm) {}

			template<typename Callable, typename... Args>
			void Function(const char* name, Callable function, const char*, const Args&... args)
			{
				m_VM.RegisterFunction(name, FS::AnyArity, WrapFunction(name, function, std::make_tuple(args...)));
			}

			template<typename T>
			ForgeScriptClass<T> Class(const char* name, const char*)
			{
				std::unique_ptr<FS::NativeType>& slot = NativeTypes()[std::type_index(typeid(T))];
				if (!slot)
				{
					slot = std::make_unique<FS::NativeType>();
					FS::NativeType& type = *slot;
					type.Name = name;
					type.Finalizer = [](void* data) { delete static_cast<T*>(data); };
					const std::string typeName = name;
					type.ToText = [typeName](void* data)
					{
						if constexpr (requires(const T& value) { TextOf(value, typeName); })
							return TextOf(*static_cast<T*>(data), typeName);
						else
							return "<" + typeName + ">";
					};
					if constexpr (HasIdentity<T>)
					{
						type.Equals = [](void* a, void* b) { return IdentityOf(*static_cast<T*>(a)) == IdentityOf(*static_cast<T*>(b)); };
						type.Hash = [](void* data)
						{
							const auto identity = IdentityOf(*static_cast<T*>(data));
							return std::hash<uint64_t>()(identity.first) ^ (std::hash<uint64_t>()(identity.second) << 1);
						};
					}
				}
				return ForgeScriptClass<T>(m_VM, *slot);
			}

		private:
			FS::VM& m_VM;
		};

	}

	namespace {

		struct EnginePhrase
		{
			ForgeScriptPhraseInfo Info;
			FS::NativeFn Function;
		};

		// The entity a phrase is about ('example' shows the phrase written right).
		ScriptEntity PhraseEntityAt(FS::CallContext& context, size_t index, const char* phrase, const char* what, const std::string& example)
		{
			FS::VM& vm = context.GetVM();
			const FS::Value value = context.Arg(index);
			if (!FS::IsObjectType(value, FS::ObjectType::NativeObject) || FS::As<FS::NativeObject>(value)->Type != &TypeFor<ScriptEntity>())
			{
				throw FS::RuntimeError(std::string("'") + phrase + "' needs " + what + ", like: " + example + ". This is " +
					(value.IsNothing() ? std::string("nothing") : "a " + vm.TypeOf(value)) + ".");
			}
			return Convert<ScriptEntity>::From(vm, value, std::string("'") + phrase + "'");
		}

		ScriptEntity PhraseEntity(FS::CallContext& context, const char* phrase, const char* what, const std::string& example)
		{
			return PhraseEntityAt(context, 0, phrase, what, example);
		}

		// The Hierarchy group a phrase names (argument 'index'), found by name in any letter case. A name
		// with no group is an error that names the closest one: 'put' never makes a group by itself, so a
		// typo can't quietly start a new one.
		ScriptGroup PhraseGroup(FS::CallContext& context, size_t index)
		{
			const std::string name = context.Text(index);
			return Guard([&]
			{
				Scene& scene = RequireContextScene();
				if (const SceneGroup* group = scene.FindGroup(name))
					return Detail::MakeScriptGroup(scene, *group);
				std::vector<std::string_view> names;
				for (const SceneGroup& group : scene.GetGroups())
					names.push_back(group.Name);
				std::string message = "There's no group called '" + name + "'.";
				const std::string closest = FS::DidYouMean(name, names);
				if (!closest.empty())
					message += closest;
				else
					message += " Make one in the Hierarchy, or with create_group(\"" + name + "\").";
				throw FS::RuntimeError(message);
			});
		}

		// The Animator of the entity a phrase names (argument 'index').
		AnimatorRef PhraseAnimator(FS::CallContext& context, size_t index, const char* phrase, const std::string& example)
		{
			FS::VM& vm = context.GetVM();
			const FS::Value value = context.Arg(index);
			if (!FS::IsObjectType(value, FS::ObjectType::NativeObject) || FS::As<FS::NativeObject>(value)->Type != &TypeFor<ScriptEntity>())
			{
				throw FS::RuntimeError(std::string("'") + phrase + "' needs an entity, like: " + example + ". This is " +
					(value.IsNothing() ? std::string("nothing") : "a " + vm.TypeOf(value)) + ".");
			}
			const ScriptEntity entity = Convert<ScriptEntity>::From(vm, value, std::string("'") + phrase + "'");
			Entity resolved = Guard([&] { return entity.Resolve(); });
			if (!resolved.HasComponent<AnimatorComponent>())
				throw FS::RuntimeError(std::string("'") + phrase + "' needs an entity with an Animator, and '" + resolved.GetName() + "' has none.");
			return AnimatorRef { entity };
		}

		// The NavAgent of the entity a phrase names (argument 0).
		NavAgentRef PhraseNavAgent(FS::CallContext& context, const char* phrase, const std::string& example)
		{
			const ScriptEntity entity = PhraseEntity(context, phrase, "an entity", example);
			Entity resolved = Guard([&] { return entity.Resolve(); });
			if (!resolved.HasComponent<NavAgentComponent>())
				throw FS::RuntimeError(std::string("'") + phrase + "' needs an entity with a NavAgent, and '" + resolved.GetName() + "' has none.");
			return NavAgentRef { entity };
		}

		// The widget a UI phrase is about.
		ScriptEntity PhraseWidget(FS::CallContext& context, const char* phrase)
		{
			return PhraseEntity(context, phrase, "a UI entity", std::string(phrase) + " find_entity(\"Pause Menu\")");
		}

		// show / hide / enable / disable (widget): the everyday UI properties, as phrases.
		FS::NativeFn SetUIFlag(const char* phrase, bool interactable, bool value)
		{
			return [phrase, interactable, value](FS::CallContext& context)
			{
				const ScriptEntity widget = PhraseWidget(context, phrase);
				Guard([&]
				{
					if (interactable)
						Detail::RequireUIComponent<UIRectComponent>(widget, "interactable", "UIRect").Interactable = value;
					else
						Detail::RequireUIVisible(widget) = value;
				});
				return FS::Value();
			};
		}

		// What a tween phrase's value means.
		enum class TweenPhraseValue
		{
			AsGiven,		// A vector, as tween() takes it
			Size,			// A number for all three axes, or a vector
			Opacity			// 0 to 1: the alpha of the entity's color
		};

		struct TweenPhrase
		{
			const char* Verb;		// "move"
			const char* Link;		// "to" or "by"
			const char* Value;		// The value's name in the pattern
			const char* Property;	// The tween property, or nullptr for fade (its color property)
			TweenPhraseValue Kind;
			const char* Doc;		// What it does, before "; same as"
			const char* Same;		// The tween() call it is
		};

		const TweenPhrase c_TweenPhrases[] = {
			{ "move", "to", "position", "position", TweenPhraseValue::AsGiven, "Moves an entity smoothly to a position", "tween(entity, \"position\", position, seconds)" },
			{ "move", "by", "offset", "position", TweenPhraseValue::AsGiven, "Moves an entity smoothly by an offset", "tween_by(entity, \"position\", offset, seconds)" },
			{ "turn", "to", "angles", "rotation", TweenPhraseValue::AsGiven, "Turns an entity smoothly to rotation angles in degrees", "tween(entity, \"rotation\", angles, seconds)" },
			{ "turn", "by", "angles", "rotation", TweenPhraseValue::AsGiven, "Turns an entity smoothly by angles in degrees, more than one turn too", "tween_by(entity, \"rotation\", angles, seconds)" },
			{ "scale", "to", "size", "scale", TweenPhraseValue::Size, "Scales an entity smoothly to a size: a number for all three axes, or a vector", "tween(entity, \"scale\", size, seconds)" },
			{ "fade", "to", "opacity", nullptr, TweenPhraseValue::Opacity, "Fades a UI image (or, without one, a text) smoothly to an opacity from 0 to 1, keeping its color", "tween(entity, \"UIImage.color\", color, seconds)" },
		};

		// The color 'fade' changes: the entity's UIImage, or its Text.
		const TweenProperty& FadeProperty(const ScriptEntity& target)
		{
			Entity entity = target.Resolve();
			if (entity.HasComponent<UIImageComponent>())
				return FindTweenProperty("UIImage.color");
			if (entity.HasComponent<TextComponent>())
				return FindTweenProperty("Text.color");
			throw ScriptValueError("'fade' needs an entity with a UIImage or a Text, and '" + entity.GetName() + "' has neither");
		}

		// move / turn / scale / fade (entity) to|by (value) over (seconds) seconds [with (style)]: starts the
		// tween its tween() call would, with the same checks and messages, and gives back the Tween.
		FS::NativeFn TweenPhraseFunction(const TweenPhrase& phrase, bool withStyle)
		{
			return [&phrase, withStyle](FS::CallContext& context)
			{
				FS::VM& vm = context.GetVM();
				const std::string example = std::string(phrase.Verb) + " self " + phrase.Link + " " +
					(phrase.Kind == TweenPhraseValue::Opacity ? "0.5" : phrase.Kind == TweenPhraseValue::Size ? "2" : "vector(0, 1, 0)") + " over 1 second";
				const ScriptEntity target = PhraseEntity(context, phrase.Verb, "an entity", example);
				const std::string what = std::string("'") + phrase.Verb + "'";
				const FS::Value given = context.Arg(1);
				const double seconds = context.Number(2);
				const std::string ease = withStyle ? context.Text(3) : std::string("in out");
				return ReturnValue(vm, Guard([&]
				{
					ScriptValue value;
					std::string property = phrase.Property ? phrase.Property : "";
					switch (phrase.Kind)
					{
						case TweenPhraseValue::AsGiven:
							value = Convert<ScriptValue>::From(vm, given, what);
							break;
						case TweenPhraseValue::Size:
							if (given.IsNumber())
								value = glm::vec3(static_cast<float>(given.AsNumber()));
							else
								value = Convert<ScriptValue>::From(vm, given, what);
							break;
						case TweenPhraseValue::Opacity:
						{
							if (!given.IsNumber() || !std::isfinite(given.AsNumber()) || given.AsNumber() < 0.0 || given.AsNumber() > 1.0)
								throw ScriptValueError("'fade' needs an opacity from 0 (clear) to 1 (solid), like: fade self to 0 over 1 second");
							const TweenProperty& color = FadeProperty(target);
							glm::vec4 rgba = color.Get(target.Resolve());
							rgba.a = static_cast<float>(given.AsNumber());
							value = ScriptValueList { double(rgba.r), double(rgba.g), double(rgba.b), double(rgba.a) };
							property = color.Name;
							break;
						}
					}
					return Detail::StartTween(target, property, value, seconds, ease, 0.0, std::string_view(phrase.Link) == "by");
				}));
			};
		}

		FS::NativeFn ShakeCameraPhrase()
		{
			return [](FS::CallContext& context)
			{
				const double strength = context.Number(0);
				const double seconds = context.Number(1);
				Guard([&] { Detail::ShakeCamera(strength, seconds); });
				return FS::Value();
			};
		}

		// The phrases are thin wrappers over functions of the shared API, so both spellings behave alike.
		const std::vector<EnginePhrase>& GetEnginePhrases()
		{
			static const std::vector<EnginePhrase> s_Phrases = [] {
			std::vector<EnginePhrase> phrases = {
				{ { "save screenshot", "Saves what the player sees and gives back the file's full path; same as save_screenshot()", "str" },
					[](FS::CallContext& context) { return context.GetVM().MakeText(Guard([] { return SaveScreenshot("", 1920); })); } },
				{ { "save screenshot to (path)", "Saves what the player sees to path (inside the user folder when relative) and gives back the full path; same as save_screenshot(path)", "str" },
					[](FS::CallContext& context)
					{
						const std::string path = context.Text(0);
						return context.GetVM().MakeText(Guard([&path] { return SaveScreenshot(path, 1920); }));
					} },
				{ { "user folder", "The game's own folder for saves, reports and screenshots; same as user_folder()", "str" },
					[](FS::CallContext& context) { return context.GetVM().MakeText(GetUserFolder().string()); } },
				{ { "pause the game", "Pauses the game: physics, time() and scripts stop, except scripts marked Run While Paused; same as set_paused(true)", "None" },
					[](FS::CallContext&) { Guard([] { RequireContextScene().SetGamePaused(true); }); return FS::Value(); } },
				{ { "resume the game", "Lets a paused game go on, and every script picks up where it was; same as set_paused(false)", "None" },
					[](FS::CallContext&) { Guard([] { RequireContextScene().SetGamePaused(false); }); return FS::Value(); } },
				{ { "the game is paused", "True while the game is paused; same as is_paused()", "bool" },
					[](FS::CallContext&) { return FS::Value::FromBool(Guard([] { return RequireContextScene().IsGamePaused(); })); } },
				{ { "keep (entity) across scenes", "Keeps the entity and its children when load_scene changes the scene, with their scripts, physics and sounds; same as set entity.keep_across_scenes to true", "None", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity entity = PhraseEntity(context, "keep", "an entity", "keep self across scenes");
						Guard([&] { entity.Resolve().GetComponent<TagComponent>().KeepAcrossScenes = true; });
						return FS::Value();
					} },
				{ { "show (widget)", "Shows a UI entity and everything under it; same as set widget.visible to true", "None", true },
					SetUIFlag("show", false, true) },
				{ { "hide (widget)", "Hides a UI entity and everything under it; same as set widget.visible to false", "None", true },
					SetUIFlag("hide", false, false) },
				{ { "enable (widget)", "Lets the pointer and focus reach a UI entity again; same as set widget.interactable to true", "None", true },
					SetUIFlag("enable", true, true) },
				{ { "disable (widget)", "Stops the pointer and focus reaching a UI entity (it shows as disabled); same as set widget.interactable to false", "None", true },
					SetUIFlag("disable", true, false) },
				{ { "focus (widget)", "Gives a UI entity keyboard and gamepad focus, and gives back whether it could take it; same as widget.focus()", "bool", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity widget = PhraseWidget(context, "focus");
						return FS::Value::FromBool(Guard([&]
						{
							Detail::RequireUIComponent<UIRectComponent>(widget, "focus", "UIRect");
							Entity entity = widget.Resolve();
							return entity.GetScene()->SetUIFocus(entity);
						}));
					} },
				{ { "tag (entity) as (tag)", "Adds a tag to an entity (nothing happens when it has it already); same as entity.add_tag(tag)", "None", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity entity = PhraseEntity(context, "tag", "an entity", "tag self as \"enemy\"");
						const std::string tag = context.Text(1);
						Guard([&] { Detail::AddTag(entity, tag); });
						return FS::Value();
					} },
				{ { "untag (entity) as (tag)", "Takes a tag off an entity (nothing happens when it doesn't have it); same as entity.remove_tag(tag)", "None", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity entity = PhraseEntity(context, "untag", "an entity", "untag self as \"enemy\"");
						const std::string tag = context.Text(1);
						Guard([&] { EntityTags::RemoveTag(entity.Resolve().GetComponent<TagComponent>(), tag); });
						return FS::Value();
					} },
				{ { "everything tagged (tag)", "A list of every entity with the tag, parents before their children; same as find_tagged(tag)", "list[Entity]", true },
					[](FS::CallContext& context)
					{
						const std::string tag = context.Text(0);
						std::vector<ScriptEntity> entities;
						Guard([&]
						{
							for (Entity entity : EntityTags::FindTagged(RequireContextScene(), tag))
								entities.emplace_back(entity);
						});
						return ReturnValue(context.GetVM(), entities);
					} },
				{ { "everything in group (name)", "A list of the top-level entities in the Hierarchy group, in Hierarchy order; same as group(name).entities()", "list[Entity]", true },
					[](FS::CallContext& context)
					{
						const ScriptGroup group = PhraseGroup(context, 0);
						return ReturnValue(context.GetVM(), Guard([&] { return Detail::GroupEntities(group, false); }));
					} },
				{ { "everything under group (name)", "A list of the entities in the Hierarchy group and everything under them, parents before their children; same as group(name).all_entities()", "list[Entity]", true },
					[](FS::CallContext& context)
					{
						const ScriptGroup group = PhraseGroup(context, 0);
						return ReturnValue(context.GetVM(), Guard([&] { return Detail::GroupEntities(group, true); }));
					} },
				{ { "group (name) has (entity)", "Yes when the entity, or its top-level ancestor, is in the Hierarchy group; same as group(name).has_entity(entity)", "bool", true },
					[](FS::CallContext& context)
					{
						const ScriptGroup group = PhraseGroup(context, 0);
						const ScriptEntity entity = PhraseEntityAt(context, 1, "group ... has", "an entity", "if group \"Docking\" has bay then");
						return FS::Value::FromBool(Guard([&]
						{
							auto [scene, sceneGroup] = Detail::ResolveGroup(group);
							Entity resolved = entity.Resolve();
							return resolved.GetScene() == scene && scene->GetEntityGroup(resolved) == sceneGroup->ID;
						}));
					} },
				{ { "put (entity) in group (name)", "Moves a top-level entity into the Hierarchy group, from any other; same as group(name).add_entity(entity)", "None", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity entity = PhraseEntity(context, "put", "an entity", "put bay in group \"Docking\"");
						const ScriptGroup group = PhraseGroup(context, 1);
						Guard([&] { Detail::SetEntityGroup(entity, group); });
						return FS::Value();
					} },
				{ { "remove (entity) from group (name)", "Takes a top-level entity out of the Hierarchy group, into none (nothing happens when it isn't in it); same as group(name).remove_entity(entity)", "None", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity entity = PhraseEntity(context, "remove", "an entity", "remove bay from group \"Docking\"");
						const ScriptGroup group = PhraseGroup(context, 1);
						Guard([&]
						{
							auto [scene, sceneGroup] = Detail::ResolveGroup(group);
							Entity resolved = entity.Resolve();
							if (resolved.GetScene() == scene && !resolved.GetParent() && scene->GetEntityGroup(resolved) == sceneGroup->ID)
								scene->SetEntityGroup(resolved, 0);
						});
						return FS::Value();
					} },
				{ { "the data in (path)", "The contents of a JSON data file in the Assets folder (\"Data/items.json\"), as maps and lists; same as load_json(path)", "Value", true },
					[](FS::CallContext& context)
					{
						const std::string path = context.Text(0);
						return ReturnValue(context.GetVM(), Guard([&] { return LoadDataJson(path); }));
					} },
				{ { "shake the camera by (strength) over (seconds) seconds", "Shakes the game camera's view, fading out over the seconds; same as shake_camera(strength, seconds)", "None", true },
					ShakeCameraPhrase() },
				{ { "shake the camera by (strength) over (seconds) second", "Shakes the game camera's view, fading out over the second; same as shake_camera(strength, seconds)", "None", true },
					ShakeCameraPhrase() },
				{ { "play animation (clip) on (entity)", "Plays one of an entity's animation clips, fading in over 0.2 seconds; same as entity.get_component(\"Animator\").play(clip)", "None", true },
					[](FS::CallContext& context)
					{
						const std::string clip = context.Text(0);
						const AnimatorRef animator = PhraseAnimator(context, 1, "play animation", "play animation \"Walk\" on hero");
						Guard([&] { Detail::AnimatorPlay(animator, clip, 0.2); });
						return FS::Value();
					} },
				{ { "pause animation on (entity)", "Holds an entity's animation where it is; same as entity.get_component(\"Animator\").pause()", "None", true },
					[](FS::CallContext& context)
					{
						const AnimatorRef animator = PhraseAnimator(context, 0, "pause animation", "pause animation on hero");
						Guard([&] { animator.Get().RuntimePlayer.Pause(); });
						return FS::Value();
					} },
				{ { "resume animation on (entity)", "Lets a paused animation go on; same as entity.get_component(\"Animator\").resume()", "None", true },
					[](FS::CallContext& context)
					{
						const AnimatorRef animator = PhraseAnimator(context, 0, "resume animation", "resume animation on hero");
						Guard([&] { animator.Get().RuntimePlayer.Resume(); });
						return FS::Value();
					} },
				{ { "walk (entity) to (position)", "Sends an entity's NavAgent walking to a position, and gives back whether there's navmesh to walk to near it; same as entity.get_component(\"NavAgent\").set_destination(position)", "bool", true },
					[](FS::CallContext& context)
					{
						const NavAgentRef agent = PhraseNavAgent(context, "walk", "walk hero to vector(4, 0, 2)");
						const glm::vec3 position = Convert<glm::vec3>::From(context.GetVM(), context.Arg(1), "'walk'");
						return FS::Value::FromBool(Guard([&] { return Detail::NavAgentSetDestination(agent, position); }));
					} },
				{ { "halt (entity)", "Stops an entity's NavAgent where it is; same as entity.get_component(\"NavAgent\").stop()", "None", true },
					[](FS::CallContext& context)
					{
						const NavAgentRef agent = PhraseNavAgent(context, "halt", "halt hero");
						Guard([&] { Detail::NavAgentStop(agent); });
						return FS::Value();
					} },
				{ { "the path from (start) to (end)", "The corners of the shortest walkable path between two positions, both ends included (an empty list when nothing connects them); same as find_path(start, end)", "list[Vec3]", true },
					[](FS::CallContext& context)
					{
						const glm::vec3 start = Convert<glm::vec3>::From(context.GetVM(), context.Arg(0), "'the path from'");
						const glm::vec3 end = Convert<glm::vec3>::From(context.GetVM(), context.Arg(1), "'the path from'");
						return ReturnValue(context.GetVM(), Guard([&] { return Detail::RequireNavMesh().FindPath(start, end); }));
					} },
				{ { "push (entity) by (impulse)", "Gives an entity's RigidBody a sudden push, a vector of mass times speed; same as entity.rigid_body.add_impulse(impulse)", "None", true },
					[](FS::CallContext& context)
					{
						const ScriptEntity entity = PhraseEntity(context, "push", "an entity", "push self by vector(0, 5, 0)");
						const glm::vec3 impulse = Convert<glm::vec3>::From(context.GetVM(), context.Arg(1), "'push'");
						Guard([&] { Detail::AddImpulse(RigidBodyRef { entity }, impulse); });
						return FS::Value();
					} },
			};
			// Each tween phrase in four spellings: "second" and "seconds", each with and without
			// "with (style)" for the easing (FSC checked that these parse with any expression between).
			for (const TweenPhrase& phrase : c_TweenPhrases)
			{
				for (const char* unit : { "seconds", "second" })
				{
					for (const bool withStyle : { false, true })
					{
						std::string pattern = std::string(phrase.Verb) + " (entity) " + phrase.Link + " (" + phrase.Value + ") over (seconds) " + unit;
						std::string same = phrase.Same;
						std::string easing = "\"in out\"";
						if (withStyle)
						{
							pattern += " with (style)";
							same.insert(same.size() - 1, ", style");
							easing = "with an easing style (\"bounce out\"; ease() lists them)";
						}
						const std::string doc = std::string(phrase.Doc) + " over a number of seconds, eased " + easing +
							", and gives back the Tween; same as " + same;
						phrases.push_back({ { pattern, doc, "Tween", true }, TweenPhraseFunction(phrase, withStyle) });
					}
				}
			}
			return phrases;
			}();
			return s_Phrases;
		}

	}

	std::vector<ForgeScriptPhraseInfo> GetForgeScriptPhrases()
	{
		std::vector<ForgeScriptPhraseInfo> phrases;
		for (const EnginePhrase& phrase : GetEnginePhrases())
			phrases.push_back(phrase.Info);
		return phrases;
	}

	void RegisterForgeScriptApi(FS::VM& vm)
	{
		ForgeScriptApi api(vm);
		DefineScriptApi(api);
		for (const EnginePhrase& phrase : GetEnginePhrases())
		{
			std::string error;
			const bool registered = vm.RegisterPhrase(phrase.Info.Pattern, phrase.Function, &error, phrase.Info.YieldsToCalls);
			BW_CORE_VERIFY(registered, "Engine phrase '{}' is not valid: {}", phrase.Info.Pattern, error);
		}
	}

	FS::Value MakeForgeScriptEntity(FS::VM& vm, const ScriptEntity& entity)
	{
		return Convert<ScriptEntity>::To(vm, entity);
	}

	namespace {

		// Deeper data is almost certainly a list that contains itself.
		constexpr int c_MaxEventValueDepth = 32;

		ScriptValue ToScriptValueAt(FS::VM& vm, const FS::Value& value, std::vector<std::string>& outUnsendable, int depth)
		{
			if (value.IsNothing())
				return {};
			if (value.IsBool())
				return value.AsBool();
			if (value.IsNumber())
				return value.AsNumber();
			if (FS::IsObjectType(value, FS::ObjectType::String))
				return FS::As<FS::String>(value)->Text;
			if (FS::IsObjectType(value, FS::ObjectType::Vector))
				return Convert<glm::vec3>::From(vm, value, "an event value");
			if (FS::IsObjectType(value, FS::ObjectType::NativeObject) && FS::As<FS::NativeObject>(value)->Type == &TypeFor<ScriptEntity>())
				return Convert<ScriptEntity>::From(vm, value, "an event value");

			const bool container = FS::IsObjectType(value, FS::ObjectType::List) || FS::IsObjectType(value, FS::ObjectType::Map);
			if (container && depth >= c_MaxEventValueDepth)
			{
				outUnsendable.push_back("a list or map nested more than " + std::to_string(c_MaxEventValueDepth) + " deep");
				return {};
			}
			if (FS::IsObjectType(value, FS::ObjectType::List))
			{
				ScriptValueList items;
				for (const FS::Value& item : FS::As<FS::List>(value)->Items)
					items.push_back(ToScriptValueAt(vm, item, outUnsendable, depth + 1));
				return items;
			}
			if (FS::IsObjectType(value, FS::ObjectType::Map))
			{
				ScriptValueMap entries;
				for (const FS::Map::Entry& entry : FS::As<FS::Map>(value)->Entries)
				{
					if (!FS::IsObjectType(entry.Key, FS::ObjectType::String))
					{
						outUnsendable.push_back("a map key that is not text (" + Describe(vm, entry.Key) + ")");
						continue;
					}
					entries.emplace_back(FS::As<FS::String>(entry.Key)->Text, ToScriptValueAt(vm, entry.Item, outUnsendable, depth + 1));
				}
				return entries;
			}
			outUnsendable.push_back(Describe(vm, value));
			return {};
		}

	}

	ScriptValue ToScriptValue(FS::VM& vm, const FS::Value& value, std::vector<std::string>& outUnsendable)
	{
		return ToScriptValueAt(vm, value, outUnsendable, 0);
	}

	FS::Value FromScriptValue(FS::VM& vm, const ScriptValue& value)
	{
		return std::visit([&vm](const auto& data) -> FS::Value
		{
			using T = std::decay_t<decltype(data)>;
			if constexpr (std::is_same_v<T, std::monostate>)
				return FS::Value();
			else if constexpr (std::is_same_v<T, bool>)
				return FS::Value::FromBool(data);
			else if constexpr (std::is_same_v<T, double>)
				return FS::Value::FromNumber(data);
			else if constexpr (std::is_same_v<T, std::string>)
				return vm.MakeText(data);
			else if constexpr (std::is_same_v<T, glm::vec3>)
				return vm.MakeVector(data.x, data.y, data.z);
			else if constexpr (std::is_same_v<T, ScriptEntity>)
				return Convert<ScriptEntity>::To(vm, data);
			else if constexpr (std::is_same_v<T, ScriptValueList>)
			{
				std::vector<FS::Value> items;
				for (const ScriptValue& item : data)
					items.push_back(FromScriptValue(vm, item));
				return vm.MakeList(std::move(items));
			}
			else
			{
				FS::Value map = vm.MakeMap();
				for (const auto& [key, item] : data)
					FS::As<FS::Map>(map)->Set(vm.MakeText(key), FromScriptValue(vm, item));
				return map;
			}
		}, value.Data);
	}

}
