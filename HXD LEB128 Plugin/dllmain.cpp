#include <vector>
#include <functional>
#include <type_traits>
#include <sstream>

#include <Windows.h>

#define SET_ENUM_CLASS_FLAGGABLE(T) \
inline T operator~ (T a) { return (T) ~(std::underlying_type<T>::type) a; } \
inline T operator| (T a, T b) { return (T) ((std::underlying_type<T>::type) a | (std::underlying_type<T>::type) b); } \
inline T operator& (T a, T b) { return (T) ((std::underlying_type<T>::type) a & (std::underlying_type<T>::type) b); } \
inline T operator^ (T a, T b) { return (T) ((std::underlying_type<T>::type) a ^ (std::underlying_type<T>::type) b); } \
inline T& operator|= (T& a, T b) { return (T&) ((std::underlying_type<T>::type&) a |= (std::underlying_type<T>::type) b); } \
inline T& operator&= (T& a, T b) { return (T&) ((std::underlying_type<T>::type&) a &= (std::underlying_type<T>::type) b); } \
inline T& operator^= (T& a, T b) { return (T&) ((std::underlying_type<T>::type&) a ^= (std::underlying_type<T>::type) b); }

// All but the first(?) field are function pointers, annotated with the expected signature (since function pointer type declarations are messy and hard to read).
// converter_init_func just returns its first parameter, thisptr.
struct DataInspectorPluginInterface {
	void* converter_alloc_func;					// __stdcall void*()
	void* converter_init_func;					// __stdcall void*(void* thisptr, const wchar_t** name, width_classification* width_type, int* maximum_size, byte_order_classification* supported_byte_orders)
	void* converter_delete_func;				// __stdcall void(void* thisptr)
	void* converter_assign_func;				// __stdcall void(void* thisptr, void* source)
	void* converter_change_byte_order_func;		// __stdcall void(void* thisptr, uint8_t* bytes, int byte_count, byte_order_classification target_byte_order)
	void* converter_bytes_to_str_func;			// __stdcall TBytesToStrError(void* thisptr, uint8_t* bytes, int byte_count, integer_display_option int_display_opt, int* converted_byte_count, const wchar_t** converted_str)
	void* converter_str_to_bytes_func;			// __stdcall TStrToBytesError(void* thisptr, const wchar_t* str, integer_display_option int_display_opt, uint8_t** converted_bytes, int* converted_byte_count)
};

// These windows macros collide with enum names
#undef UNDERFLOW
#undef OVERFLOW

namespace data_types {
	enum struct width_classification : uint8_t {
		VARIABLE,
		FIXED
	};

	enum struct byte_order_classification : uint8_t {
		LITTLE_ENDIAN = 1 << 0,
		BIG_ENDIAN = 1 << 1
	};
	SET_ENUM_CLASS_FLAGGABLE(byte_order_classification)

	enum struct bytes_to_str_error_code : uint8_t {
		NONE, INVALID_BYTES, BYTES_TOO_SHORT
	};

	enum struct str_to_bytes_error_code : uint8_t {
		NONE, INVALID_STRING, UNDERFLOW, OVERFLOW, OUT_OF_RANGE
	};

	enum struct integer_display_option : uint8_t {
		DECIMAL, HEX_UPPER_CASE, HEX_LOWER_CASE
	};

	str_to_bytes_error_code str2int(const wchar_t* begin, int64_t* res, int base) {
		// reject leading whitespace
		if (begin == NULL || iswspace(begin[0]))
			return str_to_bytes_error_code::INVALID_STRING;

		str_to_bytes_error_code converr = str_to_bytes_error_code::NONE;

		// Reset errno to 0, for reliable evaluation later on. (Because, with a few
		// specific exceptions in stdlib, where documented, errno is only updated
		// when an error occurs.)
		int saved_errno = errno;
		errno = 0;

		wchar_t* endptr;
		*res = wcstoll(begin, &endptr, base);

		if (begin == endptr || *endptr != '\0') {
			// nothing was converted (empty string or invalid integer characters), or there were unallowed characters after the last valid digit
			converr = str_to_bytes_error_code::INVALID_STRING;
		}
		else if (errno == ERANGE) {
			if (*res == LLONG_MIN)
				converr = str_to_bytes_error_code::UNDERFLOW;
			else if (*res == LLONG_MAX)
				converr = str_to_bytes_error_code::OVERFLOW;
		}

		// Be transparent to other stdlib functions
		errno = saved_errno;
		return converr;
	}

