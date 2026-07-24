// Formatting library for C++ - formatting library tests
//
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// All rights reserved.
//
// For the license information refer to format.h.

#include "fmt/xchar.h"

#include <bitset>
#include <complex>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "fmt/chrono.h"
#include "fmt/color.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"
#include "fmt/std.h"
#include "gtest-extra.h"  // Contains
#include "util.h"         // get_locale

using fmt::detail::max_value;
using testing::Contains;

#if defined(__MINGW32__) && !defined(_UCRT)
// Only C89 conversion specifiers when using MSVCRT instead of UCRT
#  define FMT_HAS_C99_STRFTIME 0
#else
#  define FMT_HAS_C99_STRFTIME 1
#endif

struct non_string {};

template <typename T> class has_to_string_view_test : public testing::Test {};

using string_char_types = testing::Types<char, wchar_t, char16_t, char32_t>;
TYPED_TEST_SUITE(has_to_string_view_test, string_char_types);

template <typename Char>
struct derived_from_string_view : fmt::basic_string_view<Char> {};

TYPED_TEST(has_to_string_view_test, has_to_string_view) {
  EXPECT_TRUE(fmt::detail::has_to_string_view<TypeParam*>::value);
  EXPECT_TRUE(fmt::detail::has_to_string_view<const TypeParam*>::value);
  EXPECT_TRUE(fmt::detail::has_to_string_view<TypeParam[2]>::value);
  EXPECT_TRUE(fmt::detail::has_to_string_view<const TypeParam[2]>::value);
  EXPECT_TRUE(
      fmt::detail::has_to_string_view<std::basic_string<TypeParam>>::value);
  EXPECT_TRUE(fmt::detail::has_to_string_view<
              fmt::basic_string_view<TypeParam>>::value);
  EXPECT_TRUE(fmt::detail::has_to_string_view<
              derived_from_string_view<TypeParam>>::value);
  using fmt_string_view = fmt::detail::std_string_view<TypeParam>;
  EXPECT_TRUE(std::is_empty<fmt_string_view>::value !=
              fmt::detail::has_to_string_view<fmt_string_view>::value);
  EXPECT_FALSE(fmt::detail::has_to_string_view<non_string>::value);
}

// std::is_constructible is broken in MSVC until version 2015.
#if !FMT_MSC_VERSION || FMT_MSC_VERSION >= 1900
struct explicitly_convertible_to_wstring_view {
  explicit operator fmt::wstring_view() const { return L"foo"; }
};

TEST(xchar_test, format_explicitly_convertible_to_wstring_view) {
  // Types explicitly convertible to wstring_view are not formattable by
  // default because it may introduce ODR violations.
  static_assert(
      !fmt::is_formattable<explicitly_convertible_to_wstring_view>::value, "");
}
#endif

TEST(xchar_test, format) {
  EXPECT_EQ(fmt::format(L"{}", 42), L"42");
  EXPECT_EQ(fmt::format(L"{}", 4.2), L"4.2");
  EXPECT_EQ(fmt::format(L"{}", 1e100), L"1e+100");
  EXPECT_EQ(fmt::format(L"{}", L"abc"), L"abc");
  EXPECT_EQ(fmt::format(L"{}", L'z'), L"z");
  EXPECT_THROW(fmt::format(fmt::runtime(L"{:*\x343E}"), 42), fmt::format_error);
  EXPECT_EQ(fmt::format(L"{}", true), L"true");
  EXPECT_EQ(fmt::format(L"{0}", L'a'), L"a");
  EXPECT_EQ(fmt::format(L"Letter {}", L'\x40e'), L"Letter \x40e");  // Ў
  if (sizeof(wchar_t) == 4)
    EXPECT_EQ(fmt::format(fmt::runtime(L"{:𓀨>3}"), 42), L"𓀨42");
  EXPECT_EQ(fmt::format(L"{}c{}", L"ab", 1), L"abc1");
}

TEST(xchar_test, is_formattable) {
  static_assert(!fmt::is_formattable<const wchar_t*>::value, "");
}

TEST(xchar_test, compile_time_string) {
  EXPECT_EQ(fmt::format(fmt::wformat_string<int>(L"{}"), 42), L"42");
#if defined(FMT_USE_STRING_VIEW) && FMT_CPLUSPLUS >= 201703L
  EXPECT_EQ(fmt::format(FMT_STRING(std::wstring_view(L"{}")), 42), L"42");
#endif
}

TEST(xchar_test, format_to) {
  auto buf = std::vector<wchar_t>();
  fmt::format_to(std::back_inserter(buf), L"{}{}", 42, L'\0');
  EXPECT_STREQ(buf.data(), L"42");
}

TEST(xchar_test, compile_time_string_format_to) {
  std::wstring ws;
  fmt::format_to(std::back_inserter(ws), FMT_STRING(L"{}"), 42);
  EXPECT_EQ(L"42", ws);
}

TEST(xchar_test, vformat_to) {
  int n = 42;
  auto args = fmt::make_wformat_args(n);
  auto w = std::wstring();
  fmt::vformat_to(std::back_inserter(w), L"{}", args);
  EXPECT_EQ(L"42", w);
}

