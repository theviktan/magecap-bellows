#include "bwpch.h"
#include "Bellows/Scripting/ScriptApiDescription.h"

#include "Bellows/Scripting/ScriptApiDefinition.h"

#include <deque>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <variant>

namespace Bellows::Scripting {

	namespace {

		// Class names by C++ type, filled while the API is walked so parameters can name them.
		using ClassNames = std::unordered_map<std::type_index, std::string>;

		// -------------------------------------------------------------------------
		// Callable signatures
		// -------------------------------------------------------------------------

		template<typename F>
		struct Signature : Signature<decltype(&F::operator())> {};

		template<typename C, typename R, typename... A>
		struct Signature<R (C::*)(A...) const>
		{
			using Return = R;
			using Params = std::tuple<A...>;
		};

		template<typename C, typename R, typename... A>
		struct Signature<R (C::*)(A...)>
		{
			using Return = R;
			using Params = std::tuple<A...>;
		};

		template<typename R, typename... A>
		struct Signature<R (*)(A...)>
		{
			using Return = R;
			using Params = std::tuple<A...>;
		};

		// -------------------------------------------------------------------------
		// Type names
		// -------------------------------------------------------------------------

		template<typename T> struct IsOptional : std::false_type {};
		template<typename T> struct IsOptional<std::optional<T>> : std::true_type {};
		template<typename T> struct IsVector : std::false_type {};
		template<typename T> struct IsVector<std::vector<T>> : std::true_type {};
		template<typename T> struct IsVariant : std::false_type {};
		template<typename... T> struct IsVariant<std::variant<T...>> : std::true_type {};
		template<typename T> struct IsTuple : std::false_type {};
		template<typename... T> struct IsTuple<std::tuple<T...>> : std::true_type {};
		template<typename A, typename B> struct IsTuple<std::pair<A, B>> : std::true_type {};

		template<typename T>
		std::string TypeName(const ClassNames& classes);

		template<typename... T>
		std::string JoinTypeNames(const ClassNames& classes, const char* separator)
		{
			std::string text;
			((text += (text.empty() ? "" : separator) + TypeName<T>(classes)), ...);
			return text;
		}

		template<typename Variant> struct VariantNames;
		template<typename... T>
		struct VariantNames<std::variant<T...>>
		{
			static std::string Get(const ClassNames& classes) { return JoinTypeNames<T...>(classes, " | "); }
		};

		template<typename Tuple> struct TupleNames;
		template<typename... T>
		struct TupleNames<std::tuple<T...>>
		{
			static std::string Get(const ClassNames& classes) { return "tuple[" + JoinTypeNames<T...>(classes, ", ") + "]"; }
		};
		template<typename A, typename B>
		struct TupleNames<std::pair<A, B>>
		{
			static std::string Get(const ClassNames& classes) { return "tuple[" + JoinTypeNames<A, B>(classes, ", ") + "]"; }
		};

		template<typename T>
		std::string TypeName(const ClassNames& classes)
		{
			using U = std::remove_cvref_t<T>;
			if constexpr (std::is_void_v<U>) return "None";
			else if constexpr (std::is_same_v<U, bool>) return "bool";
			else if constexpr (std::is_integral_v<U>) return "int";
			else if constexpr (std::is_floating_point_v<U>) return "float";
			else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, const char*>) return "str";
			else if constexpr (std::is_same_v<U, glm::vec2>) return "Vec2";
			else if constexpr (std::is_same_v<U, glm::vec3>) return "Vec3";
			else if constexpr (std::is_same_v<U, glm::vec4>) return "Vec4";
			else if constexpr (std::is_same_v<U, glm::quat>) return "Quat";
			else if constexpr (std::is_same_v<U, ColorValue>) return "Vec4 | list[float]";
			else if constexpr (std::is_same_v<U, KeyArg>) return "Key | str";
			else if constexpr (std::is_same_v<U, MouseButtonArg>) return "MouseButton | str";
			else if constexpr (std::is_same_v<U, ComponentName>) return "str";
			else if constexpr (std::is_same_v<U, ComponentHandle>) return "Component";
			else if constexpr (IsOptional<U>::value) return TypeName<typename U::value_type>(classes) + " | None";
			else if constexpr (IsVector<U>::value) return "list[" + TypeName<typename U::value_type>(classes) + "]";
			else if constexpr (IsVariant<U>::value) return VariantNames<U>::Get(classes);
			else if constexpr (IsTuple<U>::value) return TupleNames<U>::Get(classes);
			else
			{
				auto it = classes.find(std::type_index(typeid(U)));
				return it != classes.end() ? it->second : "Any";
			}
		}