	str_to_bytes_error_code str2uint(const wchar_t* str, uint64_t* res, int base) {
		// reject leading whitespace
		if (str == NULL || iswspace(str[0]))
			return str_to_bytes_error_code::INVALID_STRING;

		str_to_bytes_error_code converr = str_to_bytes_error_code::NONE;

		// Reset errno to 0, for reliable evaluation later on. (Because, with a few
		// specific exceptions in stdlib, where documented, errno is only updated
		// when an error occurs.)
		int saved_errno = errno;
		errno = 0;

		wchar_t* endptr;
		*res = wcstoul(str, &endptr, base);

		if (str == endptr || *endptr != '\0') {
			// nothing was converted (empty string or invalid integer characters), or there were unallowed characters after the last valid digit
			converr = str_to_bytes_error_code::INVALID_STRING;
		}
		else if (errno == ERANGE) {
			if (*res == 0)
				converr = str_to_bytes_error_code::UNDERFLOW;
			else if (*res == ULLONG_MAX)
				converr = str_to_bytes_error_code::OVERFLOW;
		}

		// be transparent to other stdlib functions
		errno = saved_errno;
		return converr;
	}

	// Since the underlying conversion type is uint64_t, the original leb128 can only be up to 9 bytes, in order to avoid overflow.
	struct unsigned_leb128 {
		const wchar_t* m_name;
		int m_maximum_size;
		width_classification m_width_type;
		byte_order_classification m_supported_byte_orders;

		std::wstring bytes_to_string_result;
		std::vector<uint8_t> string_to_bytes_result;

		static void* __stdcall alloc_instance() {
			return new unsigned_leb128;
		}

		static void* __stdcall init_instance(void* alloc_instance_func, const wchar_t** name, width_classification* width_type, int* maximum_size, byte_order_classification* supported_byte_orders) {
			unsigned_leb128* thisptr = static_cast<unsigned_leb128*>((reinterpret_cast<void*(*)()>(alloc_instance_func))());
			thisptr->m_name = L"Unsigned LEB128";
			thisptr->m_maximum_size = 64 / 7; // uint64_t used in conversion, and each byte of an LEB128 has 7 effective bits
			thisptr->m_width_type = width_classification::VARIABLE;
			thisptr->m_supported_byte_orders = byte_order_classification::LITTLE_ENDIAN;

			*name = thisptr->m_name;
			*maximum_size = thisptr->m_maximum_size;
			*width_type = thisptr->m_width_type;
			*supported_byte_orders = thisptr->m_supported_byte_orders;
			return thisptr;
		}

		static void __stdcall delete_instance(const unsigned_leb128* thisptr) {
			delete thisptr;
		}

		static void __stdcall assign_instance(unsigned_leb128* thisptr, unsigned_leb128* source) {
			*thisptr = *source;
		}

		static void __stdcall change_byte_order(unsigned_leb128* thisptr, uint8_t* bytes, int byte_count, byte_order_classification target_byte_order) {
			// Do nothing, since LEB128 has a fixed little-endian byte order (hence the 'LE' in LEB)
		}

		static bytes_to_str_error_code __stdcall bytes_to_str(unsigned_leb128* thisptr, uint8_t* bytes, int byte_count, integer_display_option int_display_opt, int* converted_byte_count, const wchar_t** converted_str) {
			uint64_t value = 0;
			*converted_byte_count = 0;
			for (uint64_t i = 0; ; i += 7) {
				if (*converted_byte_count >= byte_count)
					return bytes_to_str_error_code::BYTES_TOO_SHORT;
				if (*converted_byte_count >= 64 / 7)
					return bytes_to_str_error_code::INVALID_BYTES;
				uint8_t c = *bytes;
				++*converted_byte_count;
				++bytes;
				value += static_cast<uint64_t>(c & 127) << i;
				if (!(c & 128))
					break;
			}

			std::stringstream ss;
			switch (int_display_opt) {
			case integer_display_option::HEX_UPPER_CASE:
				ss << std::hex << std::uppercase;
				break;
			case integer_display_option::HEX_LOWER_CASE:
				ss << std::hex << std::nouppercase;
				break;
			case integer_display_option::DECIMAL:
			default:
				ss << std::dec;
				break;
			}
			ss << value;
			std::string str = ss.str();
			thisptr->bytes_to_string_result = std::wstring(str.begin(), str.end());
			*converted_str = thisptr->bytes_to_string_result.c_str();

			return bytes_to_str_error_code::NONE;
		}