namespace test {
struct struct_as_wstring_view {};
auto format_as(struct_as_wstring_view) -> fmt::wstring_view { return L"foo"; }
}  // namespace test

TEST(xchar_test, format_as) {
  EXPECT_EQ(fmt::format(L"{}", test::struct_as_wstring_view()), L"foo");
}

TEST(format_test, wide_format_to_n) {
  wchar_t buffer[4];
  buffer[3] = L'x';
  auto result = fmt::format_to_n(buffer, 3, L"{}", 12345);
  EXPECT_EQ(5u, result.size);
  EXPECT_EQ(buffer + 3, result.out);
  EXPECT_EQ(L"123x", fmt::wstring_view(buffer, 4));
  buffer[0] = L'x';
  buffer[1] = L'x';
  buffer[2] = L'x';
  result = fmt::format_to_n(buffer, 3, L"{}", L'A');
  EXPECT_EQ(1u, result.size);
  EXPECT_EQ(buffer + 1, result.out);
  EXPECT_EQ(L"Axxx", fmt::wstring_view(buffer, 4));
  result = fmt::format_to_n(buffer, 3, L"{}{} ", L'B', L'C');
  EXPECT_EQ(3u, result.size);
  EXPECT_EQ(buffer + 3, result.out);
  EXPECT_EQ(L"BC x", fmt::wstring_view(buffer, 4));
}

TEST(format_test, wide_format_to_n_runtime) {
  wchar_t buffer[4];
  buffer[3] = L'x';
  auto result = fmt::format_to_n(buffer, 3, fmt::runtime(L"{}"), 12345);
  EXPECT_EQ(5u, result.size);
  EXPECT_EQ(buffer + 3, result.out);
  EXPECT_EQ(L"123x", fmt::wstring_view(buffer, 4));
  buffer[0] = L'x';
  buffer[1] = L'x';
  buffer[2] = L'x';
  result = fmt::format_to_n(buffer, 3, fmt::runtime(L"{}"), L'A');
  EXPECT_EQ(1u, result.size);
  EXPECT_EQ(buffer + 1, result.out);
  EXPECT_EQ(L"Axxx", fmt::wstring_view(buffer, 4));
  result = fmt::format_to_n(buffer, 3, fmt::runtime(L"{}{} "), L'B', L'C');
  EXPECT_EQ(3u, result.size);
  EXPECT_EQ(buffer + 3, result.out);
  EXPECT_EQ(L"BC x", fmt::wstring_view(buffer, 4));
}

TEST(xchar_test, named_arg_udl) {
  using namespace fmt::literals;
  auto udl_a =
      fmt::format(L"{first}{second}{first}{third}", L"first"_a = L"abra",
                  L"second"_a = L"cad", L"third"_a = 99);
  EXPECT_EQ(
      fmt::format(L"{first}{second}{first}{third}", fmt::arg(L"first", L"abra"),
                  fmt::arg(L"second", L"cad"), fmt::arg(L"third", 99)),
      udl_a);
}

TEST(xchar_test, print) {
  // Check that the wide print overload compiles.
  if (false) {
    fmt::print(L"test");
    fmt::println(L"test");
  }
}

TEST(xchar_test, join) {
  int v[3] = {1, 2, 3};
  EXPECT_EQ(fmt::format(u"({})", fmt::join(v, v + 3, u", ")), u"(1, 2, 3)");
  EXPECT_EQ(fmt::format(U"({})", fmt::join(v, v + 3, U", ")), U"(1, 2, 3)");
  EXPECT_EQ(fmt::format(L"({})", fmt::join(v, v + 3, L", ")), L"(1, 2, 3)");
  auto vector = std::vector<int>{1, 2, 3};
  EXPECT_EQ(fmt::format(u"({})", fmt::join(vector, u", ")), u"(1, 2, 3)");
  EXPECT_EQ(fmt::format(U"({})", fmt::join(vector, U", ")), U"(1, 2, 3)");
  EXPECT_EQ(fmt::format(L"({})", fmt::join(vector, L", ")), L"(1, 2, 3)");
  auto tuple_char16 = std::tuple<char16_t, int, float>(u'a', 1, 2.0f);
  EXPECT_EQ(fmt::format(u"({})", fmt::join(tuple_char16, u", ")), u"(a, 1, 2)");
  auto tuple_char32 = std::tuple<char32_t, int, float>(U'a', 1, 2.0f);
  EXPECT_EQ(fmt::format(U"({})", fmt::join(tuple_char32, U", ")), U"(a, 1, 2)");
  auto tuple_wchar = std::tuple<wchar_t, int, float>(L'a', 1, 2.0f);
  EXPECT_EQ(fmt::format(L"({})", fmt::join(tuple_wchar, L", ")), L"(a, 1, 2)");
}

#ifdef __cpp_lib_byte
TEST(xchar_test, join_bytes) {
  auto v = std::vector<std::byte>{std::byte(1), std::byte(2), std::byte(3)};
  EXPECT_EQ(fmt::format(L"{}", fmt::join(v, L", ")), L"1, 2, 3");
}
#endif