		// -------------------------------------------------------------------------
		// Default values
		// -------------------------------------------------------------------------

		template<typename T>
		Json DefaultValue(const T& value)
		{
			using U = std::remove_cvref_t<T>;
			if constexpr (std::is_same_v<U, bool> || std::is_arithmetic_v<U> || std::is_same_v<U, std::string>)
				return value;
			else if constexpr (std::is_same_v<U, const char*>)
				return std::string(value);
			else if constexpr (std::is_same_v<U, glm::vec2>) return Json::array({ value.x, value.y });
			else if constexpr (std::is_same_v<U, glm::vec3>) return Json::array({ value.x, value.y, value.z });
			else if constexpr (std::is_same_v<U, glm::vec4>) return Json::array({ value.x, value.y, value.z, value.w });
			else if constexpr (std::is_same_v<U, ColorValue>) return DefaultValue(value.Value);
			else if constexpr (std::is_same_v<U, KeyArg>)
			{
				const char* name = GetKeyCodeName(value.Code);
				return name ? Json(name) : Json(nullptr);
			}
			else if constexpr (std::is_same_v<U, MouseButtonArg>)
			{
				const char* name = GetMouseButtonName(value.Button);
				return name ? Json(name) : Json(nullptr);
			}
			else if constexpr (std::is_same_v<U, ComponentName>) return value.Name;
			else if constexpr (IsOptional<U>::value)
				return value ? DefaultValue(*value) : Json(nullptr);
			else if constexpr (IsVector<U>::value)
			{
				Json list = Json::array();
				for (const auto& item : value)
					list.push_back(DefaultValue(item));
				return list;
			}
			else
				return nullptr;
		}

		// -------------------------------------------------------------------------
		// Parameters
		// -------------------------------------------------------------------------

		template<typename Param>
		Json DescribeParam(const ClassNames& classes, const ArgName& spec)
		{
			return Json { { "name", spec.Name }, { "type", TypeName<Param>(classes) } };
		}

		template<typename Param, typename T>
		Json DescribeParam(const ClassNames& classes, const ArgWithDefault<T>& spec)
		{
			return Json { { "name", spec.Name }, { "type", TypeName<Param>(classes) }, { "default", DefaultValue(spec.Default) } };
		}

		// Params is the full parameter tuple; Skip leading entries (the object itself for methods) have no Arg.
		template<typename Params, size_t Skip, typename... Specs, size_t... I>
		Json DescribeParams(const ClassNames& classes, const std::tuple<Specs...>& specs, std::index_sequence<I...>)
		{
			Json params = Json::array();
			(params.push_back(DescribeParam<std::tuple_element_t<I + Skip, Params>>(classes, std::get<I>(specs))), ...);
			return params;
		}

		template<typename Callable, size_t Skip, typename... Args>
		Json DescribeCallable(const ClassNames& classes, const char* doc, const Args&... args)
		{
			using Params = typename Signature<Callable>::Params;
			static_assert(std::tuple_size_v<Params> == sizeof...(Args) + Skip, "Every parameter needs an Arg(...) in ScriptApiDefinition.h");
			Json description;
			description["params"] = DescribeParams<Params, Skip>(classes, std::make_tuple(args...), std::index_sequence_for<Args...>());
			description["returns"] = TypeName<typename Signature<Callable>::Return>(classes);
			if (doc)
				description["doc"] = doc;
			return description;
		}