		static str_to_bytes_error_code __stdcall str_to_bytes(unsigned_leb128* thisptr, const wchar_t* str, integer_display_option int_display_opt, uint8_t** converted_bytes, int* converted_byte_count) {
			std::wstring wstr;
			{
				const wchar_t* begin = str;
				while (iswspace(*begin)) ++begin;
				const wchar_t* end = begin;
				while (*end) ++end;
				while (!*end || iswspace(*end)) --end;
				wstr = std::wstring(begin, end + 1);
			}

			int radix = 10;
			switch (int_display_opt) {
			case integer_display_option::HEX_UPPER_CASE:
			case integer_display_option::HEX_LOWER_CASE:
				radix = 16;
				break;
			case integer_display_option::DECIMAL:
			default:
				radix = 10;
				break;
			}

			uint64_t value;
			auto result = str2uint(str, &value, radix);
			
			thisptr->string_to_bytes_result.clear();
			while (true) {
				uint8_t c = value & 127;
				value >>= 7;
				if (value) {
					c |= 128;
					thisptr->string_to_bytes_result.push_back(c);
				}
				else {
					thisptr->string_to_bytes_result.push_back(c);
					break;
				}
			}

			*converted_bytes = thisptr->string_to_bytes_result.data();
			*converted_byte_count = thisptr->string_to_bytes_result.size();

			return result;
		}
	};

	struct signed_leb128 {
		const wchar_t* m_name;
		int m_maximum_size;
		width_classification m_width_type;
		byte_order_classification m_supported_byte_orders;

		std::wstring bytes_to_string_result;
		std::vector<uint8_t> string_to_bytes_result;

		static void* __stdcall alloc_instance() {
			return new signed_leb128;
		}

		static void* __stdcall init_instance(void* alloc_instance_func, const wchar_t** name, width_classification* width_type, int* maximum_size, byte_order_classification* supported_byte_orders) {
			signed_leb128* thisptr = static_cast<signed_leb128*>((reinterpret_cast<void*(*)()>(alloc_instance_func))());
			thisptr->m_name = L"Signed LEB128";
			thisptr->m_maximum_size = 64 / 7; // uint64_t used in conversion, and each byte of an LEB128 has 7 effective bits
			thisptr->m_width_type = width_classification::VARIABLE;
			thisptr->m_supported_byte_orders = byte_order_classification::LITTLE_ENDIAN;

			*name = thisptr->m_name;
			*maximum_size = thisptr->m_maximum_size;
			*width_type = thisptr->m_width_type;
			*supported_byte_orders = thisptr->m_supported_byte_orders;
			return thisptr;
		}

		static void __stdcall delete_instance(const signed_leb128* thisptr) {
			delete thisptr;
		}

		static void __stdcall assign_instance(signed_leb128* thisptr, signed_leb128* source) {
			*thisptr = *source;
		}

		static void __stdcall change_byte_order(signed_leb128* thisptr, uint8_t* bytes, int byte_count, byte_order_classification target_byte_order) {
			// Do nothing, since LEB128 has a fixed little-endian byte order (hence the 'LE' in LEB)
		}