enum streamable_enum {};

std::wostream& operator<<(std::wostream& os, streamable_enum) {
  return os << L"streamable_enum";
}

namespace fmt {
template <>
struct formatter<streamable_enum, wchar_t> : basic_ostream_formatter<wchar_t> {
};
}  // namespace fmt

enum unstreamable_enum {};
auto format_as(unstreamable_enum e) -> int { return e; }

TEST(xchar_test, enum) {
  EXPECT_EQ(L"streamable_enum", fmt::format(L"{}", streamable_enum()));
  EXPECT_EQ(L"0", fmt::format(L"{}", unstreamable_enum()));
}

struct streamable_and_unformattable {};

auto operator<<(std::wostream& os, streamable_and_unformattable)
    -> std::wostream& {
  return os << L"foo";
}

TEST(xchar_test, streamed) {
  EXPECT_FALSE(fmt::is_formattable<streamable_and_unformattable>());
  EXPECT_EQ(fmt::format(L"{}", fmt::streamed(streamable_and_unformattable())),
            L"foo");
}

TEST(xchar_test, sign_not_truncated) {
  wchar_t format_str[] = {
      L'{', L':',
      '+' | static_cast<wchar_t>(1 << fmt::detail::num_bits<char>()), L'}', 0};
  EXPECT_THROW(fmt::format(fmt::runtime(format_str), 42), fmt::format_error);
}

TEST(xchar_test, chrono) {
  auto tm = std::tm();
  tm.tm_year = 116;
  tm.tm_mon = 3;
  tm.tm_mday = 25;
  tm.tm_hour = 11;
  tm.tm_min = 22;
  tm.tm_sec = 33;
  EXPECT_EQ(fmt::format("The date is {:%Y-%m-%d %H:%M:%S}.", tm),
            "The date is 2016-04-25 11:22:33.");
  EXPECT_EQ(L"42s", fmt::format(L"{}", std::chrono::seconds(42)));
  EXPECT_EQ(fmt::format(L"{:%F}", tm), L"2016-04-25");
  EXPECT_EQ(fmt::format(L"{:%T}", tm), L"11:22:33");

  auto t = fmt::sys_time<std::chrono::seconds>(std::chrono::seconds(290088000));
  EXPECT_EQ(fmt::format("{:%Y-%m-%d %H:%M:%S}", t), "1979-03-12 12:00:00");
}

TEST(xchar_test, color) {
  EXPECT_EQ(fmt::format(fg(fmt::rgb(255, 20, 30)), L"rgb(255,20,30) wide"),
            L"\x1b[38;2;255;020;030mrgb(255,20,30) wide\x1b[0m");
}

TEST(xchar_test, ostream) {
#if !FMT_GCC_VERSION || FMT_GCC_VERSION >= 409
  {
    std::wostringstream wos;
    fmt::print(wos, L"Don't {}!", L"panic");
    EXPECT_EQ(wos.str(), L"Don't panic!");
  }

  {
    std::wostringstream wos;
    fmt::println(wos, L"Don't {}!", L"panic");
    EXPECT_EQ(wos.str(), L"Don't panic!\n");
  }
#endif
}

TEST(xchar_test, format_map) {
  auto m = std::map<std::wstring, int>{{L"one", 1}, {L"t\"wo", 2}};
  EXPECT_EQ(fmt::format(L"{}", m), L"{\"one\": 1, \"t\\\"wo\": 2}");
}

TEST(xchar_test, escape_string) {
  using vec = std::vector<std::wstring>;
  EXPECT_EQ(fmt::format(L"{}", vec{L"\n\r\t\"\\"}), L"[\"\\n\\r\\t\\\"\\\\\"]");
  EXPECT_EQ(fmt::format(L"{}", vec{L"понедельник"}), L"[\"понедельник\"]");
}

TEST(xchar_test, to_wstring) { EXPECT_EQ(L"42", fmt::to_wstring(42)); }

#ifndef FMT_STATIC_THOUSANDS_SEPARATOR

template <typename Char> struct numpunct : std::numpunct<Char> {
 protected:
  Char do_decimal_point() const override { return '?'; }
  std::string do_grouping() const override { return "\03"; }
  Char do_thousands_sep() const override { return '~'; }
};

template <typename Char> struct no_grouping : std::numpunct<Char> {
 protected:
  Char do_decimal_point() const override { return '.'; }
  std::string do_grouping() const override { return ""; }
  Char do_thousands_sep() const override { return ','; }
};

template <typename Char> struct special_grouping : std::numpunct<Char> {
 protected:
  Char do_decimal_point() const override { return '.'; }
  std::string do_grouping() const override { return "\03\02"; }
  Char do_thousands_sep() const override { return ','; }
};

template <typename Char> struct small_grouping : std::numpunct<Char> {
 protected:
  Char do_decimal_point() const override { return '.'; }
  std::string do_grouping() const override { return "\01"; }
  Char do_thousands_sep() const override { return ','; }
};

