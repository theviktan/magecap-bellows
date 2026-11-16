#include "bwpch.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"

#include "Bellows/Scripting/ScriptApiDefinition.h"


#include <cmath>
#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace FS = ForgeScript;

namespace Bellows::Scripting {

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
			static FS::Value To(FS::VM&, T value) { return FS::Value::FromNumber(static_cast<double>(value)); }
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
			static FS::Value To(FS::VM& vm, const glm::vec3& value) { return vm.MakeVector(value.x, value.y, value.z); }
			static glm::vec3 From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::Vector))
				{
					const FS::Vector* v = FS::As<FS::Vector>(value);
					return glm::vec3(static_cast<float>(v->X), static_cast<float>(v->Y), static_cast<float>(v->Z));
				}
				if (FS::IsObjectType(value, FS::ObjectType::List))
				{
					std::vector<double> n = NumberList(vm, value, what, 3, 3);
					return glm::vec3(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]));
				}
				Mismatch(vm, what, "a vector", value);
			}
		};

		template<>
		struct Convert<glm::vec2>
		{
			static FS::Value To(FS::VM& vm, const glm::vec2& value) { return vm.MakeVector(value.x, value.y, 0.0); }
			static glm::vec2 From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::Vector))
				{
					const FS::Vector* v = FS::As<FS::Vector>(value);
					return glm::vec2(static_cast<float>(v->X), static_cast<float>(v->Y));
				}
				if (FS::IsObjectType(value, FS::ObjectType::List))
				{
					std::vector<double> n = NumberList(vm, value, what, 2, 2);
					return glm::vec2(static_cast<float>(n[0]), static_cast<float>(n[1]));
				}
				Mismatch(vm, what, "a vector", value);
			}
		};

		template<>
		struct Convert<glm::quat>
		{
			static FS::Value To(FS::VM& vm, const glm::quat& q) { return MakeNumberList(vm, { q.w, q.x, q.y, q.z }); }
			static glm::quat From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				std::vector<double> n = NumberList(vm, value, what, 4, 4);
				return glm::quat(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]), static_cast<float>(n[3]));
			}
		};

		template<>
		struct Convert<ColorValue>
		{
			static FS::Value To(FS::VM& vm, const ColorValue& color) { return MakeNumberList(vm, { color.Value.r, color.Value.g, color.Value.b, color.Value.a }); }
			static ColorValue From(FS::VM& vm, const FS::Value& value, const std::string& what)
			{
				if (FS::IsObjectType(value, FS::ObjectType::Vector))
					return ColorValue { glm::vec4(Convert<glm::vec3>::From(vm, value, what), 1.0f) };
				if (!FS::IsObjectType(value, FS::ObjectType::List))
					Mismatch(vm, what, "a color like [1, 0.5, 0]", value);
				std::vector<double> n = NumberList(vm, value, what, 3, 4);
				return ColorValue { glm::vec4(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]), n.size() > 3 ? static_cast<float>(n[3]) : 1.0f) };
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

		template<typename T>
		concept HasIdentity = requires(const T& value) { IdentityOf(value); };

		std::string EntityText(const ScriptEntity& entity)
		{
			Entity resolved = entity.TryResolve();
			return resolved ? "Entity('" + resolved.GetName() + "')" : std::string("Entity(<destroyed>)");
		}

		inline std::string TextOf(const ScriptEntity& entity, const std::string&) { return EntityText(entity); }

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

	void RegisterForgeScriptApi(FS::VM& vm)
	{
		ForgeScriptApi api(vm);
		DefineScriptApi(api);
	}

	FS::Value MakeForgeScriptEntity(FS::VM& vm, const ScriptEntity& entity)
	{
		return Convert<ScriptEntity>::To(vm, entity);
	}

}
