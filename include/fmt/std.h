// Formatting library for C++ - formatters for standard library types
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#ifndef FMT_STD_H_
#define FMT_STD_H_

#include "format.h"
#include "ostream.h"

#ifndef FMT_MODULE
#  include <atomic>
#  include <bitset>
#  include <complex>
#  include <cstddef>     // std::byte
#  include <exception>   // std::exception
#  include <functional>  // std::reference_wrapper
#  include <memory>
#  include <thread>
#  include <type_traits>
#  include <typeinfo>  // std::type_info
#  include <utility>   // std::make_index_sequence

// Check FMT_CPLUSPLUS to suppress a bogus warning in MSVC.
#  if FMT_CPLUSPLUS >= 201703L
#    if FMT_HAS_INCLUDE(<filesystem>) && \
        (!defined(FMT_CPP_LIB_FILESYSTEM) || FMT_CPP_LIB_FILESYSTEM != 0)
#      include <filesystem>
#    endif
#    if FMT_HAS_INCLUDE(<variant>)
#      include <variant>
#    endif
#    if FMT_HAS_INCLUDE(<optional>)
#      include <optional>
#    endif
#  endif
// Use > instead of >= in the version check because <source_location> may be
// available after C++17 but before C++20 is marked as implemented.
#  if FMT_CPLUSPLUS > 201703L && FMT_HAS_INCLUDE(<source_location>)
#    include <source_location>
#  endif
#  if FMT_CPLUSPLUS > 202002L && FMT_HAS_INCLUDE(<expected>)
#    include <expected>
#  endif
#endif  // FMT_MODULE

#if FMT_HAS_INCLUDE(<version>)
#  include <version>
#endif

// GCC 4 does not support FMT_HAS_INCLUDE.
#if FMT_HAS_INCLUDE(<cxxabi.h>) || defined(__GLIBCXX__)
#  include <cxxabi.h>
// Android NDK with gabi++ library on some architectures does not implement
// abi::__cxa_demangle().
#  ifndef __GABIXX_CXXABI_H__
#    define FMT_HAS_ABI_CXA_DEMANGLE
#  endif
#endif

#ifdef FMT_CPP_LIB_FILESYSTEM
// Use the provided definition.
#elif defined(__cpp_lib_filesystem)
#  define FMT_CPP_LIB_FILESYSTEM __cpp_lib_filesystem
#else
#  define FMT_CPP_LIB_FILESYSTEM 0
#endif

#ifdef FMT_CPP_LIB_VARIANT
// Use the provided definition.
#elif defined(__cpp_lib_variant)
#  define FMT_CPP_LIB_VARIANT __cpp_lib_variant
#else
#  define FMT_CPP_LIB_VARIANT 0
#endif