TEST(locale_test, localized_double) {
  auto loc = std::locale(std::locale(), new numpunct<char>());
  EXPECT_EQ(fmt::format(loc, "{:L}", 1.23), "1?23");
  EXPECT_EQ(fmt::format(loc, "{:Lf}", 1.23), "1?230000");
  EXPECT_EQ(fmt::format(loc, "{:L}", 1234.5), "1~234?5");
  EXPECT_EQ(fmt::format(loc, "{:L}", 12000.0), "12~000");
  EXPECT_EQ(fmt::format(loc, "{:8L}", 1230.0), "   1~230");
  EXPECT_EQ(fmt::format(loc, "{:15.6Lf}", 0.1), "       0?100000");
  EXPECT_EQ(fmt::format(loc, "{:15.6Lf}", 1.0), "       1?000000");
  EXPECT_EQ(fmt::format(loc, "{:15.6Lf}", 1e3), "   1~000?000000");
}

TEST(locale_test, format) {
  auto loc = std::locale(std::locale(), new numpunct<char>());
  EXPECT_EQ("1234567", fmt::format(std::locale(), "{:L}", 1234567));
  EXPECT_EQ("1~234~567", fmt::format(loc, "{:L}", 1234567));
  EXPECT_EQ("-1~234~567", fmt::format(loc, "{:L}", -1234567));
  EXPECT_EQ("-256", fmt::format(loc, "{:L}", -256));
  auto n = 1234567;
  EXPECT_EQ("1~234~567", fmt::vformat(loc, "{:L}", fmt::make_format_args(n)));
  auto s = std::string();
  fmt::format_to(std::back_inserter(s), loc, "{:L}", 1234567);
  EXPECT_EQ("1~234~567", s);

  auto no_grouping_loc = std::locale(std::locale(), new no_grouping<char>());
  EXPECT_EQ("1234567", fmt::format(no_grouping_loc, "{:L}", 1234567));

  auto special_grouping_loc =
      std::locale(std::locale(), new special_grouping<char>());
  EXPECT_EQ("1,23,45,678", fmt::format(special_grouping_loc, "{:L}", 12345678));
  EXPECT_EQ("12,345", fmt::format(special_grouping_loc, "{:L}", 12345));

  auto small_grouping_loc =
      std::locale(std::locale(), new small_grouping<char>());
  EXPECT_EQ("4,2,9,4,9,6,7,2,9,5",
            fmt::format(small_grouping_loc, "{:L}", max_value<uint32_t>()));
}

TEST(locale_test, format_default_align) {
  auto loc = std::locale({}, new special_grouping<char>());
  EXPECT_EQ("  12,345", fmt::format(loc, "{:8L}", 12345));
}

TEST(locale_test, format_plus) {
  auto loc = std::locale({}, new special_grouping<char>());
  EXPECT_EQ("+100", fmt::format(loc, "{:+L}", 100));
}

TEST(locale_test, wformat) {
  auto loc = std::locale(std::locale(), new numpunct<wchar_t>());
  EXPECT_EQ(L"1234567", fmt::format(std::locale(), L"{:L}", 1234567));
  EXPECT_EQ(L"1~234~567", fmt::format(loc, L"{:L}", 1234567));
  int n = 1234567;
  EXPECT_EQ(L"1~234~567",
            fmt::vformat(loc, L"{:L}", fmt::make_wformat_args(n)));
  EXPECT_EQ(L"1234567", fmt::format(std::locale("C"), L"{:L}", 1234567));

  auto no_grouping_loc = std::locale(std::locale(), new no_grouping<wchar_t>());
  EXPECT_EQ(L"1234567", fmt::format(no_grouping_loc, L"{:L}", 1234567));

  auto special_grouping_loc =
      std::locale(std::locale(), new special_grouping<wchar_t>());
  EXPECT_EQ(L"1,23,45,678",
            fmt::format(special_grouping_loc, L"{:L}", 12345678));

  auto small_grouping_loc =
      std::locale(std::locale(), new small_grouping<wchar_t>());
  EXPECT_EQ(L"4,2,9,4,9,6,7,2,9,5",
            fmt::format(small_grouping_loc, L"{:L}", max_value<uint32_t>()));
}

TEST(locale_test, int_formatter) {
  auto loc = std::locale(std::locale(), new special_grouping<char>());
  auto f = fmt::formatter<int>();
  auto parse_ctx = fmt::format_parse_context("L");
  f.parse(parse_ctx);
  auto buf = fmt::memory_buffer();
  fmt::basic_format_context<fmt::appender, char> format_ctx(
      fmt::appender(buf), {}, fmt::locale_ref(loc));
  f.format(12345, format_ctx);
  EXPECT_EQ(fmt::to_string(buf), "12,345");
}

TEST(locale_test, chrono_weekday) {
  auto loc = get_locale("es_ES.UTF-8", "Spanish_Spain.1252");
  auto loc_old = std::locale::global(loc);
  auto sat = fmt::weekday(6);
  EXPECT_EQ(fmt::format(L"{}", sat), L"Sat");
  if (loc != std::locale::classic()) {
    // L'\341' is 'á'.
    auto saturdays =
        std::vector<std::wstring>{L"s\341b", L"s\341.", L"s\341b."};
    EXPECT_THAT(saturdays, Contains(fmt::format(loc, L"{:L}", sat)));
  }
  std::locale::global(loc_old);
}

