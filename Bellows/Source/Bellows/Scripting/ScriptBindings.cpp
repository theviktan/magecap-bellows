#include "bwpch.h"
#include "Bellows/Scripting/ScriptGlue.h"

#include "Bellows/Scripting/ScriptApiDefinition.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>

#include <glm/gtx/quaternion.hpp>

#include <typeindex>
#include <unordered_map>

namespace py = pybind11;

// -----------------------------------------------------------------------------
// How the language-neutral argument types of ScriptApi.h look in Python
// -----------------------------------------------------------------------------

namespace pybind11::detail {

	// A color: a Vec4, a Vec3 (opaque) or any sequence of 3 or 4 numbers. Returned as a Vec4.
	template<>
	struct type_caster<Bellows::Scripting::ColorValue>
	{
		PYBIND11_TYPE_CASTER(Bellows::Scripting::ColorValue, const_name("Vec4 | typing.Sequence[float]"));

		bool load(handle source, bool)
		{
			if (source.is_none() || isinstance<pybind11::str>(source))
				return false;
			object object = reinterpret_borrow<pybind11::object>(source);
			if (isinstance<glm::vec4>(object))
			{
				value.Value = object.cast<glm::vec4>();
				return true;
			}
			if (isinstance<glm::vec3>(object))
			{
				value.Value = glm::vec4(object.cast<glm::vec3>(), 1.0f);
				return true;
			}
			if (!isinstance<pybind11::sequence>(object))
				return false;
			const pybind11::sequence items = object.cast<pybind11::sequence>();
			const size_t count = len(items);
			if (count != 3 && count != 4)
				throw Bellows::Scripting::ScriptValueError("Expected a sequence of 3 or 4 numbers");
			value.Value = glm::vec4(1.0f);
			for (size_t i = 0; i < count; i++)
				value.Value[static_cast<int>(i)] = items[i].cast<float>();
			return true;
		}

		static handle cast(const Bellows::Scripting::ColorValue& color, return_value_policy, handle parent)
		{
			return type_caster_base<glm::vec4>::cast(color.Value, return_value_policy::move, parent);
		}
	};

	// Any value the engine can pass between languages (save_data's value): None, bool, numbers, str,
	// Vec3, Entity, and lists and dicts (with str keys) of these.
	template<>
	struct type_caster<Bellows::Scripting::ScriptValue>
	{
		PYBIND11_TYPE_CASTER(Bellows::Scripting::ScriptValue, const_name("typing.Any"));

		bool load(handle source, bool)
		{
			value = Bellows::Scripting::ScriptValueFromPython(source);
			return true;
		}

		static handle cast(const Bellows::Scripting::ScriptValue& data, return_value_policy, handle)
		{
			return Bellows::Scripting::ScriptValueToPython(data).release();
		}
	};

	// A key: a bellows.Key, a key name ("W", "space") or a key code.
	template<>
	struct type_caster<Bellows::Scripting::KeyArg>
	{
		PYBIND11_TYPE_CASTER(Bellows::Scripting::KeyArg, const_name("Key | str"));

		bool load(handle source, bool)
		{
			object object = reinterpret_borrow<pybind11::object>(source);
			if (isinstance<Bellows::KeyCode>(object))
				value.Code = object.cast<Bellows::KeyCode>();
			else if (isinstance<pybind11::str>(object))
				value.Code = Bellows::Scripting::KeyFromName(object.cast<std::string>());
			else if (isinstance<pybind11::int_>(object) && !isinstance<pybind11::bool_>(object))
				value.Code = static_cast<Bellows::KeyCode>(object.cast<int>());
			else
				throw type_error("Expected a bellows.Key, a key name or a key code");
			return true;
		}

		static handle cast(const Bellows::Scripting::KeyArg& key, return_value_policy, handle)
		{
			return pybind11::cast(key.Code).release();
		}
	};

	// A mouse button: a bellows.MouseButton, a button number, or 'left', 'right' or 'middle'.
	template<>
	struct type_caster<Bellows::Scripting::MouseButtonArg>
	{
		PYBIND11_TYPE_CASTER(Bellows::Scripting::MouseButtonArg, const_name("MouseButton | str"));