FMT_BEGIN_NAMESPACE
namespace detail {

#ifdef FMT_USE_BITINT
// Use the provided definition.
#elif FMT_CLANG_VERSION >= 1500 && !defined(__CUDACC__)
#  define FMT_USE_BITINT 1
#else
#  define FMT_USE_BITINT 0
#endif

#if FMT_USE_BITINT
FMT_PRAGMA_CLANG(diagnostic ignored "-Wbit-int-extension")
template <int N> using bitint = _BitInt(N);
template <int N> using ubitint = unsigned _BitInt(N);
#else
template <int N> struct bitint {};
template <int N> struct ubitint {};
#endif  // FMT_USE_BITINT

#if FMT_CPP_LIB_FILESYSTEM

template <typename Char, typename PathChar>
auto get_path_string(const std::filesystem::path& p,
                     const std::basic_string<PathChar>& native) {
  if constexpr (std::is_same_v<Char, char> &&
                std::is_same_v<PathChar, wchar_t>) {
    return to_utf8<wchar_t>(native, to_utf8_error_policy::wtf);
  } else {
    return p.string<Char>();
  }
}

// Like needs_escape, but path-separator '\' is left alone so Windows paths
// stay readable in debug form (e.g. "C:\foo" not "C:\\foo"). Quotes, controls
// (\t \n \r, …), and other non-printables are still escaped.
inline auto needs_path_escape(uint32_t cp) -> bool {
  if (cp == '\\') return false;
  return needs_escape(cp);
}

template <typename Char>
auto find_path_escape(const Char* begin, const Char* end)
    -> find_escape_result<Char> {
  for (; begin != end; ++begin) {
    uint32_t cp = static_cast<unsigned_char<Char>>(*begin);
    if (sizeof(Char) == 1 && cp >= 0x80) continue;
    if (needs_path_escape(cp)) return {begin, begin + 1, cp};
  }
  return {begin, nullptr, 0};
}

inline auto find_path_escape(const char* begin, const char* end)
    -> find_escape_result<char> {
  if FMT_CONSTEXPR20 (!use_utf8) return find_path_escape<char>(begin, end);
  auto result = find_escape_result<char>{end, nullptr, 0};
  for_each_codepoint(string_view(begin, to_unsigned(end - begin)),
                     [&](uint32_t cp, string_view sv) {
                       if (needs_path_escape(cp)) {
                         result = {sv.begin(), sv.end(), cp};
                         return false;
                       }
                       return true;
                     });
  return result;
}

// Quoted debug form of a path string: escapes controls/quotes like strings,
// but does not double path-separator backslashes.
template <typename Char, typename OutputIt>
auto write_escaped_path_string(OutputIt out, basic_string_view<Char> str)
    -> OutputIt {
  *out++ = static_cast<Char>('"');
  auto begin = str.begin(), end = str.end();
  do {
    auto escape = find_path_escape(begin, end);
    out = copy<Char>(begin, escape.begin, out);
    begin = escape.end;
    if (!begin) break;
    out = write_escaped_cp<OutputIt, Char>(out, escape);
  } while (begin != end);
  *out++ = static_cast<Char>('"');
  return out;
}

template <typename Char, typename PathChar>
void write_escaped_path(basic_memory_buffer<Char>& quoted,
                        const std::filesystem::path& p,
                        const std::basic_string<PathChar>& native) {
  // Char = format output unit; PathChar = path::value_type (char or wchar_t).
  if constexpr (std::is_same_v<Char, char> &&
                std::is_same_v<PathChar, wchar_t>) {
    // Windows: native path is UTF-16/wchar_t, format output is char.
    // Escape in wide form first, then convert the escaped text to UTF-8.
    auto buf = basic_memory_buffer<wchar_t>();
    write_escaped_path_string<wchar_t>(std::back_inserter(buf),
                                       basic_string_view<wchar_t>(native));
    bool valid = to_utf8<wchar_t>::convert(quoted, {buf.data(), buf.size()});
    FMT_ASSERT(valid, "invalid utf16");
  } else if constexpr (std::is_same_v<Char, PathChar>) {
    // Same width for path and output (typical POSIX char path, or wpath →
    // wchar_t format): escape the native string directly.
    write_escaped_path_string<Char>(std::back_inserter(quoted),
                                    basic_string_view<Char>(native));
  } else {
    // Mismatched widths (e.g. char path into wchar_t format): convert the path
    // to the output character type, then escape.
    auto s = p.string<Char>();
    write_escaped_path_string<Char>(std::back_inserter(quoted),
                                    basic_string_view<Char>(s));
  }
}

#endif  // FMT_CPP_LIB_FILESYSTEM

#if defined(__cpp_lib_expected) || FMT_CPP_LIB_VARIANT

// Writes a variant/expected alternative in debug form (strings/chars escaped).
// Formats through a local context so the result always lands on `out` (needed
// when the caller buffers first and then applies outer fill/align/width).
template <typename Char, typename OutputIt, typename T>
FMT_CONSTEXPR auto write_escaped_alternative(OutputIt out, const T& v)
    -> OutputIt {
  if constexpr (has_to_string_view<T>::value)
    return write_escaped_string<Char>(out, detail::to_string_view(v));
  if constexpr (std::is_same_v<T, Char>) return write_escaped_char(out, v);

  formatter<std::remove_cv_t<T>, Char> underlying;
  maybe_set_debug_format(underlying, true);
  auto fctx = basic_format_context<OutputIt, Char>(out, {}, {});
  return underlying.format(v, fctx);
}
#endif

#if FMT_CPP_LIB_VARIANT

template <typename> struct is_variant_like_ : std::false_type {};
template <typename... Types>
struct is_variant_like_<std::variant<Types...>> : std::true_type {};

template <typename Variant, typename Char> class is_variant_formattable {
  template <size_t... Is>
  static auto check(std::index_sequence<Is...>) -> std::conjunction<
      is_formattable<std::variant_alternative_t<Is, Variant>, Char>...>;

 public:
  static constexpr bool value = decltype(check(
      std::make_index_sequence<std::variant_size<Variant>::value>()))::value;
};

#endif  // FMT_CPP_LIB_VARIANT

#if FMT_USE_RTTI
inline auto normalize_libcxx_inline_namespaces(string_view demangled_name_view,
                                               char* begin) -> string_view {
  // Normalization of stdlib inline namespace names.
  // libc++ inline namespaces.
  //  std::__1::*       -> std::*
  //  std::__1::__fs::* -> std::*
  // libstdc++ inline namespaces.
  //  std::__cxx11::*             -> std::*
  //  std::filesystem::__cxx11::* -> std::filesystem::*
  //
  // Only strip "__identifier::" where identifier is [A-Za-z0-9_]+. Do not scan
  // for "::" past other characters — that would mangle template names such as
  // std::__nested<std::logic_error> into "std::logic_error>" (stray '>').
  if (demangled_name_view.starts_with("std::")) {
    char* to = begin + 5;  // std::
    for (const char *from = to, *end = begin + demangled_name_view.size();
         from < end;) {
      // This is safe, because demangled_name is NUL-terminated.
      if (from[0] == '_' && from[1] == '_') {
        const char* next = from + 2;
        while (next < end && ((*next >= '0' && *next <= '9') ||
                              (*next >= 'A' && *next <= 'Z') ||
                              (*next >= 'a' && *next <= 'z') || *next == '_'))
          ++next;
        if (next + 1 < end && next[0] == ':' && next[1] == ':') {
          from = next + 2;
          continue;
        }
      }
      *to++ = *from++;
    }
    demangled_name_view = {begin, detail::to_unsigned(to - begin)};
  }
  return demangled_name_view;
}

template <class OutputIt>
auto normalize_msvc_abi_name(string_view abi_name_view, OutputIt out)
    -> OutputIt {
  const string_view demangled_name(abi_name_view);
  for (size_t i = 0; i < demangled_name.size(); ++i) {
    auto sub = demangled_name;
    sub.remove_prefix(i);
    if (sub.starts_with("enum ")) {
      i += 4;
      continue;
    }
    if (sub.starts_with("class ") || sub.starts_with("union ")) {
      i += 5;
      continue;
    }
    if (sub.starts_with("struct ")) {
      i += 6;
      continue;
    }
    if (*sub.begin() != ' ') *out++ = *sub.begin();
  }
  return out;
}

template <typename OutputIt>
auto write_demangled_name(OutputIt out, const std::type_info& ti) -> OutputIt {
#  ifdef FMT_HAS_ABI_CXA_DEMANGLE
  int status = 0;
  size_t size = 0;
  std::unique_ptr<char, void (*)(void*)> demangled_name_ptr(
      abi::__cxa_demangle(ti.name(), nullptr, &size, &status), &free);

  if (demangled_name_ptr && status == 0) {
    string_view demangled_name_view = normalize_libcxx_inline_namespaces(
        demangled_name_ptr.get(), demangled_name_ptr.get());
    return detail::write_bytes<char>(out, demangled_name_view);
  }
  // Demangle unavailable at runtime, or failed: fall back to the raw
  // mangled ABI name.
  return detail::write_bytes<char>(out, string_view(ti.name()));
#  elif FMT_MSC_VERSION && defined(_MSVC_STL_UPDATE)
  // MSVC's type_info::name() is already a readable decorated name, not
  // Itanium-mangled; strip MSVC prefixes (class/struct/...).
  return normalize_msvc_abi_name(ti.name(), out);
#  elif FMT_MSC_VERSION && defined(_LIBCPP_VERSION)
  const string_view demangled_name = ti.name();
  std::string name_copy(demangled_name.size(), '\0');
  // normalize_msvc_abi_name removes class, struct, union etc that MSVC has in
  // front of types
  name_copy.erase(normalize_msvc_abi_name(demangled_name, name_copy.begin()),
                  name_copy.end());
  // normalize_libcxx_inline_namespaces removes the inline __1, __2, etc
  // namespaces libc++ uses for ABI versioning On MSVC ABI + libc++
  // environments, we need to eliminate both of them.
  const string_view normalized_name =
      normalize_libcxx_inline_namespaces(name_copy, name_copy.data());
  return detail::write_bytes<char>(out, normalized_name);
#  else
  // No demangler in this build (e.g. no <cxxabi.h> / gabi++), fall back to
  // the raw mangled name.
  return detail::write_bytes<char>(out, string_view(ti.name()));
#  endif
}

#endif  // FMT_USE_RTTI

template <typename T, typename Enable = void>
struct has_flip : std::false_type {};

template <typename T>
struct has_flip<T, void_t<decltype(std::declval<T>().flip())>>
    : std::true_type {};

template <typename T> struct is_bit_reference_like {
  static constexpr bool value = std::is_convertible<T, bool>::value &&
                                std::is_nothrow_assignable<T, bool>::value &&
                                has_flip<T>::value;
};

// Workaround for libc++ incompatibility with C++ standard.
// According to the Standard, `bitset::operator[] const` returns bool.
#if defined(_LIBCPP_VERSION) && !defined(FMT_IMPORT_STD)
template <typename C>
struct is_bit_reference_like<std::__bit_const_reference<C>> {
  static constexpr bool value = true;
};
#endif

template <typename T, typename Enable = void>
struct has_format_as : std::false_type {};
template <typename T>
struct has_format_as<T, void_t<decltype(format_as(std::declval<const T&>()))>>
    : std::true_type {};

template <typename T, typename Enable = void>
struct has_format_as_member : std::false_type {};
template <typename T>
struct has_format_as_member<
    T, void_t<decltype(formatter<T>::format_as(std::declval<const T&>()))>>
    : std::true_type {};

}  // namespace detail