TEST(locale_test, sign) {
  EXPECT_EQ(fmt::format(std::locale(), L"{:L}", -50), L"-50");
}

TEST(std_test_xchar, format_bitset) {
  auto bs = std::bitset<6>(42);
  EXPECT_EQ(fmt::format(L"{}", bs), L"101010");
  EXPECT_EQ(fmt::format(L"{:0>8}", bs), L"00101010");
  EXPECT_EQ(fmt::format(L"{:-^12}", bs), L"---101010---");

  // '#' groups bits in fours (from the right).
  EXPECT_EQ(fmt::format(L"{:#}", bs), L"10 1010");
  EXPECT_EQ(fmt::format(L"{:#}", std::bitset<8>(0b10101100)), L"1010 1100");
  EXPECT_EQ(fmt::format(L"{:#}", std::bitset<4>(0b1010)), L"1010");
  EXPECT_EQ(fmt::format(L"{:#}", std::bitset<16>(0b1010110011110000)),
            L"1010 1100 1111 0000");
  // Grouping + fill / align / width ("1010 1100" is 9 wide chars).
  EXPECT_EQ(fmt::format(L"{:*>12#}", std::bitset<8>(0b10101100)),
            L"***1010 1100");
  EXPECT_EQ(fmt::format(L"{:#12}", std::bitset<8>(0b10101100)),
            L"1010 1100   ");
  EXPECT_EQ(fmt::format(L"{:>#12}", std::bitset<8>(0b10101100)),
            L"   1010 1100");
  // Dynamic width with grouping.
  EXPECT_EQ(fmt::format(L"{:*>{}#}", std::bitset<8>(0b10101100), 12),
            L"***1010 1100");
}

TEST(std_test_xchar, complex) {
  using limits = std::numeric_limits<double>;
  EXPECT_EQ(fmt::format(L"{}", std::complex<double>(1, 2)), L"(1+2i)");
  EXPECT_EQ(fmt::format(L"{:.2f}", std::complex<double>(1, 2)),
            L"(1.00+2.00i)");
  EXPECT_EQ(fmt::format(L"{:8}", std::complex<double>(1, 2)), L"(1+2i)  ");
  EXPECT_EQ(fmt::format(L"{:-<8}", std::complex<double>(1, 2)), L"(1+2i)--");

  // Pure imag (real == 0): no parentheses.
  EXPECT_EQ(fmt::format(L"{}", std::complex<double>(0, 2.2)), L"2.2i");
  EXPECT_EQ(fmt::format(L"{}", std::complex<double>(0, -2.2)), L"-2.2i");
  EXPECT_EQ(fmt::format(L"{:+}", std::complex<double>(0, 2.2)), L"+2.2i");
  EXPECT_EQ(fmt::format(L"{:+}", std::complex<double>(0, -2.2)), L"-2.2i");

  // Full form with signs / space.
  EXPECT_EQ(fmt::format(L"{}", std::complex<double>(1, -2.2)), L"(1-2.2i)");
  EXPECT_EQ(fmt::format(L"{:+}", std::complex<double>(1, 2.2)), L"(+1+2.2i)");
  EXPECT_EQ(fmt::format(L"{:+}", std::complex<double>(1, -2.2)), L"(+1-2.2i)");
  EXPECT_EQ(fmt::format(L"{: }", std::complex<double>(1, 2.2)), L"( 1+2.2i)");
  EXPECT_EQ(fmt::format(L"{: }", std::complex<double>(1, -2.2)), L"( 1-2.2i)");

  // Outer width pads the whole complex string (not just components).
  EXPECT_EQ(fmt::format(L"{:>20.2f}", std::complex<double>(1, 2.2)),
            L"        (1.00+2.20i)");
  EXPECT_EQ(fmt::format(L"{:<20.2f}", std::complex<double>(1, 2.2)),
            L"(1.00+2.20i)        ");
  EXPECT_EQ(fmt::format(L"{:<20.2f}", std::complex<double>(1, -2.2)),
            L"(1.00-2.20i)        ");
  EXPECT_EQ(fmt::format(L"{:<{}.{}f}", std::complex<double>(1, -2.2), 20, 2),
            L"(1.00-2.20i)        ");
  // "2i" is 2 chars → 6 fill chars to width 8.
  EXPECT_EQ(fmt::format(L"{:*>8}", std::complex<double>(0, 2)), L"******2i");

  // Non-finite imag.
  EXPECT_EQ(fmt::format(L"{}", std::complex<double>(1, limits::quiet_NaN())),
            L"(1+nan i)");
  EXPECT_EQ(fmt::format(L"{}", std::complex<double>(1, -limits::infinity())),
            L"(1-inf i)");
  EXPECT_EQ(fmt::format(L"{}", std::complex<int>(1, 2)), L"(1+2i)");
}