		bool load(handle source, bool)
		{
			object object = reinterpret_borrow<pybind11::object>(source);
			if (isinstance<Bellows::MouseButton>(object))
				value.Button = object.cast<Bellows::MouseButton>();
			else if (isinstance<pybind11::int_>(object) && !isinstance<pybind11::bool_>(object))
				value.Button = static_cast<Bellows::MouseButton>(object.cast<int>());
			else if (isinstance<pybind11::str>(object))
				value.Button = Bellows::Scripting::MouseButtonFromName(object.cast<std::string>());
			else
				throw Bellows::Scripting::ScriptValueError("Expected bellows.MouseButton, 'left', 'right' or 'middle'");
			return true;
		}

		static handle cast(const Bellows::Scripting::MouseButtonArg& button, return_value_policy, handle)
		{
			return pybind11::cast(button.Button).release();
		}
	};

	// A component: its name ("RigidBody") or its class (bellows.RigidBody).
	template<>
	struct type_caster<Bellows::Scripting::ComponentName>
	{
		PYBIND11_TYPE_CASTER(Bellows::Scripting::ComponentName, const_name("str | type"));

		bool load(handle source, bool)
		{
			object object = reinterpret_borrow<pybind11::object>(source);
			if (isinstance<pybind11::str>(object))
				value.Name = object.cast<std::string>();
			else if (hasattr(object, "__name__"))
				value.Name = object.attr("__name__").cast<std::string>();
			else
				throw type_error("Expected a component name or component class");
			return true;
		}

		static handle cast(const Bellows::Scripting::ComponentName& component, return_value_policy, handle)
		{
			return pybind11::str(component.Name).release();
		}
	};

}

namespace Bellows::Scripting {

	void LinkBindings()
	{
	}

	namespace {

		// -------------------------------------------------------------------------
		// The Python binder: turns the shared API description into pybind11 definitions.
		// -------------------------------------------------------------------------

		inline py::arg ToPython(const ArgName& argument) { return py::arg(argument.Name); }

		template<typename T>
		py::arg_v ToPython(const ArgWithDefault<T>& argument) { return py::arg(argument.Name) = argument.Default; }

		template<typename T>
		class PythonClass
		{
		public:
			explicit PythonClass(py::class_<T> type) : m_Type(std::move(type)) {}

			template<typename Getter, typename Setter>
			PythonClass& Property(const char* name, Getter&& getter, Setter&& setter, const char* doc)
			{
				m_Type.def_property(name, std::forward<Getter>(getter), std::forward<Setter>(setter), doc);
				return *this;
			}

			template<typename Getter>
			PythonClass& ReadOnly(const char* name, Getter&& getter, const char* doc)
			{
				m_Type.def_property_readonly(name, std::forward<Getter>(getter), doc);
				return *this;
			}

			template<typename Callable, typename... Args>
			PythonClass& Method(const char* name, Callable&& function, const char* doc, const Args&... args)
			{
				m_Type.def(name, std::forward<Callable>(function), doc, ToPython(args)...);
				return *this;
			}

			template<typename Callable, typename... Args>
			PythonClass& Constructor(Callable&& function, const Args&... args)
			{
				m_Type.def(py::init(std::forward<Callable>(function)), ToPython(args)...);
				return *this;
			}

			template<typename Callable, typename... Args>
			PythonClass& Static(const char* name, Callable&& function, const char* doc, const Args&... args)
			{
				m_Type.def_static(name, std::forward<Callable>(function), doc, ToPython(args)...);
				return *this;
			}

		private:
			py::class_<T> m_Type;
		};

		class PythonApi
		{
		public:
			explicit PythonApi(py::module_& module) : m_Module(module) {}

			template<typename Callable, typename... Args>
			void Function(const char* name, Callable&& function, const char* doc, const Args&... args)
			{
				m_Module.def(name, std::forward<Callable>(function), doc, ToPython(args)...);
			}

			template<typename T>
			PythonClass<T> Class(const char* name, const char* doc)
			{
				py::class_<T> type(m_Module, name, doc);
				m_Classes[std::type_index(typeid(T))] = type;
				return PythonClass<T>(type);
			}