template <typename T, typename Deleter>
auto ptr(const std::unique_ptr<T, Deleter>& p) -> const void* {
  return p.get();
}
template <typename T> auto ptr(const std::shared_ptr<T>& p) -> const void* {
  return p.get();
}

#if FMT_CPP_LIB_FILESYSTEM

template <typename Char> struct formatter<std::filesystem::path, Char> {
 private:
  format_specs specs_;
  detail::arg_ref<Char> width_ref_;
  bool debug_ = false;
  char path_type_ = 0;

 public:
  FMT_CONSTEXPR void set_debug_format(bool set = true) { debug_ = set; }

  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) {
    auto it = ctx.begin(), end = ctx.end();
    if (it == end) return it;

    it = detail::parse_align(it, end, specs_);
    if (it == end) return it;

    Char c = *it;
    if ((c >= '0' && c <= '9') || c == '{')
      it = detail::parse_width(it, end, specs_, width_ref_, ctx);

    // Optional '?' (debug) and 'g' (generic path separators). Independent;
    // either order; each at most once in the format string. debug_ may already
    // be set via set_debug_format (e.g. optional/range nesting) — still consume
    // a format '?' so it is not reported as unknown. A second '?' / 'g' is left
    // for the caller to reject.
    bool saw_debug = false, saw_generic = false;
    while (it != end) {
      if (*it == '?' && !saw_debug) {
        saw_debug = true;
        debug_ = true;
        ++it;
      } else if (*it == 'g' && !saw_generic) {
        saw_generic = true;
        path_type_ = 'g';
        ++it;
      } else {
        break;
      }
    }
    return it;
  }

  template <typename FormatContext>
  auto format(const std::filesystem::path& p, FormatContext& ctx) const {
    auto specs = specs_;
    // 'g' selects generic (usually '/') separators; otherwise the native form.
    auto path_string =
        !path_type_ ? p.native()
                    : p.generic_string<std::filesystem::path::value_type>();

    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width, width_ref_,
                                ctx);
    // Non-debug: write the path text as-is (no quoting or escaping). Width
    // and fill apply to that raw string. Debug ('?'): build a quoted/escaped
    // buffer first, then apply width/fill to the escaped form.
    if (!debug_) {
      auto s = detail::get_path_string<Char>(p, path_string);
      return detail::write(ctx.out(), basic_string_view<Char>(s), specs);
    }
    auto quoted = basic_memory_buffer<Char>();
    detail::write_escaped_path(quoted, p, path_string);
    return detail::write(ctx.out(),
                         basic_string_view<Char>(quoted.data(), quoted.size()),
                         specs);
  }
};

