#include "ForgeScript/VM/VM.h"

#include "ForgeScript/Core/Easing.h"
#include "ForgeScript/Core/Utf8.h"
#include "ForgeScript/Tools/Json.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// The ForgeScript standard library: functions every script can use, plus the functions
// that belong to lists, text, maps and vectors.

namespace ForgeScript {

	// ======================================================================= Turning values into text

	std::string VM::ToText(const Value& value)
	{
		return Describe(value, false);
	}

	std::string VM::Describe(const Value& value, bool quoteText)
	{
		std::string text;
		if (!DescribeInto(value, quoteText, text) && m_TextLimit >= c_MaxTextBytes)
			throw RuntimeError("Turning this into text would make more than " + FormatNumber(static_cast<double>(c_MaxTextBytes)) +
				" bytes of text. Does a list or map hold the same big part many times?");
		return text;
	}

	// Appends the text of a value to "out". Returns false once "out" is longer than m_TextLimit: the
	// caller stops there, so a value that would make gigabytes of text costs no more than the limit.
	bool VM::DescribeInto(const Value& value, bool quoteText, std::string& out)
	{
		switch (value.GetType())
		{
			case ValueType::Nothing: out += "nothing"; return out.size() <= m_TextLimit;
			case ValueType::Boolean: out += value.AsBool() ? "true" : "false"; return out.size() <= m_TextLimit;
			case ValueType::Number:  out += FormatNumber(value.AsNumber()); return out.size() <= m_TextLimit;
			case ValueType::Object:  break;
		}

		// A very deep value is shortened.
		if (m_TextDepth > 16)
		{
			out += "...";
			return out.size() <= m_TextLimit;
		}
		struct DepthGuard
		{
			int& Depth;
			explicit DepthGuard(int& depth) : Depth(depth) { Depth++; }
			~DepthGuard() { Depth--; }
		} guard(m_TextDepth);

		Object* object = value.AsObject();
		ObjectType type = object->GetType();

		// A list, map or object that holds itself shows "[...]" where it appears again inside itself.
		// An object with its own "to_text" is left out: a "to_text" that uses itself forever stays a
		// mistake, as it always was.
		Closure* customText = nullptr;
		if (type == ObjectType::Instance && !m_PlainText)
		{
			customText = static_cast<Instance*>(object)->Class->FindMethod(Intern("to_text"));
			if (customText && customText->Fn->Arity != 0)
				customText = nullptr;
		}
		bool container = type == ObjectType::List || type == ObjectType::Map || (type == ObjectType::Instance && !customText);
		if (container && std::find(m_TextOpen.begin(), m_TextOpen.end(), object) != m_TextOpen.end())
		{
			if (type == ObjectType::List)
				out += "[...]";
			else if (type == ObjectType::Map)
				out += "{...}";
			else
				out += static_cast<Instance*>(object)->Class->Name->Text + " {...}";
			return out.size() <= m_TextLimit;
		}
		struct OpenGuard
		{
			std::vector<Object*>& Open;
			bool Pushed;
			OpenGuard(std::vector<Object*>& open, Object* object, bool push) : Open(open), Pushed(push) { if (Pushed) Open.push_back(object); }
			~OpenGuard() { if (Pushed) Open.pop_back(); }
		} open(m_TextOpen, object, container);

		switch (type)
		{
			case ObjectType::String:
			{
				const std::string& text = static_cast<String*>(object)->Text;
				if (quoteText)
					out += '"';
				// Copy no more than the limit allows, so a huge text costs no more than the limit.
				size_t room = m_TextLimit >= out.size() ? m_TextLimit - out.size() + 1 : 0;
				out.append(text, 0, std::min(text.size(), room));
				if (quoteText && text.size() < room)
					out += '"';
				return out.size() <= m_TextLimit;
			}
			case ObjectType::List:
			{
				out += '[';
				const std::vector<Value>& items = static_cast<List*>(object)->Items;
				for (size_t i = 0; i < items.size(); i++)
				{
					if (i > 0)
						out += ", ";
					if (!DescribeInto(items[i], true, out))
						return false;
				}
				out += ']';
				return out.size() <= m_TextLimit;
			}
			case ObjectType::Map:
			{
				out += '{';
				const std::vector<Map::Entry>& entries = static_cast<Map*>(object)->Entries;
				for (size_t i = 0; i < entries.size(); i++)
				{
					if (i > 0)
						out += ", ";
					if (!DescribeInto(entries[i].Key, true, out))
						return false;
					out += ": ";
					if (!DescribeInto(entries[i].Item, true, out))
						return false;
				}
				out += '}';
				return out.size() <= m_TextLimit;
			}
			case ObjectType::Vector:
			{
				Vector* v = static_cast<Vector*>(object);
				out += "(" + FormatNumber(v->X) + ", " + FormatNumber(v->Y) + ", " + FormatNumber(v->Z) + ")";
				return out.size() <= m_TextLimit;
			}
			case ObjectType::Function:
				out += "<function " + (static_cast<Function*>(object)->Name ? static_cast<Function*>(object)->Name->Text : std::string("?")) + ">";
				return out.size() <= m_TextLimit;
			case ObjectType::Closure:
			{
				Function* fn = static_cast<Closure*>(object)->Fn;
				out += "<function " + (fn->Name ? fn->Name->Text : std::string("?")) + ">";
				return out.size() <= m_TextLimit;
			}
			case ObjectType::NativeFunction:
				out += "<function " + static_cast<NativeFunction*>(object)->Name + ">";
				return out.size() <= m_TextLimit;
			case ObjectType::BoundMethod:
			{
				Object* method = static_cast<BoundMethod*>(object)->Method;
				if (method->GetType() == ObjectType::Closure)
					return DescribeInto(Value::FromObject(method), false, out);
				out += "<function " + static_cast<NativeFunction*>(method)->Name + ">";
				return out.size() <= m_TextLimit;
			}
			case ObjectType::Kind:
				out += "<kind " + static_cast<Kind*>(object)->Name->Text + ">";
				return out.size() <= m_TextLimit;
			case ObjectType::Instance:
			{
				Instance* instance = static_cast<Instance*>(object);
				// A kind can describe itself with a "to_text" function.
				if (customText)
				{
					if (m_TextDepth > 8)
						throw RuntimeError("'to_text' of " + instance->Class->Name->Text + " keeps turning itself into text forever.");
					Value bound = Value::FromObject(Allocate<BoundMethod>(0, value, customText));
					Handle keep(*this, bound);
					Value result = CallFromNative(bound, {});
					return DescribeInto(result, false, out);
				}
				out += instance->Class->Name->Text + " {";
				for (size_t i = 0; i < instance->FieldOrder.size(); i++)
				{
					String* name = instance->FieldOrder[i];
					if (i > 0)
						out += ", ";
					out += name->Text + ": ";
					if (!DescribeInto(instance->Fields[name], true, out))
						return false;
				}
				out += '}';
				return out.size() <= m_TextLimit;
			}
			case ObjectType::NativeObject:
			{
				NativeObject* native = static_cast<NativeObject*>(object);
				if (native->Type->ToText)
					out += native->Type->ToText(native->Data);
				else
					out += "<" + native->Type->Name + ">";
				return out.size() <= m_TextLimit;
			}
			case ObjectType::Module:
				out += "<script " + static_cast<Module*>(object)->Name + ">";
				return out.size() <= m_TextLimit;
			case ObjectType::Upvalue:
				out += "<upvalue>";
				return out.size() <= m_TextLimit;
			case ObjectType::Task:
				out += "<task>";
				return out.size() <= m_TextLimit;
		}
		out += "?";
		return out.size() <= m_TextLimit;
	}

	// ======================================================================= Helpers

	namespace {