TEST(std_test_xchar, optional) {
#  ifdef __cpp_lib_optional
  // Empty: bare "none", never quoted (even with '?').
  EXPECT_EQ(fmt::format(L"{}", std::optional<int>{}), L"none");
  EXPECT_EQ(fmt::format(L"{:?}", std::optional<int>{}), L"none");
  EXPECT_EQ(fmt::format(L"{:x}", std::optional<int>{}), L"none");
  EXPECT_EQ(fmt::format(L"{:n}", std::optional<int>{}), L"none");
  EXPECT_EQ(fmt::format(L"{}", std::optional<std::wstring>{}), L"none");

  // Specs forward to the contained value; wrapper stays.
  EXPECT_EQ(fmt::format(L"{}", std::optional{42}), L"optional(42)");
  EXPECT_EQ(fmt::format(L"{:x}", std::optional{42}), L"optional(2a)");
  EXPECT_EQ(fmt::format(L"{:X}", std::optional{42}), L"optional(2A)");
  EXPECT_EQ(fmt::format(L"{:#x}", std::optional{42}), L"optional(0x2a)");
  EXPECT_EQ(fmt::format(L"{:05d}", std::optional{42}), L"optional(00042)");
  EXPECT_EQ(fmt::format(L"{:*>8}", std::optional{42}), L"optional(******42)");
  EXPECT_EQ(fmt::format(L"{:>10x}", std::optional{42}),
            L"optional(        2a)");
  EXPECT_EQ(fmt::format(L"{:?x}", std::optional{42}), L"optional(2a)");
  EXPECT_EQ(fmt::format(L"{:?}", std::optional{42}), L"optional(42)");
  EXPECT_EQ(fmt::format(L"{:.{}f}", std::optional{3.14}, 1), L"optional(3.1)");

  // '{:n}' drops the optional(...) wrapper.
  EXPECT_EQ(fmt::format(L"{:n}", std::optional{42}), L"42");
  EXPECT_EQ(fmt::format(L"{:nx}", std::optional{42}), L"2a");
  EXPECT_EQ(fmt::format(L"{:n#x}", std::optional{42}), L"0x2a");
  EXPECT_EQ(fmt::format(L"{:n*>8}", std::optional{42}), L"******42");
  EXPECT_EQ(fmt::format(L"{:?n}", std::optional{42}), L"42");
  EXPECT_EQ(fmt::format(L"{:n?}", std::optional{42}), L"42");

  // Wide char / wstring always debug-quoted inside optional.
  EXPECT_EQ(fmt::format(L"{}", std::optional{L'C'}), L"optional(\'C\')");
  EXPECT_EQ(fmt::format(L"{:n}", std::optional{L'C'}), L"\'C\'");
  EXPECT_EQ(fmt::format(L"{}", std::optional{std::wstring{L"wide string"}}),
            L"optional(\"wide string\")");
  EXPECT_EQ(fmt::format(L"{:?}", std::optional{std::wstring{L"wide string"}}),
            L"optional(\"wide string\")");
  EXPECT_EQ(fmt::format(L"{:s}", std::optional{std::wstring{L"hi"}}),
            L"optional(\"hi\")");
  EXPECT_EQ(fmt::format(L"{:n}", std::optional{std::wstring{L"hi"}}), L"\"hi\"");
  // Escapes in wide strings.
  EXPECT_EQ(fmt::format(L"{}", std::optional{std::wstring{L"a\"b\n"}}),
            L"optional(\"a\\\"b\\n\")");
  EXPECT_EQ(fmt::format(L"{:n}", std::optional{std::wstring{L"a\"b\n"}}),
            L"\"a\\\"b\\n\"");

  // Nested optional / range with 'n'.
  EXPECT_EQ(
      fmt::format(L"{}", std::optional<std::optional<int>>{{42}}),
      L"optional(optional(42))");
  EXPECT_EQ(
      fmt::format(L"{:n}", std::optional<std::optional<int>>{{42}}),
      L"optional(42)");
  EXPECT_EQ(fmt::format(L"{:nn}", std::optional{std::vector{1, 2, 3}}),
            L"1, 2, 3");
  EXPECT_EQ(fmt::format(L"{:n}", std::optional{std::vector{1, 2, 3}}),
            L"[1, 2, 3]");
  EXPECT_EQ(
      fmt::format(L"{}", std::vector{std::optional{1}, std::optional{2},
                                     std::optional{3}}),
      L"[optional(1), optional(2), optional(3)]");
  EXPECT_EQ(
      fmt::format(L"{:<{}}", std::optional{std::wstring{L"left aligned"}}, 30),
      L"optional(\"left aligned\"                )");
  EXPECT_EQ(
      fmt::format(L"{::d}",
                  std::optional{std::vector{L'h', L'e', L'l', L'l', L'o'}}),
      L"optional([104, 101, 108, 108, 111])");

  EXPECT_TRUE((fmt::is_formattable<std::optional<int>, wchar_t>::value));
  EXPECT_TRUE(
      (fmt::is_formattable<std::optional<std::wstring>, wchar_t>::value));
#  endif
}

#  ifdef __cpp_lib_variant
namespace {
struct throws_on_move_xchar {
  throws_on_move_xchar() = default;
  [[noreturn]] throws_on_move_xchar(throws_on_move_xchar&&) {
    throw std::runtime_error("Thrown by throws_on_move_xchar");
  }
  throws_on_move_xchar(const throws_on_move_xchar&) = default;
};
}  // namespace