		static bytes_to_str_error_code __stdcall bytes_to_str(signed_leb128* thisptr, uint8_t* bytes, int byte_count, integer_display_option int_display_opt, int* converted_byte_count, const wchar_t** converted_str) {
			int64_t value = 0;
			*converted_byte_count = 0;
			for (int64_t i = 0; ; i += 7) {
				if (*converted_byte_count >= byte_count)
					return bytes_to_str_error_code::BYTES_TOO_SHORT;
				if (*converted_byte_count >= 64 / 7)
					return bytes_to_str_error_code::INVALID_BYTES;
				uint8_t c = *bytes;
				++*converted_byte_count;
				++bytes;
				value += static_cast<int64_t>(c & 127) << i;
				if (!(c & 128))
					break;
			}
			value = (value >> 1) ^ -(value & 1);

			std::stringstream ss;
			switch (int_display_opt) {
			case integer_display_option::HEX_UPPER_CASE:
				ss << std::hex << std::uppercase;
				break;
			case integer_display_option::HEX_LOWER_CASE:
				ss << std::hex << std::nouppercase;
				break;
			case integer_display_option::DECIMAL:
			default:
				ss << std::dec;
				break;
			}
			ss << value;
			std::string str = ss.str();
			thisptr->bytes_to_string_result = std::wstring(str.begin(), str.end());
			*converted_str = thisptr->bytes_to_string_result.c_str();

			return bytes_to_str_error_code::NONE;
		}

		static str_to_bytes_error_code __stdcall str_to_bytes(signed_leb128* thisptr, const wchar_t* str, integer_display_option int_display_opt, uint8_t** converted_bytes, int* converted_byte_count) {
			std::wstring wstr;
			{
				const wchar_t* begin = str;
				while (iswspace(*begin)) ++begin;
				const wchar_t* end = begin;
				while (*end) ++end;
				while (!*end || iswspace(*end)) --end;
				wstr = std::wstring(begin, end + 1);
			}

			int radix = 10;
			switch (int_display_opt) {
			case integer_display_option::HEX_UPPER_CASE:
			case integer_display_option::HEX_LOWER_CASE:
				radix = 16;
				break;
			case integer_display_option::DECIMAL:
			default:
				radix = 10;
				break;
			}

			int64_t value;
			auto result = str2int(str, &value, radix);
			if (result == str_to_bytes_error_code::OVERFLOW)
				result = str2uint(str, reinterpret_cast<uint64_t*>(&value), radix);

			uint64_t unsigned_value = static_cast<uint64_t>((value << 1) ^ (value >> 63));

			thisptr->string_to_bytes_result.clear();
			while (true) {
				uint8_t c = unsigned_value & 127;
				unsigned_value >>= 7;
				if (unsigned_value) {
					c |= 128;
					thisptr->string_to_bytes_result.push_back(c);
				}
				else {
					thisptr->string_to_bytes_result.push_back(c);
					break;
				}
			}

			*converted_bytes = thisptr->string_to_bytes_result.data();
			*converted_byte_count = thisptr->string_to_bytes_result.size();

			return result;
		}
	};
}

std::vector<DataInspectorPluginInterface> plugin_interfaces;

extern "C" __declspec(dllexport) BOOL __stdcall GetDataTypeConverters(DataInspectorPluginInterface** interfaces, int* interface_count) {
	if (plugin_interfaces.size() > 0)
		*interfaces = &plugin_interfaces[0];
	else
		*interfaces = NULL;
	*interface_count = static_cast<int>(plugin_interfaces.size());

	return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		plugin_interfaces.push_back({
			reinterpret_cast<void*>(data_types::unsigned_leb128::alloc_instance),
			reinterpret_cast<void*>(data_types::unsigned_leb128::init_instance),
			reinterpret_cast<void*>(data_types::unsigned_leb128::delete_instance),
			reinterpret_cast<void*>(data_types::unsigned_leb128::assign_instance),
			reinterpret_cast<void*>(data_types::unsigned_leb128::change_byte_order),
			reinterpret_cast<void*>(data_types::unsigned_leb128::bytes_to_str),
			reinterpret_cast<void*>(data_types::unsigned_leb128::str_to_bytes)
			});
		plugin_interfaces.push_back({
			reinterpret_cast<void*>(data_types::signed_leb128::alloc_instance),
			reinterpret_cast<void*>(data_types::signed_leb128::init_instance),
			reinterpret_cast<void*>(data_types::signed_leb128::delete_instance),
			reinterpret_cast<void*>(data_types::signed_leb128::assign_instance),
			reinterpret_cast<void*>(data_types::signed_leb128::change_byte_order),
			reinterpret_cast<void*>(data_types::signed_leb128::bytes_to_str),
			reinterpret_cast<void*>(data_types::signed_leb128::str_to_bytes)
			});
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}