			// The Python class made for T, so Python-only extras can be added after the shared API.
			template<typename T>
			py::class_<T> Get() const
			{
				return py::reinterpret_borrow<py::class_<T>>(m_Classes.at(std::type_index(typeid(T))));
			}

		private:
			py::module_& m_Module;
			std::unordered_map<std::type_index, py::object> m_Classes;
		};

		template<typename V>
		V SequenceToVector(const py::sequence& sequence)
		{
			constexpr int count = V::length();
			if (static_cast<int>(py::len(sequence)) != count)
				throw py::value_error("Expected a sequence of " + std::to_string(count) + " numbers");
			V result;
			for (int i = 0; i < count; i++)
				result[i] = sequence[static_cast<size_t>(i)].cast<float>();
			return result;
		}

		py::object ScriptFieldToPython(const ScriptFieldValue& value)
		{
			return std::visit([](const auto& v) -> py::object { return py::cast(v); }, value);
		}

		// Python's own types and the scalar values have no class to name, so give them a readable one.
		std::string PythonTypeName(const py::handle& value)
		{
			return py::str(py::type::handle_of(value).attr("__name__")).cast<std::string>();
		}

		py::object ScriptInstanceOrNone(UUID entity)
		{
			void* instance = ScriptEngine::GetScriptInstance(entity);
			return instance ? py::reinterpret_borrow<py::object>(static_cast<PyObject*>(instance)) : py::none();
		}

	}

	py::object ScriptValueToPython(const ScriptValue& value)
	{
		return std::visit([](const auto& data) -> py::object
		{
			using T = std::decay_t<decltype(data)>;
			if constexpr (std::is_same_v<T, std::monostate>)
				return py::none();
			else if constexpr (std::is_same_v<T, ScriptValueList>)
			{
				py::list list;
				for (const ScriptValue& item : data)
					list.append(ScriptValueToPython(item));
				return list;
			}
			else if constexpr (std::is_same_v<T, ScriptValueMap>)
			{
				py::dict dict;
				for (const auto& [key, item] : data)
					dict[py::str(key)] = ScriptValueToPython(item);
				return dict;
			}
			else
				return py::cast(data);
		}, value.Data);
	}