		// -------------------------------------------------------------------------
		// The description binder
		// -------------------------------------------------------------------------

		template<typename T>
		class DescriptionClass
		{
		public:
			DescriptionClass(const ClassNames& classes, Json& type) : m_Classes(classes), m_Type(type) {}

			template<typename Getter, typename Setter>
			DescriptionClass& Property(const char* name, Getter, Setter, const char* doc)
			{
				AddProperty<Getter>(name, doc, false);
				return *this;
			}

			template<typename Getter>
			DescriptionClass& ReadOnly(const char* name, Getter, const char* doc)
			{
				AddProperty<Getter>(name, doc, true);
				return *this;
			}

			template<typename Callable, typename... Args>
			DescriptionClass& Method(const char* name, Callable, const char* doc, const Args&... args)
			{
				Json member { { "name", name }, { "kind", "method" } };
				member.update(DescribeCallable<Callable, 1>(m_Classes, doc, args...));
				m_Type["members"].push_back(std::move(member));
				return *this;
			}

			template<typename Callable, typename... Args>
			DescriptionClass& Constructor(Callable, const Args&... args)
			{
				Json constructor = DescribeCallable<Callable, 0>(m_Classes, nullptr, args...);
				constructor.erase("returns");
				m_Type["constructor"] = std::move(constructor);
				return *this;
			}

			template<typename Callable, typename... Args>
			DescriptionClass& Static(const char* name, Callable, const char* doc, const Args&... args)
			{
				Json member { { "name", name }, { "kind", "static" } };
				member.update(DescribeCallable<Callable, 0>(m_Classes, doc, args...));
				m_Type["members"].push_back(std::move(member));
				return *this;
			}

		private:
			template<typename Getter>
			void AddProperty(const char* name, const char* doc, bool readOnly)
			{
				Json member { { "name", name }, { "kind", "property" }, { "type", TypeName<typename Signature<Getter>::Return>(m_Classes) } };
				if (readOnly)
					member["readonly"] = true;
				member["doc"] = doc ? doc : "";
				m_Type["members"].push_back(std::move(member));
			}

		private:
			const ClassNames& m_Classes;
			Json& m_Type;
		};

		// Walked twice: the first walk only learns class names, so the second can name every type.
		class DescriptionApi
		{
		public:
			explicit DescriptionApi(ClassNames& classes) : m_Classes(classes) {}

			template<typename Callable, typename... Args>
			void Function(const char* name, Callable, const char* doc, const Args&... args)
			{
				Json function { { "name", name } };
				function.update(DescribeCallable<Callable, 0>(m_Classes, doc ? doc : "", args...));
				m_Functions.push_back(std::move(function));
			}

			template<typename T>
			DescriptionClass<T> Class(const char* name, const char* doc)
			{
				m_Classes[std::type_index(typeid(T))] = name;
				m_Types.push_back(Json { { "name", name }, { "doc", doc ? doc : "" }, { "members", Json::array() } });
				return DescriptionClass<T>(m_Classes, m_Types.back());
			}

			Json Finish()
			{
				Json functions = Json::array();
				for (Json& function : m_Functions)
					functions.push_back(std::move(function));
				Json types = Json::array();
				for (Json& type : m_Types)
					types.push_back(std::move(type));
				return Json { { "version", 1 }, { "functions", std::move(functions) }, { "types", std::move(types) }, { "phrases", Json::array() } };
			}

		private:
			ClassNames& m_Classes;
			std::vector<Json> m_Functions;
			// A deque keeps each type's Json in place while its DescriptionClass adds members.
			std::deque<Json> m_Types;
		};

	}

	Json DescribeScriptApi()
	{
		ClassNames classes;
		{
			DescriptionApi namesOnly(classes);
			DefineScriptApi(namesOnly);
		}
		DescriptionApi api(classes);
		DefineScriptApi(api);
		return api.Finish();
	}

	std::string WriteScriptApiDescription()
	{
		return DescribeScriptApi().dump(1, '\t') + "\n";
	}

}
