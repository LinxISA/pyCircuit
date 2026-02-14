#pragma once

#include <array>
#include <cstddef>

namespace pyc::cpp {

template <typename T, std::size_t N>
struct Vec {
  std::array<T, N> v{};

  constexpr T &operator[](std::size_t i) { return v[i]; }
  constexpr const T &operator[](std::size_t i) const { return v[i]; }

  static constexpr std::size_t size() { return N; }

  constexpr auto begin() { return v.begin(); }
  constexpr auto end() { return v.end(); }
  constexpr auto begin() const { return v.begin(); }
  constexpr auto end() const { return v.end(); }
};

} // namespace pyc::cpp