class path : public std::filesystem::path {
 public:
  auto display_string() const -> std::string {
    const std::filesystem::path& base = *this;
    return fmt::format(FMT_STRING("{}"), base);
  }
  auto system_string() const -> std::string { return string(); }

  auto generic_display_string() const -> std::string {
    const std::filesystem::path& base = *this;
    return fmt::format(FMT_STRING("{:g}"), base);
  }
  auto generic_system_string() const -> std::string { return generic_string(); }
};

#endif  // FMT_CPP_LIB_FILESYSTEM

// Formats a bitset as a binary digit string (MSB first), e.g. "101010".
// Supports fill / align / width like a string. The '#' alternate form inserts
// a space every four bits (from the right), e.g. "10 1010" for 6 bits, which
// keeps wider bitsets readable.
template <size_t N, typename Char>
struct formatter<std::bitset<N>, Char> {
 private:
  format_specs specs_;
  detail::arg_ref<Char> width_ref_;
  bool grouped_ = false;

 public:
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) -> const Char* {
    auto it = ctx.begin(), end = ctx.end();
    if (it == end || *it == '}') return it;

    it = detail::parse_align(it, end, specs_);
    // '#' may appear before or after width (e.g. "{:#}", "{:#16}", "{:*>16#}").
    if (it != end && *it == '#') {
      grouped_ = true;
      ++it;
    }
    if (it != end && ((*it >= '0' && *it <= '9') || *it == '{'))
      it = detail::parse_width(it, end, specs_, width_ref_, ctx);
    if (it != end && *it == '#') {
      grouped_ = true;
      ++it;
    }
    return it;
  }

  template <typename FormatContext>
  auto format(const std::bitset<N>& bs, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto specs = specs_;
    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width, width_ref_,
                                ctx);

    auto buf = basic_memory_buffer<Char>();
    auto out = basic_appender<Char>(buf);
    // Emit MSB first. With grouping, put a space before every bit whose index
    // from the LSB is a multiple of 4 (except at the start), so the rightmost
    // group is always 4 bits when N >= 4: e.g. N=10 → "10 1010 1100".
    for (size_t pos = N; pos > 0; --pos) {
      if (grouped_ && pos != N && pos % 4 == 0) *out++ = Char(' ');
      *out++ = bs[pos - 1] ? Char('1') : Char('0');
    }
    return detail::write(
        ctx.out(), basic_string_view<Char>(buf.data(), buf.size()), specs);
  }
};

template <typename Char>
struct formatter<std::thread::id, Char> : basic_ostream_formatter<Char> {};