namespace fmt {
template <typename Char>
struct formatter<throws_on_move_xchar, Char>
    : formatter<basic_string_view<Char>, Char> {
  template <typename FormatContext>
  auto format(const throws_on_move_xchar&, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    basic_string_view<Char> str(
        detail::string_literal<Char, '<', 't', 'h', 'r', 'o', 'w', 's', '>'>{});
    return formatter<basic_string_view<Char>, Char>::format(str, ctx);
  }
};
}  // namespace fmt

TEST(std_test_xchar, variant) {
  EXPECT_EQ(fmt::format(L"{}", std::monostate{}), L"monostate");

  using V0 = std::variant<int, float, std::wstring, wchar_t>;
  V0 v0(42);
  V0 v1(1.5f);
  V0 v2(std::wstring(L"hello"));
  V0 v3(L'i');
  EXPECT_EQ(fmt::format(L"{}", v0), L"variant(42)");
  EXPECT_EQ(fmt::format(L"{}", v1), L"variant(1.5)");
  EXPECT_EQ(fmt::format(L"{}", v2), L"variant(\"hello\")");
  EXPECT_EQ(fmt::format(L"{}", v3), L"variant('i')");

  // '{:n}' drops the variant(...) wrapper.
  EXPECT_EQ(fmt::format(L"{:n}", v0), L"42");
  EXPECT_EQ(fmt::format(L"{:n}", v1), L"1.5");
  EXPECT_EQ(fmt::format(L"{:n}", v2), L"\"hello\"");
  EXPECT_EQ(fmt::format(L"{:n}", v3), L"'i'");

  // Width / align pad the full content (wrapped or with 'n').
  EXPECT_EQ(fmt::format(L"{:>15}", v0), L"    variant(42)");
  EXPECT_EQ(fmt::format(L"{:15}", v0), L"variant(42)    ");
  EXPECT_EQ(fmt::format(L"{:*>15}", v0), L"****variant(42)");
  EXPECT_EQ(fmt::format(L"{:*>{}}", v0, 15), L"****variant(42)");
  EXPECT_EQ(fmt::format(L"{:<20}", v2), L"variant(\"hello\")    ");
  EXPECT_EQ(fmt::format(L"{:n*>8}", v0), L"******42");
  EXPECT_EQ(fmt::format(L"{:*>8n}", v0), L"******42");
  EXPECT_EQ(fmt::format(L"{:n>6}", v0), L"    42");

  using V1 = std::variant<std::monostate, std::wstring, std::wstring>;
  V1 v4{};
  V1 v5{std::in_place_index<1>, L"yes, this is variant"};
  EXPECT_EQ(fmt::format(L"{}", v4), L"variant(monostate)");
  EXPECT_EQ(fmt::format(L"{:n}", v4), L"monostate");
  // "variant(monostate)" is 18 chars; with 'n', bare "monostate" is 9.
  EXPECT_EQ(fmt::format(L"{:*>20}", v4), L"**variant(monostate)");
  EXPECT_EQ(fmt::format(L"{:n*>12}", v4), L"***monostate");
  EXPECT_EQ(fmt::format(L"{}", v5), L"variant(\"yes, this is variant\")");
  EXPECT_EQ(fmt::format(L"{:n}", v5), L"\"yes, this is variant\"");

  // Escapes in wide alternatives.
  V0 v_esc(std::wstring(L"a\"b\n"));
  EXPECT_EQ(fmt::format(L"{}", v_esc), L"variant(\"a\\\"b\\n\")");
  EXPECT_EQ(fmt::format(L"{:n}", v_esc), L"\"a\\\"b\\n\"");

  std::variant<std::monostate, throws_on_move_xchar> v6;
  try {
    throws_on_move_xchar thrower;
    v6.emplace<throws_on_move_xchar>(std::move(thrower));
  } catch (const std::runtime_error&) {
  }
  EXPECT_EQ(fmt::format(L"{}", v6), L"variant(valueless by exception)");
  EXPECT_EQ(fmt::format(L"{:n}", v6), L"valueless by exception");
  EXPECT_EQ(fmt::format(L"{:*>35}", v6),
            L"****variant(valueless by exception)");
  EXPECT_EQ(fmt::format(L"{:n*>25}", v6), L"***valueless by exception");

  EXPECT_TRUE((fmt::is_formattable<std::variant<int, float>, wchar_t>::value));
  EXPECT_TRUE((fmt::is_formattable<std::monostate, wchar_t>::value));
  EXPECT_TRUE((fmt::is_formattable<
               std::variant<std::monostate, std::wstring>, wchar_t>::value));
  struct unformattable {};
  EXPECT_FALSE(
      (fmt::is_formattable<std::variant<unformattable>, wchar_t>::value));
}
#  endif  // __cpp_lib_variant

