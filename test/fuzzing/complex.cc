// A fuzzer for the std::complex formatter.
// For the license information refer to format.h.

#include <fmt/format.h>
#include <fmt/std.h>

#include <complex>
#include <cstdint>
#include <stdexcept>

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

// dual writes a real and imag component from the start of the buffer, then uses
// the remainder as a format string.
template <typename T>
void invoke_outer(const uint8_t* data, size_t size) {
  constexpr auto value_size = sizeof(T) * 2;
  if (size <= value_size) return;

  const T real = assign_from_buf<T>(data);
  const T imag = assign_from_buf<T>(data + sizeof(T));
  data += value_size;
  size -= value_size;

  invoke_inner(fmt::string_view(as_chars(data), size), real, imag);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size <= 3) return 0;

  const auto type_selector = data[0];
  data++;
  size--;

  switch (type_selector) {
  case 1:
    invoke_outer<float>(data, size);
    break;
  case 2:
    invoke_outer<double>(data, size);
    break;
  case 3:
    invoke_outer<long double>(data, size);
    break;
  case 4:
    invoke_outer<int>(data, size);
    break;
  case 5:
    invoke_outer<long>(data, size);
    break;
  }
  return 0;
}