#ifdef __cpp_lib_optional
template <typename T, typename Char>
struct formatter<std::optional<T>, Char,
                 std::enable_if_t<is_formattable<T, Char>::value>> {
 private:
  formatter<std::remove_cv_t<T>, Char> underlying_;
  bool no_wrapper_ = false;
  static constexpr basic_string_view<Char> optional =
      detail::string_literal<Char, 'o', 'p', 't', 'i', 'o', 'n', 'a', 'l',
                             '('>{};
  static constexpr basic_string_view<Char> none =
      detail::string_literal<Char, 'n', 'o', 'n', 'e'>{};

 public:
  // Parses optional-level flags then underlying specs for T:
  //
  //   {:} / {:?}   optional(value) / none  (value in debug form)
  //   {:n}         drop the optional(...) wrapper (like ranges' {:n});
  //                empty is still the bare word "none"
  //   {:nx}        no wrapper + underlying presentation (e.g. hex)
  //
  // '?' and 'n' may appear in either order, each at most once; remaining
  // specs are forwarded to T.
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) {
    auto it = ctx.begin(), end = ctx.end();
    bool saw_debug = false, saw_n = false;
    while (it != end) {
      if (*it == static_cast<Char>('?') && !saw_debug) {
        saw_debug = true;
        ++it;
      } else if (*it == static_cast<Char>('n') && !saw_n) {
        saw_n = true;
        no_wrapper_ = true;
        ++it;
      } else {
        break;
      }
    }
    ctx.advance_to(it);

    // Always format the contained value in debug form so strings and chars
    // stay quoted (optional("text") / with n: "text") and nested values stay
    // readable. Independent of whether the outer format used '?'. Presentation
    // specs (e.g. 'x', precision, width) still go through to T below.
    detail::maybe_set_debug_format(underlying_, true);
    it = underlying_.parse(ctx);
    // Re-apply: some presentation types (notably 's') clear debug on parse.
    detail::maybe_set_debug_format(underlying_, true);
    return it;
  }

  template <typename FormatContext>
  auto format(const std::optional<T>& opt, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    // Empty optional is always the bare word "none" — never quoted/escaped.
    if (!opt) return detail::write<Char>(ctx.out(), none);

    // '{:n}': format only the contained value (no optional(...) wrapper).
    if (no_wrapper_) return underlying_.format(*opt, ctx);

    auto out = ctx.out();
    out = detail::write<Char>(out, optional);
    ctx.advance_to(out);
    // Forward the (already-parsed) specs to the contained value.
    out = underlying_.format(*opt, ctx);
    return detail::write(out, ')');
  }
};
#endif  // __cpp_lib_optional

#ifdef __cpp_lib_expected
template <typename T, typename E, typename Char>
struct formatter<std::expected<T, E>, Char,
                 std::enable_if_t<(std::is_void<T>::value ||
                                   is_formattable<T, Char>::value) &&
                                  is_formattable<E, Char>::value>> {
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) -> const Char* {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const std::expected<T, E>& value, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto out = ctx.out();

    if (value.has_value()) {
      // write_bytes: ASCII labels work for char and wchar_t.
      out = detail::write_bytes<Char>(out, "expected(");
      if constexpr (!std::is_void<T>::value)
        out = detail::write_escaped_alternative<Char>(out, *value);
    } else {
      out = detail::write_bytes<Char>(out, "unexpected(");
      out = detail::write_escaped_alternative<Char>(out, value.error());
    }
    *out++ = static_cast<Char>(')');
    return out;
  }
};

template <typename E, typename Char>
struct formatter<std::unexpected<E>, Char,
                 std::enable_if_t<is_formattable<E, Char>::value>> {
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) -> const Char* {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const std::unexpected<E>& value, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto out = ctx.out();

    out = detail::write_bytes<Char>(out, "unexpected(");
    out = detail::write_escaped_alternative<Char>(out, value.error());

    *out++ = static_cast<Char>(')');
    return out;
  }
};
#endif  // __cpp_lib_expected

#ifdef __cpp_lib_source_location
template <> struct formatter<std::source_location> {
  FMT_CONSTEXPR auto parse(parse_context<>& ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const std::source_location& loc, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto out = ctx.out();
    out = detail::write(out, loc.file_name());
    out = detail::write(out, ':');
    out = detail::write<char>(out, loc.line());
    out = detail::write(out, ':');
    out = detail::write<char>(out, loc.column());
    out = detail::write(out, ": ");
    out = detail::write(out, loc.function_name());
    return out;
  }
};
#endif

#if FMT_CPP_LIB_VARIANT

template <typename T> struct is_variant_like {
  static constexpr bool value = detail::is_variant_like_<T>::value;
};

template <typename Char> struct formatter<std::monostate, Char> {
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) -> const Char* {
    return ctx.begin();
  }

  template <typename FormatContext>
  FMT_CONSTEXPR auto format(const std::monostate&, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    // write_bytes so the ASCII label works for char and wchar_t alike.
    return detail::write_bytes<Char>(ctx.out(), "monostate");
  }
};