	ScriptValue ScriptValueFromPython(const py::handle& value)
	{
		if (value.is_none())
			return ScriptValue();
		if (py::isinstance<py::bool_>(value))
			return value.cast<bool>();
		if (py::isinstance<py::int_>(value) || py::isinstance<py::float_>(value))
			return value.cast<double>();
		if (py::isinstance<py::str>(value))
			return value.cast<std::string>();
		if (py::isinstance<glm::vec3>(value))
			return value.cast<glm::vec3>();
		// Vec2 and Vec4 (colors) go as lists of numbers, the way ForgeScript writes them.
		if (py::isinstance<glm::vec2>(value))
		{
			const glm::vec2 v = value.cast<glm::vec2>();
			return ScriptValueList { double(v.x), double(v.y) };
		}
		if (py::isinstance<glm::vec4>(value))
		{
			const glm::vec4 v = value.cast<glm::vec4>();
			return ScriptValueList { double(v.x), double(v.y), double(v.z), double(v.w) };
		}
		if (py::isinstance<ScriptEntity>(value))
			return value.cast<ScriptEntity>();
		if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value))
		{
			ScriptValueList list;
			for (const py::handle item : value)
				list.push_back(ScriptValueFromPython(item));
			return list;
		}
		if (py::isinstance<py::dict>(value))
		{
			ScriptValueMap map;
			for (const auto& [key, item] : value.cast<py::dict>())
			{
				if (!py::isinstance<py::str>(key))
					throw ScriptValueError("Dicts sent to the engine need text keys, not " + PythonTypeName(key));
				map.emplace_back(key.cast<std::string>(), ScriptValueFromPython(item));
			}
			return map;
		}
		throw ScriptValueError("Values sent to the engine can be None, bool, numbers, str, Vec2, Vec3, Vec4, Entity, lists and dicts, not " + PythonTypeName(value));
	}

	// -------------------------------------------------------------------------
	// Module definition
	// -------------------------------------------------------------------------

	void RegisterBindings(py::module_& m)
	{
		m.doc() = "MageCap Bellows native scripting API. Import 'bellows' instead of using this module directly.";

		// --- Math -------------------------------------------------------------
		py::class_<glm::vec2>(m, "Vec2", "2D vector")
			.def(py::init<>())
			.def(py::init<float>(), py::arg("scalar"))
			.def(py::init<float, float>(), py::arg("x"), py::arg("y"))
			.def(py::init([](const py::sequence& s) { return SequenceToVector<glm::vec2>(s); }))
			.def_readwrite("x", &glm::vec2::x)
			.def_readwrite("y", &glm::vec2::y)
			.def(py::self + py::self).def(py::self - py::self).def(py::self * float()).def(float() * py::self)
			.def(py::self / float()).def(py::self * py::self).def(-py::self).def(py::self == py::self)
			.def("length", [](const glm::vec2& v) { return glm::length(v); })
			.def("normalized", [](const glm::vec2& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; })
			.def("dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); })
			.def("copy", [](const glm::vec2& v) { return v; })
			.def("__getitem__", [](const glm::vec2& v, int i) { if (i < 0 || i > 1) throw py::index_error(); return v[i]; })
			.def("__len__", [](const glm::vec2&) { return 2; })
			.def("__iter__", [](const glm::vec2& v) { return py::iter(py::make_tuple(v.x, v.y)); })
			.def("__repr__", [](const glm::vec2& v) { return fmt::format("Vec2({:.4g}, {:.4g})", v.x, v.y); });

		py::class_<glm::vec3>(m, "Vec3", "3D vector")
			.def(py::init<>())
			.def(py::init<float>(), py::arg("scalar"))
			.def(py::init<float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"))
			.def(py::init([](const py::sequence& s) { return SequenceToVector<glm::vec3>(s); }))
			.def_readwrite("x", &glm::vec3::x)
			.def_readwrite("y", &glm::vec3::y)
			.def_readwrite("z", &glm::vec3::z)
			.def(py::self + py::self).def(py::self - py::self).def(py::self * float()).def(float() * py::self)
			.def(py::self / float()).def(py::self * py::self).def(-py::self).def(py::self == py::self)
			.def(py::self += py::self).def(py::self -= py::self).def(py::self *= float())
			.def("length", [](const glm::vec3& v) { return glm::length(v); })
			.def("length_squared", [](const glm::vec3& v) { return glm::dot(v, v); })
			.def("normalized", [](const glm::vec3& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; })
			.def("dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); })
			.def("cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); })
			.def("distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); })
			.def("lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); })
			.def("copy", [](const glm::vec3& v) { return v; })
			.def("__getitem__", [](const glm::vec3& v, int i) { if (i < 0 || i > 2) throw py::index_error(); return v[i]; })
			.def("__len__", [](const glm::vec3&) { return 3; })
			.def("__iter__", [](const glm::vec3& v) { return py::iter(py::make_tuple(v.x, v.y, v.z)); })
			.def("__repr__", [](const glm::vec3& v) { return fmt::format("Vec3({:.4g}, {:.4g}, {:.4g})", v.x, v.y, v.z); });

		py::class_<glm::vec4>(m, "Vec4", "4D vector (also used for RGBA colors)")
			.def(py::init<>())
			.def(py::init<float>(), py::arg("scalar"))
			.def(py::init<float, float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"))
			.def(py::init([](const py::sequence& s) { return SequenceToVector<glm::vec4>(s); }))
			.def_readwrite("x", &glm::vec4::x)
			.def_readwrite("y", &glm::vec4::y)
			.def_readwrite("z", &glm::vec4::z)
			.def_readwrite("w", &glm::vec4::w)
			.def_property("r", [](const glm::vec4& v) { return v.r; }, [](glm::vec4& v, float value) { v.r = value; })
			.def_property("g", [](const glm::vec4& v) { return v.g; }, [](glm::vec4& v, float value) { v.g = value; })
			.def_property("b", [](const glm::vec4& v) { return v.b; }, [](glm::vec4& v, float value) { v.b = value; })
			.def_property("a", [](const glm::vec4& v) { return v.a; }, [](glm::vec4& v, float value) { v.a = value; })
			.def(py::self + py::self).def(py::self - py::self).def(py::self * float()).def(float() * py::self)
			.def(py::self / float()).def(py::self * py::self).def(py::self == py::self)
			.def("copy", [](const glm::vec4& v) { return v; })
			.def("__getitem__", [](const glm::vec4& v, int i) { if (i < 0 || i > 3) throw py::index_error(); return v[i]; })
			.def("__len__", [](const glm::vec4&) { return 4; })
			.def("__iter__", [](const glm::vec4& v) { return py::iter(py::make_tuple(v.x, v.y, v.z, v.w)); })
			.def("__repr__", [](const glm::vec4& v) { return fmt::format("Vec4({:.4g}, {:.4g}, {:.4g}, {:.4g})", v.x, v.y, v.z, v.w); });

		py::class_<glm::quat>(m, "Quat", "Rotation quaternion")
			.def(py::init([]() { return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); }))
			.def(py::init<float, float, float, float>(), py::arg("w"), py::arg("x"), py::arg("y"), py::arg("z"))
			.def_static("from_euler", [](const glm::vec3& radians) { return glm::quat(radians); }, py::arg("radians"))
			.def_static("from_euler_degrees", [](const glm::vec3& degrees) { return glm::quat(glm::radians(degrees)); }, py::arg("degrees"))
			.def_static("from_axis_angle", [](const glm::vec3& axis, float radians) { return glm::angleAxis(radians, glm::normalize(axis)); },
				py::arg("axis"), py::arg("radians"))
			.def_readwrite("w", &glm::quat::w).def_readwrite("x", &glm::quat::x).def_readwrite("y", &glm::quat::y).def_readwrite("z", &glm::quat::z)
			.def(py::self * py::self)
			.def("__mul__", [](const glm::quat& q, const glm::vec3& v) { return q * v; })
			.def("to_euler", [](const glm::quat& q) { return glm::eulerAngles(q); })
			.def("to_euler_degrees", [](const glm::quat& q) { return glm::degrees(glm::eulerAngles(q)); })
			.def("normalized", [](const glm::quat& q) { return glm::normalize(q); })
			.def("inverse", [](const glm::quat& q) { return glm::inverse(q); })
			.def("slerp", [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); })
			.def("__repr__", [](const glm::quat& q) { return fmt::format("Quat(w={:.4g}, x={:.4g}, y={:.4g}, z={:.4g})", q.w, q.x, q.y, q.z); });

		py::implicitly_convertible<py::tuple, glm::vec2>();
		py::implicitly_convertible<py::list, glm::vec2>();
		py::implicitly_convertible<py::tuple, glm::vec3>();
		py::implicitly_convertible<py::list, glm::vec3>();
		py::implicitly_convertible<py::tuple, glm::vec4>();
		py::implicitly_convertible<py::list, glm::vec4>();

		m.def("lerp", [](float a, float b, float t) { return a + (b - a) * t; }, py::arg("a"), py::arg("b"), py::arg("t"));
		m.def("clamp", [](float value, float low, float high) { return std::clamp(value, low, high); }, py::arg("value"), py::arg("low"), py::arg("high"));

		// --- Input enums ------------------------------------------------------
		py::enum_<KeyCode> key(m, "Key", "Keyboard keys");
		for (const KeyName& name : GetKeyNames())
			key.value(name.Name, name.Code);

		py::enum_<MouseButton>(m, "MouseButton", "Mouse buttons")
			.value("Left", MouseButton::Left).value("Right", MouseButton::Right).value("Middle", MouseButton::Middle)
			.value("Button3", MouseButton::Button3).value("Button4", MouseButton::Button4);

		// --- Logging ----------------------------------------------------------
		auto joinArgs = [](const py::args& args)
		{
			std::string text;
			for (const auto& argument : args)
			{
				if (!text.empty())
					text += ' ';
				text += py::str(argument).cast<std::string>();
			}
			return text;
		};
		m.def("log", [joinArgs](const py::args& args) { BW_INFO("{}", joinArgs(args)); }, "Writes an info message to the console");
		m.def("log_warning", [joinArgs](const py::args& args) { BW_WARN("{}", joinArgs(args)); });
		m.def("log_error", [joinArgs](const py::args& args) { BW_ERROR("{}", joinArgs(args)); });
		m.def("_log_line", [](int level, const std::string& line)
		{
			if (level >= 2)
				BW_ERROR("{}", line);
			else
				BW_INFO("{}", line);
		});

		// --- The shared engine API (ScriptApiDefinition.h) --------------------
		PythonApi api(m);
		DefineScriptApi(api);

		// --- Python-only extras -----------------------------------------------
		api.Get<ScriptMaterial>()
			.def("__repr__", [](const ScriptMaterial& material) { return "Material('" + material.Instance->GetName() + "')"; });

		api.Get<ScriptRef>()
			.def_property_readonly("fields", [](const ScriptRef& r)
			{
				py::dict fields;
				for (const auto& [name, value] : r.Get().Fields)
					fields[py::str(name)] = ScriptFieldToPython(value);
				return fields;
			})
			.def("set_field", [](const ScriptRef& r, const std::string& name, const py::object& value)
			{
				ScriptFieldValue field;
				if (py::isinstance<py::bool_>(value)) field = value.cast<bool>();
				else if (py::isinstance<py::int_>(value)) field = value.cast<int64_t>();
				else if (py::isinstance<py::float_>(value)) field = value.cast<double>();
				else if (py::isinstance<py::str>(value)) field = value.cast<std::string>();
				else if (py::isinstance<glm::vec2>(value)) field = value.cast<glm::vec2>();
				else if (py::isinstance<glm::vec3>(value)) field = value.cast<glm::vec3>();
				else if (py::isinstance<glm::vec4>(value)) field = value.cast<glm::vec4>();
				else throw py::type_error("Script fields must be bool, int, float, str, Vec2, Vec3 or Vec4");
				r.Get().Fields[name] = field;
			}, py::arg("name"), py::arg("value"), "Overrides an exposed field (applies when the script instance is created)")
			.def_property_readonly("instance", [](const ScriptRef& r) { r.Get(); return ScriptInstanceOrNone(r.Owner.ID); },
				"The running script object (play mode), or None");

		api.Get<ScriptEntity>()
			.def_property_readonly("script", [](const ScriptEntity& e) { e.Resolve(); return ScriptInstanceOrNone(e.ID); },
				"The entity's running script object, or None")
			.def("__eq__", [](const ScriptEntity& a, const py::object& b)
			{
				return py::isinstance<ScriptEntity>(b) && a == b.cast<ScriptEntity>();
			})
			.def("__hash__", [](const ScriptEntity& e) { return std::hash<uint64_t>()(static_cast<uint64_t>(e.ID)); })
			.def("__bool__", &ScriptEntity::IsValid)
			.def("__repr__", [](const ScriptEntity& e)
			{
				Entity entity = e.TryResolve();
				return entity ? "Entity('" + entity.GetName() + "', id=" + std::to_string(static_cast<uint64_t>(e.ID)) + ")" : std::string("Entity(<destroyed>)");
			});

		// Python-only: a variable number of values doesn't fit the shared API description, so ForgeScript
		// has 'trigger' instead. The event reaches every script in either language, the sender included.
		m.def("emit", [](const std::string& name, const py::args& values)
		{
			ScriptValueList converted;
			for (const py::handle value : values)
				converted.push_back(ScriptValueFromPython(value));
			EmitScriptEvent(name, converted);
		}, py::arg("name"), "Sends an event to every script: Python's on_event(self, name, *values) and ForgeScript's 'when <name>' handlers");

		api.Get<RaycastHit>()
			.def("__repr__", [](const RaycastHit& hit) { return fmt::format("RaycastHit(distance={:.3f})", hit.Distance); });

		m.attr("VERSION") = BW_VERSION;
	}

}

PYBIND11_EMBEDDED_MODULE(_bellows, module)
{
	Bellows::Scripting::RegisterBindings(module);
}