#  ifdef __cpp_lib_expected
TEST(std_test_xchar, expected) {
  EXPECT_EQ(fmt::format(L"{}", std::expected<void, int>{}), L"expected()");
  EXPECT_EQ(fmt::format(L"{}", std::expected<int, int>{1}), L"expected(1)");
  EXPECT_EQ(fmt::format(L"{}", std::expected<int, int>{std::unexpected(1)}),
            L"unexpected(1)");
  EXPECT_EQ(fmt::format(L"{}", std::expected<std::wstring, int>{L"test"}),
            L"expected(\"test\")");
  EXPECT_EQ(fmt::format(L"{}", std::expected<int, std::wstring>{
                                   std::unexpected(std::wstring(L"test"))}),
            L"unexpected(\"test\")");
  EXPECT_EQ(fmt::format(L"{}", std::expected<wchar_t, int>{L'a'}),
            L"expected('a')");
  EXPECT_EQ(fmt::format(L"{}", std::expected<int, wchar_t>{
                                   std::unexpected(L'a')}),
            L"unexpected('a')");
  EXPECT_EQ(
      fmt::format(L"{}", std::unexpected<std::wstring>{std::wstring(L"err")}),
      L"unexpected(\"err\")");
  EXPECT_TRUE(
      (fmt::is_formattable<std::expected<int, int>, wchar_t>::value));
}
#  endif  // __cpp_lib_expected

#  ifdef __cpp_lib_filesystem
TEST(std_test_xchar, path) {
  using std::filesystem::path;
  EXPECT_EQ(fmt::format(L"{}", path("foo/bar")), L"foo/bar");
  EXPECT_EQ(fmt::format(L"{:?}", path("foo/bar")), L"\"foo/bar\"");
  EXPECT_EQ(fmt::format(L"{:8}", path("foo")), L"foo     ");
  EXPECT_EQ(fmt::format(L"{}", path("foo\"bar")), L"foo\"bar");
  EXPECT_EQ(fmt::format(L"{:?}", path("foo\"bar")), L"\"foo\\\"bar\"");
  EXPECT_EQ(fmt::format(L"{:?}", path("foo\tbar")), L"\"foo\\tbar\"");
  EXPECT_EQ(fmt::format(L"{:?}", path("foo\nbar")), L"\"foo\\nbar\"");

  EXPECT_EQ(fmt::format(L"{:g}", path("foo/bar")), L"foo/bar");
  EXPECT_EQ(fmt::format(L"{:?g}", path("foo/bar")), L"\"foo/bar\"");
  EXPECT_EQ(fmt::format(L"{:g?}", path("foo/bar")), L"\"foo/bar\"");
  EXPECT_EQ(fmt::format(L"{:*>12g}", path("foo")), L"*********foo");
  EXPECT_EQ(fmt::format(L"{:*>12?g}", path("foo")), L"*******\"foo\"");
  EXPECT_EQ(fmt::format(L"{:*>12g?}", path("foo")), L"*******\"foo\"");
  // Dynamic width.
  EXPECT_EQ(fmt::format(L"{:*>{}g}", path("foo"), 12), L"*********foo");

#    ifdef _WIN32
  EXPECT_EQ(fmt::format(L"{}", path(L"C:\\foo")), L"C:\\foo");
  EXPECT_EQ(fmt::format(L"{:g}", path(L"C:\\foo")), L"C:/foo");
  EXPECT_EQ(fmt::format(L"{:?}", path(L"C:\\foo")), L"\"C:\\foo\"");
  EXPECT_EQ(fmt::format(L"{:?g}", path(L"C:\\foo")), L"\"C:/foo\"");
  EXPECT_EQ(fmt::format(L"{:*>12g}", path(L"C:\\foo")), L"******C:/foo");
#    endif

  // Non-ASCII path via wide native string.
  EXPECT_EQ(fmt::format(L"{}", path(L"понедельник")), L"понедельник");
  EXPECT_EQ(fmt::format(L"{:?}", path(L"понедельник")), L"\"понедельник\"");

  EXPECT_TRUE((fmt::is_formattable<path, wchar_t>::value));
}
#  endif  // __cpp_lib_filesystem

TEST(std_test_xchar, byte_and_bit_reference) {
#  ifdef __cpp_lib_byte
  EXPECT_EQ(fmt::format(L"{}", std::byte{42}), L"42");
  EXPECT_EQ(fmt::format(L"{:x}", std::byte{42}), L"2a");
#  endif
  std::bitset<2> bs(1);
  EXPECT_EQ(fmt::format(L"{} {}", bs[0], bs[1]), L"true false");
  std::vector<bool> v = {true, false};
  EXPECT_EQ(fmt::format(L"{} {}", v[0], v[1]), L"true false");
}

// error_code / exception / type_info formatters are char-only; document that
// they are not formattable with wchar_t (no silent fallback).
TEST(std_test_xchar, char_only_std_formatters) {
  EXPECT_FALSE((fmt::is_formattable<std::error_code, wchar_t>::value));
  EXPECT_FALSE((fmt::is_formattable<std::exception, wchar_t>::value));
#  if FMT_USE_RTTI
  EXPECT_FALSE((fmt::is_formattable<std::type_info, wchar_t>::value));
#  endif
}

#endif  // FMT_STATIC_THOUSANDS_SEPARATOR