// Formats std::variant as variant(<active alternative>). Supports fill /
// align / width like error_code (content is built first, then padded as a
// string). '{:n}' drops the variant(...) wrapper (same idea as optional /
// ranges). Presentation types for the active alternative are not accepted.
template <typename Variant, typename Char>
struct formatter<Variant, Char,
                 std::enable_if_t<std::conjunction_v<
                     is_variant_like<Variant>,
                     detail::is_variant_formattable<Variant, Char>>>> {
 private:
  format_specs specs_;
  detail::arg_ref<Char> width_ref_;
  bool no_wrapper_ = false;

 public:
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) -> const Char* {
    auto it = ctx.begin(), end = ctx.end();
    if (it == end || *it == '}') return it;

    // 'n' may appear before or after fill/align/width ("{:n}", "{:n>10}",
    // "{:>10n}"), same placement flexibility as exception's 't'.
    if (it != end && *it == static_cast<Char>('n')) {
      no_wrapper_ = true;
      ++it;
    }
    if (it != end && *it != '}') {
      it = detail::parse_align(it, end, specs_);
      if (it != end && ((*it >= '0' && *it <= '9') || *it == '{'))
        it = detail::parse_width(it, end, specs_, width_ref_, ctx);
    }
    if (it != end && *it == static_cast<Char>('n')) {
      no_wrapper_ = true;
      ++it;
    }
    return it;
  }

  template <typename FormatContext>
  FMT_CONSTEXPR20 auto format(const Variant& value, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto specs = specs_;
    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width, width_ref_,
                                ctx);
    if (specs.width == 0) return write(ctx.out(), value);

    // Build full content then apply width/align/fill.
    auto buf = basic_memory_buffer<Char>();
    write(basic_appender<Char>(buf), value);
    return detail::write(
        ctx.out(), basic_string_view<Char>(buf.data(), buf.size()), specs);
  }

 private:
  template <typename OutputIt>
  FMT_CONSTEXPR20 auto write(OutputIt out, const Variant& value) const
      -> OutputIt {
    FMT_TRY {
      std::visit(
          [&](const auto& v) {
            // write_bytes: ASCII "variant(" works for char and wchar_t.
            if (!no_wrapper_) out = detail::write_bytes<Char>(out, "variant(");
            out = detail::write_escaped_alternative<Char>(out, v);
            if (!no_wrapper_) *out++ = static_cast<Char>(')');
          },
          value);
    }
    FMT_CATCH(const std::bad_variant_access&) {
      // Valueless: still bare "valueless by exception" with '{:n}', otherwise
      // wrapped like a normal alternative.
      if (!no_wrapper_) out = detail::write_bytes<Char>(out, "variant(");
      out = detail::write_bytes<Char>(out, "valueless by exception");
      if (!no_wrapper_) *out++ = static_cast<Char>(')');
    }
    return out;
  }
};

#endif  // FMT_CPP_LIB_VARIANT

// Formats std::error_code. Default is "category:value" (e.g. "generic:42").
//
//   {:s}   platform message text (ec.message())
//   {:n}   value only — drop the "category:" prefix (same idea as optional /
//          variant '{:n}' dropping their wrapper)
//   {:?}   debug-quoted form of the chosen text
//
// Fill / align / width pad the whole resulting string (including after 's' /
// 'n' / '?'). 'n' may appear before or after fill/align/width.
template <> struct formatter<std::error_code> {
 private:
  format_specs specs_;
  detail::arg_ref<char> width_ref_;
  bool debug_ = false;
  bool value_only_ = false;

 public:
  FMT_CONSTEXPR void set_debug_format(bool set = true) { debug_ = set; }

  FMT_CONSTEXPR auto parse(parse_context<>& ctx) -> const char* {
    auto it = ctx.begin(), end = ctx.end();
    if (it == end || *it == '}') return it;

    // 'n' may appear before or after fill/align/width ("{:n}", "{:n>8}",
    // "{:>8n}"), same placement as variant / exception 't'.
    if (it != end && *it == 'n') {
      value_only_ = true;
      ++it;
    }
    if (it != end && *it != '}') {
      it = detail::parse_align(it, end, specs_);
      if (it != end && ((*it >= '0' && *it <= '9') || *it == '{'))
        it = detail::parse_width(it, end, specs_, width_ref_, ctx);
    }
    if (it != end && *it == 'n') {
      value_only_ = true;
      ++it;
    }
    if (it != end && *it == '?') {
      debug_ = true;
      ++it;
    }
    if (it != end && *it == 's') {
      specs_.set_type(presentation_type::string);
      ++it;
    }
    return it;
  }

  template <typename FormatContext>
  FMT_CONSTEXPR20 auto format(const std::error_code& ec,
                              FormatContext& ctx) const -> decltype(ctx.out()) {
    auto specs = specs_;
    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width, width_ref_,
                                ctx);
    auto buf = memory_buffer();
    if (specs_.type() == presentation_type::string) {
      // '{:s}' — message text; 'n' has no effect (no category prefix to drop).
      buf.append(ec.message());
    } else if (value_only_) {
      // '{:n}' — numeric value only.
      detail::write<char>(appender(buf), ec.value());
    } else {
      buf.append(string_view(ec.category().name()));
      buf.push_back(':');
      detail::write<char>(appender(buf), ec.value());
    }
    auto quoted = memory_buffer();
    auto str = string_view(buf.data(), buf.size());
    if (debug_) {
      detail::write_escaped_string<char>(std::back_inserter(quoted), str);
      str = string_view(quoted.data(), quoted.size());
    }
    // Width / align / fill always apply to the full content string.
    return detail::write<char>(ctx.out(), str, specs);
  }
};