		double ToNumberOr(const std::string& text, bool& ok)
		{
			ok = false;
			std::string trimmed = text;
			while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
				trimmed.pop_back();
			size_t start = 0;
			while (start < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[start])))
				start++;
			trimmed = trimmed.substr(start);
			if (trimmed.empty())
				return 0;
			char* end = nullptr;
			double number = std::strtod(trimmed.c_str(), &end);
			ok = end && *end == '\0';
			return number;
		}

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return text;
		}

		std::string Upper(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			return text;
		}

		std::string Trim(const std::string& text)
		{
			size_t start = 0;
			size_t end = text.size();
			while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
				start++;
			while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
				end--;
			return text.substr(start, end - start);
		}

		// Converts a 1 based position (negative = from the end) for slicing; clamps instead of failing.
		long long ClampPosition(double position, size_t size)
		{
			long long index = static_cast<long long>(position);
			if (index < 0)
				index = static_cast<long long>(size) + index + 1;
			return std::clamp<long long>(index, 1, static_cast<long long>(size) + 1);
		}

		size_t RequirePosition(CallContext& context, size_t argIndex, size_t size, bool allowEnd)
		{
			int64_t position = context.Integer(argIndex);
			if (position < 0)
				position = static_cast<int64_t>(size) + position + 1;
			int64_t limit = static_cast<int64_t>(size) + (allowEnd ? 1 : 0);
			if (position < 1 || position > limit)
			{
				context.Error("Position " + std::to_string(context.Integer(argIndex)) + " is outside the list, which has " +
					std::to_string(size) + (size == 1 ? " item." : " items."));
			}
			return static_cast<size_t>(position - 1);
		}

		bool LessThan(CallContext& context, const Value& a, const Value& b)
		{
			if (a.IsNumber() && b.IsNumber())
				return a.AsNumber() < b.AsNumber();
			if (IsObjectType(a, ObjectType::String) && IsObjectType(b, ObjectType::String))
				return As<String>(a)->Text < As<String>(b)->Text;
			context.Error("sort can only order numbers with numbers or text with text. Use sort_by with a function for anything else.");
		}

		// ---- Dates. Calendar math from days since 1970 (Howard Hinnant's civil-from-days), which is
		// the same on every platform; only local time asks the system for the time zone.

		// The seconds date functions accept: from 1970 to the end of 9999.
		constexpr double c_LastDateSecond = 253402300799.0;

		struct CivilDate
		{
			int64_t Year = 1970;
			int Month = 1;  // 1 to 12
			int Day = 1;    // 1 to 31
			int Hour = 0;
			int Minute = 0;
			int Second = 0;
			int Weekday = 4; // 1 = Monday ... 7 = Sunday (1970-01-01 was a Thursday)
			int DayOfYear = 1;
		};

		int64_t DaysFromCivil(int64_t year, int month, int day)
		{
			year -= month <= 2;
			const int64_t era = (year >= 0 ? year : year - 399) / 400;
			const int64_t yearOfEra = year - era * 400;
			const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
			const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
			return era * 146097 + dayOfEra - 719468;
		}

		CivilDate CivilFromSeconds(int64_t seconds)
		{
			CivilDate date;
			int64_t days = seconds / 86400;
			int64_t rest = seconds % 86400;
			if (rest < 0)
			{
				rest += 86400;
				days--;
			}
			date.Hour = static_cast<int>(rest / 3600);
			date.Minute = static_cast<int>(rest / 60 % 60);
			date.Second = static_cast<int>(rest % 60);
			date.Weekday = static_cast<int>(((days % 7) + 7 + 3) % 7) + 1;

			const int64_t z = days + 719468;
			const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
			const int64_t dayOfEra = z - era * 146097;
			const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
			const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
			const int64_t monthIndex = (5 * dayOfYear + 2) / 153;
			date.Day = static_cast<int>(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
			date.Month = static_cast<int>(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
			date.Year = yearOfEra + era * 400 + (date.Month <= 2);
			date.DayOfYear = static_cast<int>(days - DaysFromCivil(date.Year, 1, 1)) + 1;
			return date;
		}

		// The offset of local time from UTC at that moment, in seconds (daylight saving included).
		int64_t LocalOffsetSeconds(int64_t seconds)
		{
			const std::time_t moment = static_cast<std::time_t>(seconds);
			std::tm local {};
#ifdef _WIN32
			if (localtime_s(&local, &moment) != 0)
				return 0;
#else
			if (!localtime_r(&moment, &local))
				return 0;
#endif
			const int64_t localDays = DaysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
			const int64_t localSeconds = localDays * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
			return localSeconds - seconds;
		}

		// The seconds argument at 'index' (or now, when there is none), checked: a date function's
		// moment must be a real time between 1970 and the end of 9999.
		int64_t DateSeconds(CallContext& c, size_t index, const char* name)
		{
			double seconds;
			if (c.Count() <= index)
				seconds = std::floor(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0);
			else
				seconds = c.Number(index);
			if (!std::isfinite(seconds) || seconds < 0 || seconds > c_LastDateSecond)
				c.Error(std::string(name) + " needs a moment from real_time(): seconds from 1970 to the end of 9999, but got " +
					FormatNumber(seconds) + ".");
			return static_cast<int64_t>(std::floor(seconds));
		}

		CivilDate LocalDate(int64_t seconds)
		{
			return CivilFromSeconds(seconds + LocalOffsetSeconds(seconds));
		}

		const char* const c_MonthNames[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
			"September", "October", "November", "December" };
		const char* const c_WeekdayNames[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

		Value DateMap(VM& vm, const CivilDate& date)
		{
			Value map = vm.MakeMap();
			Map* fields = As<Map>(map);
			auto set = [&vm, fields](const char* key, const Value& value) { fields->Set(vm.MakeText(key), value); };
			set("year", Value::FromNumber(static_cast<double>(date.Year)));
			set("month", Value::FromNumber(date.Month));
			set("day", Value::FromNumber(date.Day));
			set("hour", Value::FromNumber(date.Hour));
			set("minute", Value::FromNumber(date.Minute));
			set("second", Value::FromNumber(date.Second));
			set("weekday", vm.MakeText(c_WeekdayNames[date.Weekday - 1]));
			set("weekday_number", Value::FromNumber(date.Weekday));
			set("month_name", vm.MakeText(c_MonthNames[date.Month - 1]));
			set("day_of_year", Value::FromNumber(date.DayOfYear));
			return map;
		}

		// format_date's pattern words, longest first so "Month" isn't read as "Mon" + "th".
		std::string FormatDate(const CivilDate& date, std::string_view pattern)
		{
			auto twoDigits = [](int n) { return (n < 10 ? "0" : "") + std::to_string(n); };
			std::string text;
			size_t at = 0;
			while (at < pattern.size())
			{
				const std::string_view rest = pattern.substr(at);
				auto starts = [&rest](std::string_view word) { return rest.substr(0, word.size()) == word; };
				if (starts("YYYY")) { text += std::to_string(date.Year); at += 4; }
				else if (starts("Weekday")) { text += c_WeekdayNames[date.Weekday - 1]; at += 7; }
				else if (starts("Month")) { text += c_MonthNames[date.Month - 1]; at += 5; }
				else if (starts("Wkd")) { text += std::string(c_WeekdayNames[date.Weekday - 1]).substr(0, 3); at += 3; }
				else if (starts("Mon")) { text += std::string(c_MonthNames[date.Month - 1]).substr(0, 3); at += 3; }
				else if (starts("MM")) { text += twoDigits(date.Month); at += 2; }
				else if (starts("DD")) { text += twoDigits(date.Day); at += 2; }
				else if (starts("hh")) { text += twoDigits(date.Hour); at += 2; }
				else if (starts("mm")) { text += twoDigits(date.Minute); at += 2; }
				else if (starts("ss")) { text += twoDigits(date.Second); at += 2; }
				else { text += pattern[at]; at++; }
			}
			return text;
		}

		// A number argument that must be finite: infinity, or a not-a-number made from it, would turn into
		// an int with undefined behavior inside noise and hsv, and means nothing for a point or a curve.
		double FiniteNumber(CallContext& c, size_t index, const char* name)
		{
			const double number = c.Number(index);
			if (!std::isfinite(number))
				c.Error(std::string(name) + " needs ordinary numbers, but value " + std::to_string(index + 1) + " is " +
					(std::isnan(number) ? "not a number." : "infinity."));
			return number;
		}

		// Ken Perlin's improved noise (2002), from his reference permutation, so the same position gives
		// the same value on every machine and every run.
		double PerlinNoise(double x, double y, double z)
		{
			static const uint8_t c_Permutation[256] = {
				151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10,
				23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87,
				174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211,
				133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208,
				89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5,
				202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119,
				248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232,
				178, 185, 112, 104, 218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249,
				14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205,
				93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180 };
			auto at = [](int i) { return static_cast<int>(c_Permutation[i & 255]); };
			auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
			auto mix = [](double t, double a, double b) { return a + t * (b - a); };
			auto gradient = [](int hash, double gx, double gy, double gz)
			{
				const int h = hash & 15;
				const double u = h < 8 ? gx : gy;
				const double v = h < 4 ? gy : (h == 12 || h == 14 ? gx : gz);
				return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
			};
			const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
			// Wrapping to 0..255 first keeps huge coordinates from overflowing an int.
			const int X = static_cast<int>(std::fmod(fx, 256.0) + 256.0) & 255;
			const int Y = static_cast<int>(std::fmod(fy, 256.0) + 256.0) & 255;
			const int Z = static_cast<int>(std::fmod(fz, 256.0) + 256.0) & 255;
			x -= fx;
			y -= fy;
			z -= fz;
			const double u = fade(x), v = fade(y), w = fade(z);
			const int A = at(X) + Y, AA = at(A) + Z, AB = at(A + 1) + Z;
			const int B = at(X + 1) + Y, BA = at(B) + Z, BB = at(B + 1) + Z;
			const double result = mix(w,
				mix(v, mix(u, gradient(at(AA), x, y, z), gradient(at(BA), x - 1, y, z)),
					mix(u, gradient(at(AB), x, y - 1, z), gradient(at(BB), x - 1, y - 1, z))),
				mix(v, mix(u, gradient(at(AA + 1), x, y, z - 1), gradient(at(BA + 1), x - 1, y, z - 1)),
					mix(u, gradient(at(AB + 1), x, y - 1, z - 1), gradient(at(BB + 1), x - 1, y - 1, z - 1))));
			return std::clamp(result, -1.0, 1.0);
		}

		// "#ff8800", "ff8800", "#f80" and the same with alpha ("#ff880080", "#f808"), as 0 to 1 values.
		bool ParseHexColor(std::string_view text, double outRgba[4])
		{
			if (!text.empty() && text.front() == '#')
				text.remove_prefix(1);
			if (text.size() != 3 && text.size() != 4 && text.size() != 6 && text.size() != 8)
				return false;
			const bool shortForm = text.size() <= 4;
			const size_t channels = shortForm ? text.size() : text.size() / 2;
			outRgba[3] = 1.0;
			for (size_t i = 0; i < channels; i++)
			{
				const std::string_view digits = shortForm ? text.substr(i, 1) : text.substr(i * 2, 2);
				unsigned value = 0;
				const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
				if (error != std::errc() || end != digits.data() + digits.size())
					return false;
				outRgba[i] = shortForm ? value * 17 / 255.0 : value / 255.0;
			}
			return true;
		}

	}

	void VM::RegisterMethod(std::unordered_map<String*, NativeFunction*>& table, const std::string& name, int arity, NativeFn fn)
	{
		NativeFunction* native = Allocate<NativeFunction>(name.size(), name, arity, std::move(fn));
		table[Intern(name)] = native;
	}

	// ======================================================================= Registration

	void VM::RegisterLibrary()
	{
		GCPause pause(*this);

		// ------------------------------------------------------------------ Output

		RegisterFunction("print", AnyArity, [](CallContext& c)
		{
			std::string line;
			for (size_t i = 0; i < c.Count(); i++)
			{
				if (i > 0)
					line += ' ';
				line += c.GetVM().ToText(c.Arg(i));
			}
			c.GetVM().Print(line);
			return Value();
		});
		RegisterPhrase("say (message)", [](CallContext& c)
		{
			c.GetVM().Print(c.GetVM().ToText(c.Arg(0)));
			return Value();
		});
		RegisterFunction("show", 1, [](CallContext& c)
		{
			if (!c.Arg(0).IsNothing())
				c.GetVM().Print(c.GetVM().Describe(c.Arg(0), true));
			return Value();
		});

		// ------------------------------------------------------------------ Types and conversion

		RegisterFunction("text", 1, [](CallContext& c) { return c.GetVM().MakeText(c.GetVM().ToText(c.Arg(0))); });
		RegisterFunction("number", 1, [](CallContext& c)
		{
			Value value = c.Arg(0);
			if (value.IsNumber())
				return value;
			if (value.IsBool())
				return Value::FromNumber(value.AsBool() ? 1 : 0);
			if (IsObjectType(value, ObjectType::String))
			{
				bool ok = false;
				double number = ToNumberOr(As<String>(value)->Text, ok);
				return ok ? Value::FromNumber(number) : Value();
			}
			return Value();
		});
		RegisterFunction("type", 1, [](CallContext& c) { return c.GetVM().MakeText(c.GetVM().TypeOf(c.Arg(0))); });
		RegisterFunction("length", 1, [](CallContext& c)
		{
			Value value = c.Arg(0);
			if (IsObjectType(value, ObjectType::String))
				return Value::FromNumber(static_cast<double>(Utf8::Length(As<String>(value)->Text)));
			if (IsObjectType(value, ObjectType::List))
				return Value::FromNumber(static_cast<double>(As<List>(value)->Items.size()));
			if (IsObjectType(value, ObjectType::Map))
				return Value::FromNumber(static_cast<double>(As<Map>(value)->Size()));
			if (value.IsNothing())
				return Value::FromNumber(0);
			c.Error("length needs text, a list or a map, but got " + c.GetVM().TypeOf(value) + ".");
		});
		RegisterFunction("copy", 1, [](CallContext& c)
		{
			VM& vm = c.GetVM();
			Value value = c.Arg(0);
			if (IsObjectType(value, ObjectType::List))
				return vm.MakeList(As<List>(value)->Items);
			if (IsObjectType(value, ObjectType::Map))
			{
				Value result = vm.MakeMap();
				for (const Map::Entry& entry : As<Map>(value)->Entries)
					As<Map>(result)->Set(entry.Key, entry.Item);
				return result;
			}
			if (IsObjectType(value, ObjectType::Vector))
			{
				Vector* v = As<Vector>(value);
				return vm.MakeVector(v->X, v->Y, v->Z);
			}
			return value;
		});
		RegisterFunction("assert", AnyArity, [](CallContext& c)
		{
			if (c.Count() < 1 || c.Count() > 2)
				c.Error("assert needs a condition and an optional message.");
			if (!c.Arg(0).IsTruthy())
				c.Error(c.Count() > 1 ? c.GetVM().ToText(c.Arg(1)) : std::string("Assertion failed."));
			return Value();
		});

		// ------------------------------------------------------------------ Math

		SetSharedGlobal("pi", Value::FromNumber(3.14159265358979323846));
		SetSharedGlobal("infinity", Value::FromNumber(INFINITY));

		auto unary = [this](const char* name, double (*fn)(double))
		{
			RegisterFunction(name, 1, [fn](CallContext& c) { return Value::FromNumber(fn(c.Number(0))); });
		};
		unary("abs", [](double x) { return std::fabs(x); });
		unary("floor", [](double x) { return std::floor(x); });
		unary("ceil", [](double x) { return std::ceil(x); });
		unary("sin", [](double x) { return std::sin(x); });
		unary("cos", [](double x) { return std::cos(x); });
		unary("tan", [](double x) { return std::tan(x); });
		unary("asin", [](double x) { return std::asin(x); });
		unary("acos", [](double x) { return std::acos(x); });
		unary("atan", [](double x) { return std::atan(x); });
		unary("exp", [](double x) { return std::exp(x); });
		unary("sign", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); });
		unary("radians", [](double x) { return x * 3.14159265358979323846 / 180.0; });
		unary("degrees", [](double x) { return x * 180.0 / 3.14159265358979323846; });

		RegisterFunction("sqrt", 1, [](CallContext& c)
		{
			double x = c.Number(0);
			if (x < 0)
				c.Error("Cannot take the square root of a negative number.");
			return Value::FromNumber(std::sqrt(x));
		});
		RegisterFunction("log", AnyArity, [](CallContext& c)
		{
			double x = c.Number(0);
			if (x <= 0)
				c.Error("log needs a number above 0.");
			if (c.Count() > 1)
				return Value::FromNumber(std::log(x) / std::log(c.Number(1)));
			return Value::FromNumber(std::log(x));
		});
		RegisterFunction("round", AnyArity, [](CallContext& c)
		{
			double x = c.Number(0);
			if (c.Count() > 1)
			{
				double scale = std::pow(10.0, static_cast<double>(c.Integer(1)));
				return Value::FromNumber(std::round(x * scale) / scale);
			}
			return Value::FromNumber(std::round(x));
		});
		// For scores and money on a HUD: "1,250" and "1,250.50". The digits come from to_chars, which
		// never depends on the computer's language settings.
		RegisterFunction("format_number", AnyArity, [](CallContext& c)
		{
			const double x = c.Number(0);
			const int64_t decimals = c.Count() > 1 ? c.Integer(1) : 0;
			if (decimals < 0 || decimals > 10)
				c.Error("format_number needs 0 to 10 decimals, but got " + std::to_string(decimals) + ".");
			if (!std::isfinite(x))
				return c.GetVM().MakeText(FormatNumber(x));
			char buffer[400];
			const auto [end, problem] = std::to_chars(buffer, buffer + sizeof(buffer), x, std::chars_format::fixed, static_cast<int>(decimals));
			if (problem != std::errc())
				return c.GetVM().MakeText(FormatNumber(x));
			std::string digits(buffer, end);
			std::string sign;
			if (!digits.empty() && digits.front() == '-')
			{
				digits.erase(0, 1);
				// Rounding -0.004 to two decimals gives "-0.00"; a zero has no sign.
				if (digits.find_first_not_of("0.") != std::string::npos)
					sign = "-";
			}
			const size_t point = digits.find('.');
			std::string whole = digits.substr(0, point);
			const std::string fraction = point == std::string::npos ? std::string() : digits.substr(point);
			std::string grouped;
			for (size_t i = 0; i < whole.size(); i++)
			{
				if (i > 0 && (whole.size() - i) % 3 == 0)
					grouped += ',';
				grouped += whole[i];
			}
			return c.GetVM().MakeText(sign + grouped + fraction);
		});
		// For timers: "1:05", or "1:02:03" from an hour on. Parts of a second are dropped, so a countdown
		// shows 0:00 only when it is really over.
		RegisterFunction("format_time", 1, [](CallContext& c)
		{
			const double seconds = c.Number(0);
			if (!std::isfinite(seconds))
				c.Error("format_time needs a number of seconds, but got " + FormatNumber(seconds) + ".");
			const double whole = std::floor(std::abs(seconds));
			if (whole > 9.0e15)
				c.Error("format_time needs fewer seconds than that.");
			const auto total = static_cast<int64_t>(whole);
			const int64_t hours = total / 3600;
			const int64_t minutes = (total / 60) % 60;
			const int64_t rest = total % 60;
			auto twoDigits = [](int64_t n) { return (n < 10 ? "0" : "") + std::to_string(n); };
			std::string text = seconds < 0 && total > 0 ? "-" : "";
			if (hours > 0)
				text += std::to_string(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(rest);
			else
				text += std::to_string(minutes) + ":" + twoDigits(rest);
			return c.GetVM().MakeText(text);
		});
		// ------------------------------------------------------------------ Game math (like GDScript's)

		// Moves from 'from' toward 'to' by at most 'step', never past it: a number, or a vector moving in a
		// straight line. Called every frame, it gives steady movement that stops exactly on the target.
		RegisterFunction("move_toward", 3, [](CallContext& c)
		{
			const double step = c.Number(2);
			if (step < 0)
				c.Error("move_toward needs a step of 0 or more, but got " + FormatNumber(step) + ".");
			if (IsObjectType(c.Arg(0), ObjectType::Vector) || IsObjectType(c.Arg(1), ObjectType::Vector))
			{
				Vector* from = c.VectorArg(0);
				Vector* to = c.VectorArg(1);
				const double dx = to->X - from->X, dy = to->Y - from->Y, dz = to->Z - from->Z;
				const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (distance <= step || distance == 0)
					return c.GetVM().MakeVector(to->X, to->Y, to->Z);
				const double t = step / distance;
				return c.GetVM().MakeVector(from->X + dx * t, from->Y + dy * t, from->Z + dz * t);
			}
			const double from = c.Number(0);
			const double to = c.Number(1);
			if (std::abs(to - from) <= step)
				return Value::FromNumber(to);
			return Value::FromNumber(from + (to > from ? step : -step));
		});
		// Where 'value' sits between 'a' and 'b': 0 at a, 1 at b (lerp backwards).
		RegisterFunction("inverse_lerp", 3, [](CallContext& c)
		{
			const double a = c.Number(0);
			const double b = c.Number(1);
			if (a == b)
				c.Error("inverse_lerp needs two different numbers to measure between, but got " + FormatNumber(a) + " twice.");
			return Value::FromNumber((c.Number(2) - a) / (b - a));
		});
		// Moves 'value' from one range to another: remap(5, 0, 10, 0, 100) is 50.
		RegisterFunction("remap", 5, [](CallContext& c)
		{
			const double fromLow = c.Number(1);
			const double fromHigh = c.Number(2);
			if (fromLow == fromHigh)
				c.Error("remap needs a range with two different ends to map from, but got " + FormatNumber(fromLow) + " twice.");
			const double t = (c.Number(0) - fromLow) / (fromHigh - fromLow);
			const double toLow = c.Number(3);
			return Value::FromNumber(toLow + (c.Number(4) - toLow) * t);
		});
		// 0 before 'a', 1 after 'b', and an S-shaped curve between: an easing that starts and stops gently.
		RegisterFunction("smoothstep", 3, [](CallContext& c)
		{
			const double a = c.Number(0);
			const double b = c.Number(1);
			const double x = c.Number(2);
			if (a == b)
				return Value::FromNumber(x < a ? 0.0 : 1.0);
			const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
			return Value::FromNumber(t * t * (3.0 - 2.0 * t));
		});
		// Keeps 'value' inside low to high by going around, like an angle or a looping index:
		// wrap(370, 0, 360) is 10. 'high' itself wraps to 'low'.
		RegisterFunction("wrap", 3, [](CallContext& c)
		{
			const double value = c.Number(0);
			const double low = c.Number(1);
			const double high = c.Number(2);
			if (low == high)
				return Value::FromNumber(low);
			if (low > high)
				c.Error("wrap needs the low end first, like: wrap(angle, 0, 360)");
			const double size = high - low;
			double wrapped = std::fmod(value - low, size);
			if (wrapped < 0)
				wrapped += size;
			return Value::FromNumber(low + wrapped);
		});
		// The nearest multiple of 'step': snapped(17, 5) is 15, for grids. A step of 0 leaves it alone.
		RegisterFunction("snapped", 2, [](CallContext& c)
		{
			const double value = c.Number(0);
			const double step = c.Number(1);
			if (step == 0)
				return Value::FromNumber(value);
			return Value::FromNumber(std::round(value / step) * step);
		});
		RegisterFunction("atan2", 2, [](CallContext& c) { return Value::FromNumber(std::atan2(c.Number(0), c.Number(1))); });
		RegisterFunction("clamp", 3, [](CallContext& c)
		{
			double low = c.Number(1);
			double high = c.Number(2);
			if (low > high)
				std::swap(low, high);
			return Value::FromNumber(std::clamp(c.Number(0), low, high));
		});
		RegisterFunction("lerp", 3, [](CallContext& c)
		{
			double t = c.Number(2);
			if (IsObjectType(c.Arg(0), ObjectType::Vector))
			{
				Vector* a = c.VectorArg(0);
				Vector* b = c.VectorArg(1);
				return c.GetVM().MakeVector(a->X + (b->X - a->X) * t, a->Y + (b->Y - a->Y) * t, a->Z + (b->Z - a->Z) * t);
			}
			double a = c.Number(0);
			return Value::FromNumber(a + (c.Number(1) - a) * t);
		});
		auto minMax = [this](const char* name, bool isMin)
		{
			RegisterFunction(name, AnyArity, [isMin, name](CallContext& c)
			{
				std::vector<Value> values;
				if (c.Count() == 1 && IsObjectType(c.Arg(0), ObjectType::List))
					values = c.ListArg(0)->Items;
				else
					for (size_t i = 0; i < c.Count(); i++)
						values.push_back(c.Arg(i));
				if (values.empty())
					c.Error(std::string(name) + " needs at least one number.");
				double best = 0;
				for (size_t i = 0; i < values.size(); i++)
				{
					if (!values[i].IsNumber())
						c.Error(std::string(name) + " only works with numbers.");
					double x = values[i].AsNumber();
					if (i == 0 || (isMin ? x < best : x > best))
						best = x;
				}
				return Value::FromNumber(best);
			});
		};
		minMax("min", true);
		minMax("max", false);

		// ------------------------------------------------------------------ Random numbers

		// The same seed gives the same random numbers every time, for levels made from a seed and for tests.
		RegisterFunction("random_seed", 1, [](CallContext& c)
		{
			c.GetVM().GetRandom().seed(static_cast<uint64_t>(c.Integer(0)));
			return Value();
		});

		RegisterFunction("random", AnyArity, [](CallContext& c)
		{
			std::mt19937_64& random = c.GetVM().GetRandom();
			if (c.Count() == 0)
				return Value::FromNumber(std::uniform_real_distribution<double>(0.0, 1.0)(random));
			int64_t low = 1;
			int64_t high = c.Integer(0);
			if (c.Count() >= 2)
			{
				low = c.Integer(0);
				high = c.Integer(1);
			}
			if (low > high)
				std::swap(low, high);
			return Value::FromNumber(static_cast<double>(std::uniform_int_distribution<int64_t>(low, high)(random)));
		});
		RegisterPhrase("random number from (low) to (high)", [](CallContext& c)
		{
			int64_t low = c.Integer(0);
			int64_t high = c.Integer(1);
			if (low > high)
				std::swap(low, high);
			return Value::FromNumber(static_cast<double>(std::uniform_int_distribution<int64_t>(low, high)(c.GetVM().GetRandom())));
		});
		RegisterPhrase("random item from (items)", [](CallContext& c)
		{
			List* list = c.ListArg(0);
			if (list->Items.empty())
				return Value();
			std::uniform_int_distribution<size_t> pick(0, list->Items.size() - 1);
			return list->Items[pick(c.GetVM().GetRandom())];
		});

		// true with chance p (0 never, 1 always): if random_chance(0.25) then drop a coin.
		RegisterFunction("random_chance", 1, [](CallContext& c)
		{
			const double chance = FiniteNumber(c, 0, "random_chance");
			return Value::FromBool(std::uniform_real_distribution<double>(0.0, 1.0)(c.GetVM().GetRandom()) < chance);
		});
		// A direction of length 1, any way round with the same chance.
		RegisterFunction("random_direction", 0, [](CallContext& c)
		{
			std::mt19937_64& random = c.GetVM().GetRandom();
			const double z = std::uniform_real_distribution<double>(-1.0, 1.0)(random);
			const double angle = std::uniform_real_distribution<double>(0.0, 2 * 3.14159265358979323846)(random);
			const double ring = std::sqrt(std::max(0.0, 1 - z * z));
			return c.GetVM().MakeVector(ring * std::cos(angle), ring * std::sin(angle), z);
		});
		// A point on the ground (y is 0) inside a circle around (0, 0, 0), every spot with the same
		// chance: add it to a position to spawn around it.
		RegisterFunction("random_point_in_circle", 1, [](CallContext& c)
		{
			const double radius = FiniteNumber(c, 0, "random_point_in_circle");
			if (radius < 0)
				c.Error("random_point_in_circle needs a radius of 0 or more.");
			std::mt19937_64& random = c.GetVM().GetRandom();
			const double distance = radius * std::sqrt(std::uniform_real_distribution<double>(0.0, 1.0)(random));
			const double angle = std::uniform_real_distribution<double>(0.0, 2 * 3.14159265358979323846)(random);
			return c.GetVM().MakeVector(distance * std::cos(angle), 0, distance * std::sin(angle));
		});

		// ------------------------------------------------------------------ Noise, easing and colors

		// Smooth noise from -1 to 1 that changes gently as the position moves: noise(x), noise(x, y) or
		// noise(x, y, z). The same position always gives the same value (random_seed doesn't change it);
		// add an offset for a different pattern.
		RegisterFunction("noise", AnyArity, [](CallContext& c)
		{
			if (c.Count() < 1 || c.Count() > 3)
				c.Error("noise needs 1, 2 or 3 numbers, like: noise(x) or noise(x, y)");
			if (c.Count() == 1 && c.Arg(0).IsObject() && IsObjectType(c.Arg(0), ObjectType::Vector))
			{
				const Vector* at = c.VectorArg(0);
				if (!std::isfinite(at->X) || !std::isfinite(at->Y) || !std::isfinite(at->Z))
					c.Error("noise needs ordinary numbers, but the position has infinity or not a number in it.");
				return Value::FromNumber(PerlinNoise(at->X, at->Y, at->Z));
			}
			// A left-out coordinate is a fixed fraction, not 0: noise is 0 on whole-number grid lines, so
			// noise(x) along y = 0 would hardly move.
			const double x = FiniteNumber(c, 0, "noise");
			const double y = c.Count() > 1 ? FiniteNumber(c, 1, "noise") : 0.318;
			const double z = c.Count() > 2 ? FiniteNumber(c, 2, "noise") : 0.627;
			return Value::FromNumber(PerlinNoise(x, y, z));
		});
		// ease(t, "out"): t from 0 to 1 bent into a curve, for smooth movement and animation. t is
		// clamped to 0 to 1.
		RegisterFunction("ease", 2, [](CallContext& c)
		{
			const double t = FiniteNumber(c, 0, "ease");
			double result = 0.0;
			if (TryEase(c.Text(1), t, result))
				return Value::FromNumber(result);
			std::string names;
			for (std::string_view name : EaseStyleNames())
				names += std::string(names.empty() ? "" : ", ") + "\"" + std::string(name) + "\"";
			c.Error("'" + c.Text(1) + "' isn't an easing style. The styles are " + names + ".");
		});
		// color(r, g, b [, a]) with 0 to 1 values, or color("#ff8800"): the [r, g, b, a] list engine
		// colors take.
		RegisterFunction("color", AnyArity, [](CallContext& c)
		{
			VM& vm = c.GetVM();
			if (c.Count() == 1 && c.Arg(0).IsObject() && IsObjectType(c.Arg(0), ObjectType::String))
			{
				double rgba[4] = { 0, 0, 0, 1 };
				if (!ParseHexColor(c.Text(0), rgba))
					c.Error("'" + c.Text(0) + "' isn't a color. Write it like \"#ff8800\" (red, green, blue) or \"#ff880080\" (with alpha).");
				return vm.MakeList({ Value::FromNumber(rgba[0]), Value::FromNumber(rgba[1]), Value::FromNumber(rgba[2]), Value::FromNumber(rgba[3]) });
			}
			if (c.Count() < 3 || c.Count() > 4)
				c.Error("color needs red, green and blue (and alpha) from 0 to 1, like: color(1, 0.5, 0), or a text like color(\"#ff8800\").");
			return vm.MakeList({ Value::FromNumber(FiniteNumber(c, 0, "color")), Value::FromNumber(FiniteNumber(c, 1, "color")),
				Value::FromNumber(FiniteNumber(c, 2, "color")), Value::FromNumber(c.Count() > 3 ? FiniteNumber(c, 3, "color") : 1.0) });
		});
		// hsv(hue, saturation, value [, alpha]), each from 0 to 1, as an [r, g, b, a] color. The hue goes
		// round the color wheel: 0 red, 1/3 green, 2/3 blue, and 1 red again.
		RegisterFunction("hsv", AnyArity, [](CallContext& c)
		{
			if (c.Count() < 3 || c.Count() > 4)
				c.Error("hsv needs hue, saturation and value (and alpha) from 0 to 1, like: hsv(0.6, 1, 1)");
			double hue = std::fmod(FiniteNumber(c, 0, "hsv"), 1.0);
			if (hue < 0)
				hue += 1.0;
			const double saturation = std::clamp(FiniteNumber(c, 1, "hsv"), 0.0, 1.0);
			const double value = std::clamp(FiniteNumber(c, 2, "hsv"), 0.0, 1.0);
			const double sector = hue * 6;
			const int index = static_cast<int>(sector) % 6;
			const double part = sector - std::floor(sector);
			const double p = value * (1 - saturation);
			const double q = value * (1 - saturation * part);
			const double t = value * (1 - saturation * (1 - part));
			const double table[6][3] = { { value, t, p }, { q, value, p }, { p, value, t }, { p, q, value }, { t, p, value }, { value, p, q } };
			return c.GetVM().MakeList({ Value::FromNumber(table[index][0]), Value::FromNumber(table[index][1]),
				Value::FromNumber(table[index][2]), Value::FromNumber(c.Count() > 3 ? FiniteNumber(c, 3, "hsv") : 1.0) });
		});

		// ------------------------------------------------------------------ Time

		RegisterFunction("time", 0, [](CallContext& c) { return Value::FromNumber(c.GetVM().GetTime()); });
		RegisterFunction("frame", 0, [](CallContext& c) { return Value::FromNumber(static_cast<double>(c.GetVM().GetFrame())); });

		// ------------------------------------------------------------------ Dates and the real clock

		// Seconds since the start of 1970 (UTC) by the computer's clock. Unlike time() it keeps going
		// while the game is paused or closed: for daily rewards, streaks and save stamps.
		RegisterFunction("real_time", 0, [](CallContext&)
		{
			return Value::FromNumber(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0);
		});
		// The date and time as a map (year, month, day, hour, minute, second, weekday, weekday_number,
		// month_name, day_of_year): now, or at a moment from real_time(). date() is local time,
		// utc_date() is the same everywhere.
		RegisterFunction("date", AnyArity, [](CallContext& c)
		{
			if (c.Count() > 1)
				c.Error("date takes one moment from real_time() at most, like: date(saved_at)");
			return DateMap(c.GetVM(), LocalDate(DateSeconds(c, 0, "date")));
		});
		RegisterFunction("utc_date", AnyArity, [](CallContext& c)
		{
			if (c.Count() > 1)
				c.Error("utc_date takes one moment from real_time() at most, like: utc_date(saved_at)");
			return DateMap(c.GetVM(), CivilFromSeconds(DateSeconds(c, 0, "utc_date")));
		});
		// A moment as text in local time: "2026-11-22 02:00" by default, or by a pattern of YYYY, MM, DD,
		// hh, mm, ss, Month, Mon, Weekday and Wkd: format_date(saved_at, "Weekday, Month DD").
		RegisterFunction("format_date", AnyArity, [](CallContext& c)
		{
			if (c.Count() < 1 || c.Count() > 2)
				c.Error("format_date needs a moment from real_time() and maybe a pattern, like: format_date(real_time(), \"YYYY-MM-DD\")");
			const CivilDate date = LocalDate(DateSeconds(c, 0, "format_date"));
			return c.GetVM().MakeText(FormatDate(date, c.Count() > 1 ? c.Text(1) : std::string("YYYY-MM-DD hh:mm")));
		});
		// Whole calendar days from one moment to the next in local time: 1 from 11 PM to 1 AM, so a
		// daily reward knows a new day began. Negative when 'to' is earlier.
		RegisterFunction("days_between", 2, [](CallContext& c)
		{
			const CivilDate from = LocalDate(DateSeconds(c, 0, "days_between"));
			const CivilDate to = LocalDate(DateSeconds(c, 1, "days_between"));
			return Value::FromNumber(static_cast<double>(DaysFromCivil(to.Year, to.Month, to.Day) - DaysFromCivil(from.Year, from.Month, from.Day)));
		});

		// ------------------------------------------------------------------ Vectors

		RegisterFunction("vector", AnyArity, [](CallContext& c)
		{
			if (c.Count() == 0)
				return c.GetVM().MakeVector(0, 0, 0);
			if (c.Count() < 2 || c.Count() > 3)
				c.Error("vector needs 2 or 3 numbers, like: vector(1, 2, 3)");
			return c.GetVM().MakeVector(c.Number(0), c.Number(1), c.Count() > 2 ? c.Number(2) : 0.0);
		});
		RegisterFunction("distance", 2, [](CallContext& c)
		{
			Vector* a = c.VectorArg(0);
			Vector* b = c.VectorArg(1);
			double dx = a->X - b->X, dy = a->Y - b->Y, dz = a->Z - b->Z;
			return Value::FromNumber(std::sqrt(dx * dx + dy * dy + dz * dz));
		});
		RegisterPhrase("distance from (a) to (b)", [](CallContext& c)
		{
			Vector* a = c.VectorArg(0);
			Vector* b = c.VectorArg(1);
			double dx = a->X - b->X, dy = a->Y - b->Y, dz = a->Z - b->Z;
			return Value::FromNumber(std::sqrt(dx * dx + dy * dy + dz * dz));
		});

		auto selfVector = [](CallContext& c) -> Vector*
		{
			return As<Vector>(c.Self());
		};
		RegisterMethod(VectorMethods, "dot", 1, [selfVector](CallContext& c)
		{
			Vector* a = selfVector(c);
			Vector* b = c.VectorArg(0);
			return Value::FromNumber(a->X * b->X + a->Y * b->Y + a->Z * b->Z);
		});
		RegisterMethod(VectorMethods, "cross", 1, [selfVector](CallContext& c)
		{
			Vector* a = selfVector(c);
			Vector* b = c.VectorArg(0);
			return c.GetVM().MakeVector(a->Y * b->Z - a->Z * b->Y, a->Z * b->X - a->X * b->Z, a->X * b->Y - a->Y * b->X);
		});
		RegisterMethod(VectorMethods, "normalized", 0, [selfVector](CallContext& c)
		{
			Vector* a = selfVector(c);
			double length = std::sqrt(a->X * a->X + a->Y * a->Y + a->Z * a->Z);
			if (length == 0)
				return c.GetVM().MakeVector(0, 0, 0);
			return c.GetVM().MakeVector(a->X / length, a->Y / length, a->Z / length);
		});
		RegisterMethod(VectorMethods, "distance_to", 1, [selfVector](CallContext& c)
		{
			Vector* a = selfVector(c);
			Vector* b = c.VectorArg(0);
			double dx = a->X - b->X, dy = a->Y - b->Y, dz = a->Z - b->Z;
			return Value::FromNumber(std::sqrt(dx * dx + dy * dy + dz * dz));
		});
		RegisterMethod(VectorMethods, "lerp", 2, [selfVector](CallContext& c)
		{
			Vector* a = selfVector(c);
			Vector* b = c.VectorArg(0);
			double t = c.Number(1);
			return c.GetVM().MakeVector(a->X + (b->X - a->X) * t, a->Y + (b->Y - a->Y) * t, a->Z + (b->Z - a->Z) * t);
		});
		RegisterMethod(VectorMethods, "copy", 0, [selfVector](CallContext& c)
		{
			Vector* a = selfVector(c);
			return c.GetVM().MakeVector(a->X, a->Y, a->Z);
		});

		// ------------------------------------------------------------------ Lists

		auto selfList = [](CallContext& c) -> List* { return As<List>(c.Self()); };

		RegisterMethod(ListMethods, "add", 1, [selfList](CallContext& c)
		{
			selfList(c)->Items.push_back(c.Arg(0));
			return c.Self();
		});
		RegisterMethod(ListMethods, "insert", 2, [selfList](CallContext& c)
		{
			List* list = selfList(c);
			size_t index = RequirePosition(c, 0, list->Items.size(), true);
			list->Items.insert(list->Items.begin() + static_cast<std::ptrdiff_t>(index), c.Arg(1));
			return c.Self();
		});
		RegisterMethod(ListMethods, "remove", 1, [selfList](CallContext& c)
		{
			std::vector<Value>& items = selfList(c)->Items;
			auto it = std::find_if(items.begin(), items.end(), [&c](const Value& v) { return ContentsEqual(v, c.Arg(0)); });
			if (it == items.end())
				return Value::FromBool(false);
			items.erase(it);
			return Value::FromBool(true);
		});
		RegisterMethod(ListMethods, "remove_at", 1, [selfList](CallContext& c)
		{
			std::vector<Value>& items = selfList(c)->Items;
			size_t index = RequirePosition(c, 0, items.size(), false);
			Value removed = items[index];
			items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
			return removed;
		});
		RegisterMethod(ListMethods, "pop", 0, [selfList](CallContext& c)
		{
			std::vector<Value>& items = selfList(c)->Items;
			if (items.empty())
				return Value();
			Value last = items.back();
			items.pop_back();
			return last;
		});
		RegisterMethod(ListMethods, "clear", 0, [selfList](CallContext& c)
		{
			selfList(c)->Items.clear();
			return c.Self();
		});
		RegisterMethod(ListMethods, "contains", 1, [selfList](CallContext& c)
		{
			const std::vector<Value>& items = selfList(c)->Items;
			return Value::FromBool(std::any_of(items.begin(), items.end(), [&c](const Value& v) { return ContentsEqual(v, c.Arg(0)); }));
		});
		RegisterMethod(ListMethods, "index_of", 1, [selfList](CallContext& c)
		{
			const std::vector<Value>& items = selfList(c)->Items;
			auto it = std::find_if(items.begin(), items.end(), [&c](const Value& v) { return ContentsEqual(v, c.Arg(0)); });
			if (it == items.end())
				return Value();
			return Value::FromNumber(static_cast<double>(it - items.begin() + 1));
		});
		RegisterMethod(ListMethods, "is_empty", 0, [selfList](CallContext& c) { return Value::FromBool(selfList(c)->Items.empty()); });
		RegisterMethod(ListMethods, "copy", 0, [selfList](CallContext& c) { return c.GetVM().MakeList(selfList(c)->Items); });
		RegisterMethod(ListMethods, "reverse", 0, [selfList](CallContext& c)
		{
			std::vector<Value>& items = selfList(c)->Items;
			std::reverse(items.begin(), items.end());
			return c.Self();
		});
		RegisterMethod(ListMethods, "shuffle", 0, [selfList](CallContext& c)
		{
			std::vector<Value>& items = selfList(c)->Items;
			std::shuffle(items.begin(), items.end(), c.GetVM().GetRandom());
			return c.Self();
		});
		RegisterMethod(ListMethods, "sort", 0, [selfList](CallContext& c)
		{
			std::vector<Value>& items = selfList(c)->Items;
			std::stable_sort(items.begin(), items.end(), [&c](const Value& a, const Value& b) { return LessThan(c, a, b); });
			return c.Self();
		});
		RegisterMethod(ListMethods, "sort_by", 1, [selfList](CallContext& c)
		{
			VM& vm = c.GetVM();
			List* list = selfList(c);
			Value fn = c.Arg(0);
			if (!c.IsCallable(0))
				c.Error("sort_by needs a function that gives the value to sort by.");

			// Work out every key first (kept alive in a list), then sort by the keys.
			Value keysValue = vm.MakeList();
			Handle keepKeys(vm, keysValue);
			List* keys = As<List>(keysValue);
			std::vector<Value> snapshot = list->Items;
			Value snapshotValue = vm.MakeList(snapshot);
			Handle keepSnapshot(vm, snapshotValue);
			for (const Value& item : snapshot)
				keys->Items.push_back(vm.CallFromNative(fn, { item }));

			std::vector<size_t> order(snapshot.size());
			for (size_t i = 0; i < order.size(); i++)
				order[i] = i;
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return LessThan(c, keys->Items[a], keys->Items[b]); });
			for (size_t i = 0; i < order.size(); i++)
				list->Items[i] = snapshot[order[i]];
			return c.Self();
		});
		RegisterMethod(ListMethods, "join", AnyArity, [selfList](CallContext& c)
		{
			std::string separator = c.Count() > 0 ? c.Text(0) : std::string(", ");
			std::string text;
			const std::vector<Value>& items = selfList(c)->Items;
			for (size_t i = 0; i < items.size(); i++)
			{
				if (i > 0)
					text += separator;
				text += c.GetVM().ToText(items[i]);
			}
			return c.GetVM().MakeText(text);
		});
		RegisterMethod(ListMethods, "slice", AnyArity, [selfList](CallContext& c)
		{
			const std::vector<Value>& items = selfList(c)->Items;
			long long start = ClampPosition(c.Count() > 0 ? static_cast<double>(c.Integer(0)) : 1.0, items.size());
			long long end = ClampPosition(c.Count() > 1 ? static_cast<double>(c.Integer(1)) : -1.0, items.size());
			std::vector<Value> result;
			for (long long i = start; i <= end && i <= static_cast<long long>(items.size()); i++)
				result.push_back(items[static_cast<size_t>(i - 1)]);
			return c.GetVM().MakeList(std::move(result));
		});
		RegisterMethod(ListMethods, "sum", 0, [selfList](CallContext& c)
		{
			double total = 0;
			for (const Value& item : selfList(c)->Items)
			{
				if (!item.IsNumber())
					c.Error("sum only works on a list of numbers.");
				total += item.AsNumber();
			}
			return Value::FromNumber(total);
		});
		RegisterMethod(ListMethods, "pick", 0, [selfList](CallContext& c)
		{
			const std::vector<Value>& items = selfList(c)->Items;
			if (items.empty())
				return Value();
			std::uniform_int_distribution<size_t> pick(0, items.size() - 1);
			return items[pick(c.GetVM().GetRandom())];
		});
		RegisterMethod(ListMethods, "each", 1, [selfList](CallContext& c)
		{
			Value fn = c.Arg(0);
			std::vector<Value> snapshot = selfList(c)->Items;
			Value held = c.GetVM().MakeList(snapshot);
			Handle keep(c.GetVM(), held);
			for (const Value& item : snapshot)
				c.GetVM().CallFromNative(fn, { item });
			return Value();
		});
		RegisterMethod(ListMethods, "map", 1, [selfList](CallContext& c)
		{
			VM& vm = c.GetVM();
			Value fn = c.Arg(0);
			std::vector<Value> snapshot = selfList(c)->Items;
			Value held = vm.MakeList(snapshot);
			Handle keepSnapshot(vm, held);
			Value result = vm.MakeList();
			Handle keepResult(vm, result);
			for (const Value& item : snapshot)
				As<List>(result)->Items.push_back(vm.CallFromNative(fn, { item }));
			return result;
		});
		RegisterMethod(ListMethods, "filter", 1, [selfList](CallContext& c)
		{
			VM& vm = c.GetVM();
			Value fn = c.Arg(0);
			std::vector<Value> snapshot = selfList(c)->Items;
			Value held = vm.MakeList(snapshot);
			Handle keepSnapshot(vm, held);
			Value result = vm.MakeList();
			Handle keepResult(vm, result);
			for (const Value& item : snapshot)
			{
				if (vm.CallFromNative(fn, { item }).IsTruthy())
					As<List>(result)->Items.push_back(item);
			}
			return result;
		});
		RegisterMethod(ListMethods, "find", 1, [selfList](CallContext& c)
		{
			VM& vm = c.GetVM();
			Value fn = c.Arg(0);
			std::vector<Value> snapshot = selfList(c)->Items;
			Value held = vm.MakeList(snapshot);
			Handle keep(vm, held);
			for (const Value& item : snapshot)
			{
				if (vm.CallFromNative(fn, { item }).IsTruthy())
					return item;
			}
			return Value();
		});

		// The numbers from 'from' to 'to', both included, like 'for i from 1 to 5'. Counts down when
		// 'to' is smaller; 'step' (always 0 or more) sets the gap.
		RegisterFunction("range", AnyArity, [](CallContext& c)
		{
			if (c.Count() < 2 || c.Count() > 3)
				c.Error("range needs where to start and where to end, like: range(1, 5), or range(0, 100, 10)");
			const double from = c.Number(0);
			const double to = c.Number(1);
			const double step = c.Count() > 2 ? c.Number(2) : 1.0;
			if (!(step > 0))
				c.Error("range needs a step above 0, but got " + FormatNumber(step) + ". It counts down by itself when the end is smaller.");
			const double count = std::floor(std::abs(to - from) / step + 1e-9) + 1;
			if (!std::isfinite(count) || count > 10000000)
				c.Error("range would make more than 10,000,000 numbers.");
			const double direction = to >= from ? 1.0 : -1.0;
			std::vector<Value> numbers;
			numbers.reserve(static_cast<size_t>(count));
			for (size_t i = 0; i < static_cast<size_t>(count); i++)
				numbers.push_back(Value::FromNumber(from + direction * step * static_cast<double>(i)));
			return c.GetVM().MakeList(std::move(numbers));
		});
		// Values as JSON text and back, for web services and files other programs read. JSON has no
		// vectors, functions or objects, so those are refused with what to write instead.
		RegisterFunction("to_json", 1, [](CallContext& c)
		{
			std::function<Json(const Value&, int)> convert = [&](const Value& value, int depth) -> Json
			{
				if (depth > 256)
					c.Error("to_json can't turn lists or maps nested more than 256 deep into JSON.");
				if (value.IsNothing())
					return Json(nullptr);
				if (value.IsBool())
					return Json(value.AsBool());
				if (value.IsNumber())
				{
					if (!std::isfinite(value.AsNumber()))
						c.Error("to_json can't write " + FormatNumber(value.AsNumber()) + ": JSON only has finite numbers.");
					return Json(value.AsNumber());
				}
				if (IsObjectType(value, ObjectType::String))
					return Json(As<String>(value)->Text);
				if (IsObjectType(value, ObjectType::List))
				{
					Json::Array items;
					for (const Value& item : As<List>(value)->Items)
						items.push_back(convert(item, depth + 1));
					return Json(std::move(items));
				}
				if (IsObjectType(value, ObjectType::Map))
				{
					Json::Object members;
					for (const Map::Entry& entry : As<Map>(value)->Entries)
					{
						if (!IsObjectType(entry.Key, ObjectType::String))
							c.Error("to_json needs map keys that are text, but one is " + c.GetVM().TypeOf(entry.Key) + ".");
						members.emplace_back(As<String>(entry.Key)->Text, convert(entry.Item, depth + 1));
					}
					return Json(std::move(members));
				}
				if (IsObjectType(value, ObjectType::Vector))
					c.Error("to_json can't write a vector: JSON has none. Write a list instead, like: [v.x, v.y, v.z]");
				c.Error("to_json can only write nothing, true/false, numbers, text, lists and maps, not " + c.GetVM().TypeOf(value) + ".");
			};
			return c.GetVM().MakeText(convert(c.Arg(0), 0).Dump());
		});
		RegisterFunction("from_json", 1, [](CallContext& c)
		{
			Json json;
			std::string problem;
			if (!Json::Parse(c.Text(0), json, problem))
				c.Error("from_json can't read this text as JSON: " + problem + ".");
			// The garbage collector only runs between instructions, so the parts made so far are safe while
			// the whole value is built.
			VM& vm = c.GetVM();
			std::function<Value(const Json&)> convert = [&](const Json& item) -> Value
			{
				if (item.IsNull())
					return Value();
				if (item.IsBool())
					return Value::FromBool(item.AsBool());
				if (item.IsNumber())
					return Value::FromNumber(item.AsNumber());
				if (item.IsString())
					return vm.MakeText(item.AsString());
				if (item.IsArray())
				{
					Value list = vm.MakeList();
					for (const Json& element : item.AsArray())
						As<List>(list)->Items.push_back(convert(element));
					return list;
				}
				Value map = vm.MakeMap();
				for (const auto& [key, element] : item.AsObject())
				{
					Value keyText = vm.MakeText(key);
					As<Map>(map)->Set(keyText, convert(element));
				}
				return map;
			};
			return convert(json);
		});
		// Pairs up items by position: zip(["a", "b"], [1, 2]) is [["a", 1], ["b", 2]]. Stops at the
		// end of the shorter list.
		RegisterFunction("zip", 2, [](CallContext& c)
		{
			VM& vm = c.GetVM();
			const std::vector<Value> first = c.ListArg(0)->Items;
			const std::vector<Value> second = c.ListArg(1)->Items;
			Value result = vm.MakeList();
			Handle keep(vm, result);
			for (size_t i = 0; i < std::min(first.size(), second.size()); i++)
				As<List>(result)->Items.push_back(vm.MakeList({ first[i], second[i] }));
			return result;
		});
		// Asks 'fn' about each item, on a copy of the list kept safe from the garbage collector, so the
		// function may change the list while it runs. Stops when 'visit' returns false.
		auto askEach = [selfList](CallContext& c, const Value& fn, const std::function<bool(const Value& item, bool yes)>& visit)
		{
			VM& vm = c.GetVM();
			std::vector<Value> snapshot = selfList(c)->Items;
			Value held = vm.MakeList(snapshot);
			Handle keep(vm, held);
			for (const Value& item : snapshot)
			{
				if (!visit(item, vm.CallFromNative(fn, { item }).IsTruthy()))
					return;
			}
		};
		auto needFunction = [](CallContext& c, const char* name)
		{
			if (!c.IsCallable(0))
			{
				c.Error(std::string(name) + " needs a function that answers yes or no for each item, like: enemies." + name +
					"(function(enemy) return enemy.health is 0 end)");
			}
		};
		RegisterMethod(ListMethods, "count", 1, [selfList, askEach](CallContext& c)
		{
			// With a function: how many items it says yes to. With a value: how many items equal it.
			double count = 0;
			if (c.IsCallable(0))
				askEach(c, c.Arg(0), [&count](const Value&, bool yes) { count += yes ? 1 : 0; return true; });
			else
			{
				for (const Value& item : selfList(c)->Items)
					count += ContentsEqual(item, c.Arg(0)) ? 1 : 0;
			}
			return Value::FromNumber(count);
		});
		RegisterMethod(ListMethods, "any", 1, [askEach, needFunction](CallContext& c)
		{
			needFunction(c, "any");
			bool found = false;
			askEach(c, c.Arg(0), [&found](const Value&, bool yes) { found = yes; return !yes; });
			return Value::FromBool(found);
		});
		RegisterMethod(ListMethods, "all", 1, [askEach, needFunction](CallContext& c)
		{
			needFunction(c, "all");
			bool every = true;
			askEach(c, c.Arg(0), [&every](const Value&, bool yes) { every = yes; return yes; });
			return Value::FromBool(every);
		});
		// Removes every item 'fn' says yes to, and gives back how many went. The function is asked about
		// the list as it was when remove_where started.
		RegisterMethod(ListMethods, "remove_where", 1, [selfList, askEach, needFunction](CallContext& c)
		{
			needFunction(c, "remove_where");
			std::vector<Value> kept;
			double removed = 0;
			askEach(c, c.Arg(0), [&](const Value& item, bool yes)
			{
				if (yes)
					removed++;
				else
					kept.push_back(item);
				return true;
			});
			selfList(c)->Items = std::move(kept);
			return Value::FromNumber(removed);
		});
		// The numbers of a list, for min, max and average; an empty list gives nothing back.
		auto numbersOf = [selfList](CallContext& c, const char* name)
		{
			std::vector<double> numbers;
			for (const Value& item : selfList(c)->Items)
			{
				if (!item.IsNumber())
					c.Error(std::string(name) + " only works on a list of numbers, but the list has " + c.GetVM().TypeOf(item) + ".");
				numbers.push_back(item.AsNumber());
			}
			return numbers;
		};
		RegisterMethod(ListMethods, "min", 0, [numbersOf](CallContext& c)
		{
			const std::vector<double> numbers = numbersOf(c, "min");
			return numbers.empty() ? Value() : Value::FromNumber(*std::min_element(numbers.begin(), numbers.end()));
		});
		RegisterMethod(ListMethods, "max", 0, [numbersOf](CallContext& c)
		{
			const std::vector<double> numbers = numbersOf(c, "max");
			return numbers.empty() ? Value() : Value::FromNumber(*std::max_element(numbers.begin(), numbers.end()));
		});
		RegisterMethod(ListMethods, "average", 0, [numbersOf](CallContext& c)
		{
			const std::vector<double> numbers = numbersOf(c, "average");
			if (numbers.empty())
				return Value();
			double total = 0;
			for (double number : numbers)
				total += number;
			return Value::FromNumber(total / static_cast<double>(numbers.size()));
		});

		// Each item once, the first time it comes, in order: [3, 1, 3, 2].unique() is [3, 1, 2].
		// Lists and maps count as the same when their items are.
		RegisterMethod(ListMethods, "unique", 0, [selfList](CallContext& c)
		{
			VM& vm = c.GetVM();
			const std::vector<Value>& items = selfList(c)->Items;
			std::vector<Value> kept;
			// Numbers and text are looked up quickly; other values are compared with the ones kept.
			std::unordered_set<double> numbers;
			std::unordered_set<std::string_view> texts;
			for (const Value& item : items)
			{
				bool seen = false;
				if (item.IsNumber())
					seen = !numbers.insert(item.AsNumber() == 0 ? 0.0 : item.AsNumber()).second;
				else if (IsObjectType(item, ObjectType::String))
					seen = !texts.insert(As<String>(item)->Text).second;
				else
					seen = std::any_of(kept.begin(), kept.end(), [&item](const Value& other) { return ContentsEqual(item, other); });
				if (!seen)
					kept.push_back(item);
			}
			return vm.MakeList(std::move(kept));
		});
		// The items of the lists inside, one level deep: [[1, 2], [3], 4].flatten() is [1, 2, 3, 4].
		RegisterMethod(ListMethods, "flatten", 0, [selfList](CallContext& c)
		{
			std::vector<Value> flat;
			for (const Value& item : selfList(c)->Items)
			{
				if (IsObjectType(item, ObjectType::List))
				{
					const std::vector<Value>& inner = As<List>(item)->Items;
					flat.insert(flat.end(), inner.begin(), inner.end());
				}
				else
					flat.push_back(item);
			}
			return c.GetVM().MakeList(std::move(flat));
		});
		// The list cut into lists of 'size' items (the last may be shorter): rows of a grid, pages of a
		// menu.
		RegisterMethod(ListMethods, "chunk", 1, [selfList](CallContext& c)
		{
			VM& vm = c.GetVM();
			const int64_t size = c.Integer(0);
			if (size < 1)
				c.Error("chunk needs a size of 1 or more, like: items.chunk(3)");
			const std::vector<Value> items = selfList(c)->Items;
			Value result = vm.MakeList();
			Handle keep(vm, result);
			for (size_t start = 0; start < items.size(); start += static_cast<size_t>(size))
			{
				const size_t end = std::min(items.size(), start + static_cast<size_t>(size));
				As<List>(result)->Items.push_back(vm.MakeList(std::vector<Value>(items.begin() + static_cast<std::ptrdiff_t>(start),
					items.begin() + static_cast<std::ptrdiff_t>(end))));
			}
			return result;
		});
		// The item for which 'fn' gives the biggest (or smallest) number, the first one on a tie, or
		// nothing for an empty list: enemies.min_by(function(e) return e.distance_to(me) end).
		auto bestBy = [selfList](CallContext& c, const char* name, bool biggest) -> Value
		{
			VM& vm = c.GetVM();
			if (!c.IsCallable(0))
				c.Error(std::string(name) + " needs a function that gives a number for each item, like: enemies." + name +
					"(function(enemy) return enemy.health end)");
			const std::vector<Value> snapshot = selfList(c)->Items;
			Value held = vm.MakeList(snapshot);
			Handle keep(vm, held);
			Value best;
			double bestScore = 0;
			for (size_t i = 0; i < snapshot.size(); i++)
			{
				const Value score = vm.CallFromNative(c.Arg(0), { snapshot[i] });
				if (!score.IsNumber())
					c.Error(std::string(name) + "'s function must give a number for each item, but gave " + vm.TypeOf(score) + ".");
				if (i == 0 || (biggest ? score.AsNumber() > bestScore : score.AsNumber() < bestScore))
				{
					best = snapshot[i];
					bestScore = score.AsNumber();
				}
			}
			return best;
		};
		RegisterMethod(ListMethods, "max_by", 1, [bestBy](CallContext& c) { return bestBy(c, "max_by", true); });
		RegisterMethod(ListMethods, "min_by", 1, [bestBy](CallContext& c) { return bestBy(c, "min_by", false); });

		// ------------------------------------------------------------------ Text

		auto selfText = [](CallContext& c) -> const std::string& { return As<String>(c.Self())->Text; };

		RegisterMethod(TextMethods, "upper", 0, [selfText](CallContext& c) { return c.GetVM().MakeText(Upper(selfText(c))); });
		RegisterMethod(TextMethods, "lower", 0, [selfText](CallContext& c) { return c.GetVM().MakeText(Lower(selfText(c))); });
		RegisterMethod(TextMethods, "trim", 0, [selfText](CallContext& c) { return c.GetVM().MakeText(Trim(selfText(c))); });
		RegisterMethod(TextMethods, "contains", 1, [selfText](CallContext& c)
		{
			return Value::FromBool(selfText(c).find(c.Text(0)) != std::string::npos);
		});
		RegisterMethod(TextMethods, "starts_with", 1, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			const std::string& prefix = c.Text(0);
			return Value::FromBool(text.compare(0, prefix.size(), prefix) == 0);
		});
		RegisterMethod(TextMethods, "ends_with", 1, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			const std::string& suffix = c.Text(0);
			return Value::FromBool(text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
		});
		RegisterMethod(TextMethods, "find", 1, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			size_t at = text.find(c.Text(0));
			if (at == std::string::npos)
				return Value();
			return Value::FromNumber(static_cast<double>(Utf8::Length(std::string_view(text).substr(0, at)) + 1));
		});
		RegisterMethod(TextMethods, "replace", 2, [selfText](CallContext& c)
		{
			std::string text = selfText(c);
			const std::string& from = c.Text(0);
			const std::string& to = c.Text(1);
			if (from.empty())
				c.Error("replace needs some text to look for.");
			size_t at = 0;
			while ((at = text.find(from, at)) != std::string::npos)
			{
				text.replace(at, from.size(), to);
				at += to.size();
			}
			return c.GetVM().MakeText(text);
		});
		RegisterMethod(TextMethods, "split", AnyArity, [selfText](CallContext& c)
		{
			VM& vm = c.GetVM();
			const std::string& text = selfText(c);
			std::vector<Value> parts;
			Value result = vm.MakeList();
			if (c.Count() == 0)
			{
				// Split on any whitespace.
				std::string current;
				for (char ch : text)
				{
					if (std::isspace(static_cast<unsigned char>(ch)))
					{
						if (!current.empty())
							As<List>(result)->Items.push_back(vm.MakeText(current));
						current.clear();
					}
					else
						current += ch;
				}
				if (!current.empty())
					As<List>(result)->Items.push_back(vm.MakeText(current));
				return result;
			}
			const std::string& separator = c.Text(0);
			if (separator.empty())
			{
				for (size_t i = 0, n = Utf8::Length(text); i < n; i++)
					As<List>(result)->Items.push_back(vm.MakeText(Utf8::CharAt(text, i)));
				return result;
			}
			size_t start = 0;
			size_t at = 0;
			while ((at = text.find(separator, start)) != std::string::npos)
			{
				As<List>(result)->Items.push_back(vm.MakeText(text.substr(start, at - start)));
				start = at + separator.size();
			}
			As<List>(result)->Items.push_back(vm.MakeText(text.substr(start)));
			return result;
		});
		RegisterMethod(TextMethods, "slice", AnyArity, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			size_t length = Utf8::Length(text);
			long long start = ClampPosition(c.Count() > 0 ? static_cast<double>(c.Integer(0)) : 1.0, length);
			long long end = ClampPosition(c.Count() > 1 ? static_cast<double>(c.Integer(1)) : -1.0, length);
			if (end < start || length == 0)
				return c.GetVM().MakeText("");
			long long last = std::min<long long>(end, static_cast<long long>(length));
			return c.GetVM().MakeText(Utf8::Slice(text, static_cast<size_t>(start - 1), static_cast<size_t>(last - start + 1)));
		});
		RegisterMethod(TextMethods, "repeat", 1, [selfText](CallContext& c)
		{
			int64_t count = c.Integer(0);
			if (count < 0)
				c.Error("repeat needs a count of 0 or more.");
			const std::string& text = selfText(c);
			if (!text.empty() && static_cast<uint64_t>(count) > c_MaxRepeatedTextBytes / text.size())
				c.Error("Repeating this text " + std::to_string(count) + " times would make more than " +
					FormatNumber(static_cast<double>(c_MaxRepeatedTextBytes)) + " bytes of text.");
			std::string result;
			result.reserve(text.size() * static_cast<size_t>(count));
			for (int64_t i = 0; i < count && !text.empty(); i++)
				result += text;
			return c.GetVM().MakeText(result);
		});
		// "7".pad_start(3, "0") is "007". Widths count letters, not bytes, so "é" is one.
		auto pad = [selfText](CallContext& c, bool atStart) -> Value
		{
			const char* name = atStart ? "pad_start" : "pad_end";
			const int64_t width = c.Integer(0);
			if (width < 0)
				c.Error(std::string(name) + " needs a width of 0 or more.");
			const std::string fill = c.Count() > 1 ? c.Text(1) : std::string(" ");
			if (Utf8::Length(fill) != 1)
				c.Error(std::string(name) + " fills with one character, like: \"7\"." + name + "(3, \"0\")");
			const std::string& text = selfText(c);
			const size_t length = Utf8::Length(text);
			if (static_cast<uint64_t>(width) <= length)
				return c.GetVM().MakeText(text);
			if (width > 1000000)
				c.Error(std::string(name) + " needs a width of at most 1,000,000.");
			std::string padding;
			for (size_t i = length; i < static_cast<size_t>(width); i++)
				padding += fill;
			return c.GetVM().MakeText(atStart ? padding + text : text + padding);
		};
		// Letter by letter, so "café" becomes "éfac", not broken bytes.
		RegisterMethod(TextMethods, "reverse", 0, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			std::string reversed;
			for (size_t i = Utf8::Length(text); i > 0; i--)
				reversed += Utf8::CharAt(text, i - 1);
			return c.GetVM().MakeText(reversed);
		});
		RegisterMethod(TextMethods, "pad_start", AnyArity, [pad](CallContext& c) { return pad(c, true); });
		RegisterMethod(TextMethods, "pad_end", AnyArity, [pad](CallContext& c) { return pad(c, false); });
		RegisterMethod(TextMethods, "characters", 0, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			Value result = c.GetVM().MakeList();
			for (size_t i = 0, n = Utf8::Length(text); i < n; i++)
				As<List>(result)->Items.push_back(c.GetVM().MakeText(Utf8::CharAt(text, i)));
			return result;
		});
		RegisterMethod(TextMethods, "is_number", 0, [selfText](CallContext& c)
		{
			bool ok = false;
			ToNumberOr(selfText(c), ok);
			return Value::FromBool(ok);
		});
		RegisterMethod(TextMethods, "is_empty", 0, [selfText](CallContext& c) { return Value::FromBool(selfText(c).empty()); });

		// "sword of fire".capitalize() is "Sword of fire": the first letter in capitals, the rest as it is.
		RegisterMethod(TextMethods, "capitalize", 0, [selfText](CallContext& c)
		{
			std::string text = selfText(c);
			if (!text.empty())
				text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
			return c.GetVM().MakeText(text);
		});
		// "sword of fire".title() is "Sword Of Fire": the first letter of each word in capitals.
		RegisterMethod(TextMethods, "title", 0, [selfText](CallContext& c)
		{
			std::string text = selfText(c);
			bool wordStart = true;
			for (char& ch : text)
			{
				const unsigned char byte = static_cast<unsigned char>(ch);
				if (std::isspace(byte))
					wordStart = true;
				else
				{
					if (wordStart)
						ch = static_cast<char>(std::toupper(byte));
					wordStart = false;
				}
			}
			return c.GetVM().MakeText(text);
		});
		// How many times 'part' is in the text, without overlapping: "banana".count("an") is 2.
		RegisterMethod(TextMethods, "count", 1, [selfText](CallContext& c)
		{
			const std::string& text = selfText(c);
			const std::string part = c.Text(0);
			if (part.empty())
				c.Error("count needs some text to look for, like: line.count(\"a\")");
			double count = 0;
			for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
				count++;
			return Value::FromNumber(count);
		});

		// ------------------------------------------------------------------ Maps

		auto selfMap = [](CallContext& c) -> Map* { return As<Map>(c.Self()); };

		RegisterMethod(MapMethods, "keys", 0, [selfMap](CallContext& c)
		{
			std::vector<Value> keys;
			for (const Map::Entry& entry : selfMap(c)->Entries)
				keys.push_back(entry.Key);
			return c.GetVM().MakeList(std::move(keys));
		});
		RegisterMethod(MapMethods, "values", 0, [selfMap](CallContext& c)
		{
			std::vector<Value> values;
			for (const Map::Entry& entry : selfMap(c)->Entries)
				values.push_back(entry.Item);
			return c.GetVM().MakeList(std::move(values));
		});
		RegisterMethod(MapMethods, "has", 1, [selfMap](CallContext& c) { return Value::FromBool(selfMap(c)->Has(c.Arg(0))); });
		RegisterMethod(MapMethods, "get", AnyArity, [selfMap](CallContext& c)
		{
			if (c.Count() < 1 || c.Count() > 2)
				c.Error("get needs a key and an optional default value.");
			Value value;
			if (selfMap(c)->Get(c.Arg(0), value))
				return value;
			return c.Arg(1);
		});
		RegisterMethod(MapMethods, "remove", 1, [selfMap](CallContext& c) { return Value::FromBool(selfMap(c)->Remove(c.Arg(0))); });
		// Copies every entry of 'other' into this map, replacing values whose keys are already here, and
		// gives back this map, like adding a level's settings over the defaults.
		RegisterMethod(MapMethods, "merge", 1, [selfMap](CallContext& c)
		{
			Map* target = selfMap(c);
			const std::vector<Map::Entry> entries = c.MapArg(0)->Entries;
			for (const Map::Entry& entry : entries)
				target->Set(entry.Key, entry.Item);
			return c.Self();
		});
		RegisterMethod(MapMethods, "clear", 0, [selfMap](CallContext& c)
		{
			selfMap(c)->Entries.clear();
			selfMap(c)->Index.clear();
			return c.Self();
		});
		RegisterMethod(MapMethods, "is_empty", 0, [selfMap](CallContext& c) { return Value::FromBool(selfMap(c)->Size() == 0); });
		RegisterMethod(MapMethods, "copy", 0, [selfMap](CallContext& c)
		{
			Value result = c.GetVM().MakeMap();
			for (const Map::Entry& entry : selfMap(c)->Entries)
				As<Map>(result)->Set(entry.Key, entry.Item);
			return result;
		});

	}

}
