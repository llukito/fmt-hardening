// A fuzzer for the std::complex formatter.
// For the license information refer to format.h.

#include <fmt/format.h>
#include <fmt/std.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "fuzzer-common.h"

template <typename T>
void invoke_inner(fmt::string_view format_str, T real, T imag) {
  try {
    auto value = std::complex<T>(real, imag);
#if FMT_FUZZ_FORMAT_TO_STRING
    std::string message = fmt::format(fmt::runtime(format_str), value);
#else
    auto buf = fmt::memory_buffer();
    fmt::format_to(std::back_inserter(buf), fmt::runtime(format_str), value);
#endif
  } catch (std::exception&) {
  }
}

// Build a floating value that spreads across magnitudes instead of almost
// always landing near zero when fed raw bit patterns.
// Layout: 1 tag byte + 4 payload bytes (compact, independent of sizeof(T)).
//   tag bit0     = sign
//   tag bits1..3 = magnitude class (8 buckets)
//   payload      = mantissa / variation
template <typename T>
T fuzz_float_component(uint8_t tag, uint32_t payload) {
  static_assert(std::numeric_limits<T>::is_iec559, "IEC559 float required");
  const bool neg = (tag & 1) != 0;
  const int mag = (tag >> 1) & 7;
  // [1, 2) from payload; avoid 0 so ldexp classes stay in-range.
  const T frac =
      T(1) + T(payload) / (T(std::numeric_limits<uint32_t>::max()) + T(1));

  T value = T(0);
  switch (mag) {
  case 0:  // exact zero (±0)
    value = T(0);
    break;
  case 1:  // tiny / near-min positive
    value = std::numeric_limits<T>::min() * frac;
    break;
  case 2:  // small: ~1e-12 .. ~1e-3 (clamped to type range)
    value = std::ldexp(frac, -40 + static_cast<int>(payload % 20));
    break;
  case 3:  // moderate: ~1e-2 .. ~1e3
    value = std::ldexp(frac, -6 + static_cast<int>(payload % 16));
    break;
  case 4:  // large: ~1e4 .. ~1e20
    value = std::ldexp(frac, 10 + static_cast<int>(payload % 20));
    break;
  case 5: {  // near the top of the finite range
    const int max_exp = std::numeric_limits<T>::max_exponent - 4;
    value = std::ldexp(frac, max_exp - static_cast<int>(payload % 8));
    break;
  }
  case 6:  // infinities
    value = std::numeric_limits<T>::infinity();
    break;
  case 7:  // NaN
    value = std::numeric_limits<T>::quiet_NaN();
    break;
  }

  if (neg && !std::isnan(value)) value = -value;
  // Preserve a negative NaN sign when requested.
  if (neg && std::isnan(value)) value = std::copysign(value, T(-1));
  return value;
}

// Integers: map tag+payload onto magnitude bands of the type's range.
template <typename T>
T fuzz_int_component(uint8_t tag, uint32_t payload) {
  static_assert(std::is_integral<T>::value, "");
  using U = typename std::make_unsigned<T>::type;
  const bool neg = (tag & 1) != 0 && !std::is_unsigned<T>::value;
  const int mag = (tag >> 1) & 7;
  const U up = static_cast<U>(payload);

  U magnitude = 0;
  switch (mag) {
  case 0:
    magnitude = 0;
    break;
  case 1:
    magnitude = (up % 10) + 1;  // single digits
    break;
  case 2:
    magnitude = (up % 1000) + 10;  // small
    break;
  case 3:
    magnitude = (up % 1000000u) + 1000u;  // medium
    break;
  case 4:
    magnitude = (up & 0xffffffu) + 1000000u;  // larger
    break;
  case 5:
    magnitude = (std::numeric_limits<U>::max() / 4) + (up % 1000);
    break;
  case 6:
  case 7:
    magnitude = std::numeric_limits<U>::max() - (up & 0xff);
    break;
  }

  T value = static_cast<T>(magnitude);
  if (neg) {
    // Avoid UB on the most-negative value for two's complement.
    if (magnitude >
        static_cast<U>(std::numeric_limits<T>::max()))
      value = std::numeric_limits<T>::min();
    else
      value = static_cast<T>(-static_cast<T>(magnitude));
  }
  return value;
}

// Per component: 1 tag + 4 payload = 5 bytes. Real and imag share that layout.
constexpr size_t component_bytes = 5;
constexpr size_t complex_value_bytes = component_bytes * 2;

template <typename T>
void invoke_float_outer(const uint8_t* data, size_t size) {
  if (size <= complex_value_bytes) return;
  const T real =
      fuzz_float_component<T>(data[0], assign_from_buf<uint32_t>(data + 1));
  const T imag = fuzz_float_component<T>(
      data[component_bytes], assign_from_buf<uint32_t>(data + component_bytes + 1));
  data += complex_value_bytes;
  size -= complex_value_bytes;
  invoke_inner(fmt::string_view(as_chars(data), size), real, imag);
}

template <typename T>
void invoke_int_outer(const uint8_t* data, size_t size) {
  if (size <= complex_value_bytes) return;
  const T real =
      fuzz_int_component<T>(data[0], assign_from_buf<uint32_t>(data + 1));
  const T imag = fuzz_int_component<T>(
      data[component_bytes], assign_from_buf<uint32_t>(data + component_bytes + 1));
  data += complex_value_bytes;
  size -= complex_value_bytes;
  invoke_inner(fmt::string_view(as_chars(data), size), real, imag);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size <= 3) return 0;

  const auto type_selector = data[0];
  data++;
  size--;

  switch (type_selector) {
  case 1:
    invoke_float_outer<float>(data, size);
    break;
  case 2:
    invoke_float_outer<double>(data, size);
    break;
  case 3:
    invoke_float_outer<long double>(data, size);
    break;
  case 4:
    invoke_int_outer<int>(data, size);
    break;
  case 5:
    invoke_int_outer<long>(data, size);
    break;
  }
  return 0;
}