#if FMT_USE_RTTI
// Formats a demangled type name. Supports the same width/align fill handling
// as formatter<std::error_code> (build the name, then pad as a string).
template <> struct formatter<std::type_info> {
 private:
  format_specs specs_;
  detail::arg_ref<char> width_ref_;

 public:
  FMT_CONSTEXPR auto parse(parse_context<>& ctx) -> const char* {
    auto it = ctx.begin(), end = ctx.end();
    if (it == end) return it;

    it = detail::parse_align(it, end, specs_);

    if (it != end && ((*it >= '0' && *it <= '9') || *it == '{'))
      it = detail::parse_width(it, end, specs_, width_ref_, ctx);
    return it;
  }

  template <typename Context>
  auto format(const std::type_info& ti, Context& ctx) const
      -> decltype(ctx.out()) {
    auto specs = specs_;
    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width, width_ref_,
                                ctx);
    auto buf = memory_buffer();
    detail::write_demangled_name(appender(buf), ti);
    return detail::write<char>(
        ctx.out(), string_view(buf.data(), buf.size()), specs);
  }
};
#endif  // FMT_USE_RTTI

// Formats std::exception (and derived types). Optional 't' includes the
// demangled dynamic type name. Supports fill / align / width like error_code
// and type_info (content is built first, then padded as a string).
template <typename T>
struct formatter<
    T, char,
    typename std::enable_if<std::is_base_of<std::exception, T>::value>::type> {
 private:
  format_specs specs_;
  detail::arg_ref<char> width_ref_;
  bool with_typename_ = false;

 public:
  FMT_CONSTEXPR auto parse(parse_context<>& ctx) -> const char* {
    auto it = ctx.begin();
    auto end = ctx.end();
    if (it == end || *it == '}') return it;

    // 't' may appear before fill/align/width ("{:t>40}") or after ("{:>40t}").
    if (it != end && *it == 't') {
      ++it;
      with_typename_ = FMT_USE_RTTI != 0;
    }
    if (it != end && *it != '}') {
      it = detail::parse_align(it, end, specs_);
      if (it != end && ((*it >= '0' && *it <= '9') || *it == '{'))
        it = detail::parse_width(it, end, specs_, width_ref_, ctx);
    }
    if (it != end && *it == 't') {
      ++it;
      with_typename_ = FMT_USE_RTTI != 0;
    }
    return it;
  }

  template <typename Context>
  auto format(const std::exception& ex, Context& ctx) const
      -> decltype(ctx.out()) {
    auto specs = specs_;
    detail::handle_dynamic_spec(specs.dynamic_width(), specs.width, width_ref_,
                                ctx);
    if (specs.width == 0) return write(ctx.out(), ex);

    // Build full message (including nested chain) then apply width/align.
    auto buf = memory_buffer();
    write(appender(buf), ex);
    return detail::write<char>(
        ctx.out(), string_view(buf.data(), buf.size()), specs);
  }

 private:
  template <typename OutputIt>
  auto write(OutputIt out, const std::exception& ex) const -> OutputIt {
#if FMT_USE_RTTI
    if (with_typename_) {
      out = detail::write_demangled_name(out, typeid(ex));
      *out++ = ':';
      *out++ = ' ';
    }
#endif  // FMT_USE_RTTI
    out = detail::write_bytes<char>(out, string_view(ex.what()));
#if FMT_USE_RTTI
    // If the exception carries a nested exception (e.g. via
    // std::throw_with_nested), format the whole chain.
    if (auto* nested = dynamic_cast<const std::nested_exception*>(&ex)) {
      if (auto ep = nested->nested_ptr()) {
        out = detail::write(out, string_view(": "));
        try {
          std::rethrow_exception(ep);
        } catch (const std::exception& nested_ex) {
          out = write(out, nested_ex);
        } catch (...) {
          out = detail::write(out, string_view("unknown exception"));
        }
      }
    }
#endif  // FMT_USE_RTTI
    return out;
  }
};

template <> struct formatter<std::exception_ptr> : formatter<std::exception> {
  template <typename FormatContext>
  auto format(const std::exception_ptr& ep, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    if (!ep) return detail::write(ctx.out(), string_view("none"));
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      return formatter<std::exception>::format(e, ctx);
    } catch (...) {
      return detail::write(ctx.out(), string_view("unknown exception"));
    }
  }
};

template <int N, typename Char>
struct formatter<detail::bitint<N>, Char> : formatter<long long, Char> {
  static_assert(N <= 64, "unsupported _BitInt");
  static auto format_as(detail::bitint<N> x) -> long long {
    return static_cast<long long>(x);
  }
  template <typename Context>
  auto format(detail::bitint<N> x, Context& ctx) const -> decltype(ctx.out()) {
    return formatter<long long, Char>::format(format_as(x), ctx);
  }
};

template <int N, typename Char>
struct formatter<detail::ubitint<N>, Char> : formatter<ullong, Char> {
  static_assert(N <= 64, "unsupported _BitInt");
  static auto format_as(detail::ubitint<N> x) -> ullong {
    return static_cast<ullong>(x);
  }
  template <typename Context>
  auto format(detail::ubitint<N> x, Context& ctx) const -> decltype(ctx.out()) {
    return formatter<ullong, Char>::format(format_as(x), ctx);
  }
};

// We can't use std::vector<bool, Allocator>::reference and
// std::bitset<N>::reference because the compiler can't deduce Allocator and N
// in partial specialization.
template <typename BitRef, typename Char>
struct formatter<BitRef, Char,
                 enable_if_t<detail::is_bit_reference_like<BitRef>::value>>
    : formatter<bool, Char> {
  template <typename FormatContext>
  FMT_CONSTEXPR auto format(const BitRef& v, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    return formatter<bool, Char>::format(v, ctx);
  }
};

#ifdef __cpp_lib_byte
template <typename Char>
struct formatter<std::byte, Char> : formatter<unsigned, Char> {
  FMT_CONSTEXPR static auto format_as(std::byte b) -> unsigned char {
    return static_cast<unsigned char>(b);
  }
  template <typename Context>
  FMT_CONSTEXPR auto format(std::byte b, Context& ctx) const
      -> decltype(ctx.out()) {
    return formatter<unsigned, Char>::format(format_as(b), ctx);
  }
};
#endif

template <typename T, typename Char>
struct formatter<std::atomic<T>, Char,
                 enable_if_t<is_formattable<T, Char>::value>>
    : formatter<T, Char> {
  template <typename FormatContext>
  auto format(const std::atomic<T>& v, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    return formatter<T, Char>::format(v.load(), ctx);
  }
};

#ifdef __cpp_lib_atomic_flag_test
template <typename Char>
struct formatter<std::atomic_flag, Char> : formatter<bool, Char> {
  template <typename FormatContext>
  auto format(const std::atomic_flag& v, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    return formatter<bool, Char>::format(v.test(), ctx);
  }
};
#endif  // __cpp_lib_atomic_flag_test

template <typename T> struct is_tuple_like;

template <typename T>
struct is_tuple_like<std::complex<T>> : std::false_type {};

template <typename T, typename Char> struct formatter<std::complex<T>, Char> {
 private:
  detail::dynamic_format_specs<Char> specs_;

  template <typename FormatContext, typename OutputIt>
  FMT_CONSTEXPR auto do_format(const std::complex<T>& c,
                               detail::dynamic_format_specs<Char>& specs,
                               FormatContext& ctx, OutputIt out) const
      -> OutputIt {
    if (c.real() != 0) {
      *out++ = Char('(');
      out = detail::write<Char>(out, c.real(), specs, ctx.locale());
      specs.set_sign(sign::plus);
      out = detail::write<Char>(out, c.imag(), specs, ctx.locale());
      if (!detail::isfinite(c.imag())) *out++ = Char(' ');
      *out++ = Char('i');
      *out++ = Char(')');
      return out;
    }
    out = detail::write<Char>(out, c.imag(), specs, ctx.locale());
    if (!detail::isfinite(c.imag())) *out++ = Char(' ');
    *out++ = Char('i');
    return out;
  }

 public:
  FMT_CONSTEXPR auto parse(parse_context<Char>& ctx) -> const Char* {
    if (ctx.begin() == ctx.end() || *ctx.begin() == '}') return ctx.begin();
    return parse_format_specs(ctx.begin(), ctx.end(), specs_, ctx,
                              detail::type_constant<T, Char>::value);
  }

  template <typename FormatContext>
  auto format(const std::complex<T>& c, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    auto specs = specs_;
    if (specs.dynamic()) {
      detail::handle_dynamic_spec(specs.dynamic_width(), specs.width,
                                  specs.width_ref, ctx);
      detail::handle_dynamic_spec(specs.dynamic_precision(), specs.precision,
                                  specs.precision_ref, ctx);
    }

    if (specs.width == 0) return do_format(c, specs, ctx, ctx.out());
    auto buf = basic_memory_buffer<Char>();

    auto outer_specs = format_specs();
    outer_specs.width = specs.width;
    outer_specs.copy_fill_from(specs);
    outer_specs.set_align(specs.align());

    specs.width = 0;
    specs.set_fill({});
    specs.set_align(align::none);

    do_format(c, specs, ctx, basic_appender<Char>(buf));
    return detail::write<Char>(ctx.out(),
                               basic_string_view<Char>(buf.data(), buf.size()),
                               outer_specs);
  }
};

template <typename T, typename Char>
struct formatter<std::reference_wrapper<T>, Char,
                 // Guard against format_as because reference_wrapper is
                 // implicitly convertible to T&.
                 enable_if_t<is_formattable<remove_cvref_t<T>, Char>::value &&
                             !detail::has_format_as<T>::value &&
                             !detail::has_format_as_member<T>::value>>
    : formatter<remove_cvref_t<T>, Char> {
  template <typename FormatContext>
  auto format(std::reference_wrapper<T> ref, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    return formatter<remove_cvref_t<T>, Char>::format(ref.get(), ctx);
  }
};

FMT_END_NAMESPACE

#endif  // FMT_STD_H_
