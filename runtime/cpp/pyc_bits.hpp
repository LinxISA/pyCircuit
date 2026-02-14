#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace pyc::cpp {

template <unsigned Width>
class Bits {
public:
  static_assert(Width > 0, "pyc::cpp::Bits requires Width > 0");

  using word_type = std::uint64_t;
  static constexpr unsigned kWidth = Width;
  static constexpr unsigned kWordBits = 64;
  static constexpr unsigned kWords = (Width + kWordBits - 1) / kWordBits;

  constexpr Bits() = default;

  constexpr explicit Bits(word_type low) {
    words_.fill(0);
    words_[0] = low;
    maskTop();
  }

  explicit Bits(std::initializer_list<word_type> words) {
    words_.fill(0);
    unsigned i = 0;
    for (word_type w : words) {
      if (i >= kWords)
        break;
      words_[i++] = w;
    }
    maskTop();
  }

  constexpr word_type value() const { return words_[0]; }
  constexpr bool toBool() const { return (words_[0] & 1u) != 0; }

  constexpr word_type word(unsigned i) const { return (i < kWords) ? words_[i] : word_type{0}; }
  constexpr void setWord(unsigned i, word_type w) {
    if (i >= kWords)
      return;
    if (i + 1u == kWords) {
      words_[i] = w & topMask();
      return;
    }
    words_[i] = w;
  }

  constexpr bool bit(unsigned i) const {
    if (i >= Width)
      return false;
    unsigned wi = i / kWordBits;
    unsigned bi = i % kWordBits;
    return ((word(wi) >> bi) & 1u) != 0;
  }

  static constexpr word_type mask() {
    // Legacy helper: returns a low-word mask for widths <= 64.
    if constexpr (Width >= 64)
      return ~word_type{0};
    return (word_type{1} << Width) - 1;
  }

  static constexpr Bits ones() {
    Bits out;
    for (unsigned i = 0; i < kWords; i++)
      out.words_[i] = ~word_type{0};
    out.maskTop();
    return out;
  }

  friend constexpr Bits operator+(Bits a, Bits b) {
    Bits out;
    word_type carry = 0;
    for (unsigned i = 0; i < kWords; i++) {
      unsigned __int128 sum = static_cast<unsigned __int128>(a.words_[i]) + static_cast<unsigned __int128>(b.words_[i]) +
                              static_cast<unsigned __int128>(carry);
      out.words_[i] = static_cast<word_type>(sum);
      carry = static_cast<word_type>(sum >> 64);
    }
    out.maskTop();
    return out;
  }

  friend constexpr Bits operator-(Bits a, Bits b) {
    Bits out;
    word_type borrow = 0;
    for (unsigned i = 0; i < kWords; i++) {
      unsigned __int128 ai = static_cast<unsigned __int128>(a.words_[i]);
      unsigned __int128 bi = static_cast<unsigned __int128>(b.words_[i]);
      unsigned __int128 diff = ai - bi - static_cast<unsigned __int128>(borrow);
      out.words_[i] = static_cast<word_type>(diff);
      borrow = (ai < (bi + borrow)) ? 1u : 0u;
    }
    out.maskTop();
    return out;
  }

  friend inline Bits operator*(Bits a, Bits b) {
    Bits out;
    out.words_.fill(0);
    for (unsigned i = 0; i < kWords; i++) {
      unsigned __int128 carry = 0;
      for (unsigned j = 0; j + i < kWords; j++) {
        unsigned idx = i + j;
        unsigned __int128 cur = static_cast<unsigned __int128>(out.words_[idx]);
        unsigned __int128 prod = static_cast<unsigned __int128>(a.words_[i]) * static_cast<unsigned __int128>(b.words_[j]);
        unsigned __int128 sum = cur + prod + carry;
        out.words_[idx] = static_cast<word_type>(sum);
        carry = sum >> 64;
      }
    }
    out.maskTop();
    return out;
  }

  friend constexpr Bits operator&(Bits a, Bits b) {
    Bits out;
    for (unsigned i = 0; i < kWords; i++)
      out.words_[i] = a.words_[i] & b.words_[i];
    out.maskTop();
    return out;
  }

  friend constexpr Bits operator|(Bits a, Bits b) {
    Bits out;
    for (unsigned i = 0; i < kWords; i++)
      out.words_[i] = a.words_[i] | b.words_[i];
    out.maskTop();
    return out;
  }

  friend constexpr Bits operator^(Bits a, Bits b) {
    Bits out;
    for (unsigned i = 0; i < kWords; i++)
      out.words_[i] = a.words_[i] ^ b.words_[i];
    out.maskTop();
    return out;
  }

  friend constexpr Bits operator~(Bits a) {
    Bits out;
    for (unsigned i = 0; i < kWords; i++)
      out.words_[i] = ~a.words_[i];
    out.maskTop();
    return out;
  }

  friend constexpr bool operator==(Bits a, Bits b) {
    for (unsigned i = 0; i < kWords; i++) {
      if (a.words_[i] != b.words_[i])
        return false;
    }
    return true;
  }

  friend constexpr bool operator!=(Bits a, Bits b) { return !(a == b); }

  friend constexpr bool operator<(Bits a, Bits b) {
    for (unsigned i = 0; i < kWords; i++) {
      unsigned idx = (kWords - 1u) - i;
      if (a.words_[idx] < b.words_[idx])
        return true;
      if (a.words_[idx] > b.words_[idx])
        return false;
    }
    return false;
  }

  friend constexpr bool operator>(Bits a, Bits b) { return b < a; }
  friend constexpr bool operator<=(Bits a, Bits b) { return !(b < a); }
  friend constexpr bool operator>=(Bits a, Bits b) { return !(a < b); }

private:
  static constexpr word_type topMask() {
    if constexpr ((Width % kWordBits) == 0)
      return ~word_type{0};
    return (word_type{1} << (Width % kWordBits)) - 1;
  }

  constexpr void maskTop() { words_[kWords - 1] &= topMask(); }

  std::array<word_type, kWords> words_{};
};

template <unsigned Width>
using Wire = Bits<Width>;

template <unsigned Width>
constexpr std::int64_t asSigned(Wire<Width> v) {
  static_assert(Width > 0 && Width <= 64, "asSigned supports widths 1..64");
  if constexpr (Width == 64) {
    return static_cast<std::int64_t>(v.value());
  } else {
    std::uint64_t x = v.value();
    std::uint64_t signBit = std::uint64_t{1} << (Width - 1);
    if (x & signBit)
      x |= (~std::uint64_t{0}) << Width;
    return static_cast<std::int64_t>(x);
  }
}

template <unsigned Width>
constexpr Wire<Width> shl(Wire<Width> v, unsigned amount) {
  if (amount == 0)
    return v;
  if (amount >= Width)
    return Wire<Width>(0);

  Wire<Width> out;
  unsigned wordShift = amount / Wire<Width>::kWordBits;
  unsigned bitShift = amount % Wire<Width>::kWordBits;
  for (unsigned i = 0; i < Wire<Width>::kWords; i++) {
    unsigned dst = (Wire<Width>::kWords - 1u) - i;
    std::uint64_t w = 0;
    if (dst >= wordShift) {
      unsigned src = dst - wordShift;
      w = v.word(src) << bitShift;
      if (bitShift != 0 && src > 0)
        w |= (v.word(src - 1) >> (64u - bitShift));
    }
    out.setWord(dst, w);
  }
  return out;
}

template <unsigned Width>
constexpr Wire<Width> lshr(Wire<Width> v, unsigned amount) {
  if (amount == 0)
    return v;
  if (amount >= Width)
    return Wire<Width>(0);

  Wire<Width> out;
  unsigned wordShift = amount / Wire<Width>::kWordBits;
  unsigned bitShift = amount % Wire<Width>::kWordBits;
  for (unsigned dst = 0; dst < Wire<Width>::kWords; dst++) {
    std::uint64_t w = 0;
    unsigned src = dst + wordShift;
    if (src < Wire<Width>::kWords) {
      w = v.word(src) >> bitShift;
      if (bitShift != 0 && (src + 1) < Wire<Width>::kWords)
        w |= (v.word(src + 1) << (64u - bitShift));
    }
    out.setWord(dst, w);
  }
  return out;
}

template <unsigned Width>
constexpr Wire<Width> ashr(Wire<Width> v, unsigned amount) {
  if (amount == 0)
    return v;
  if (amount >= Width)
    return v.bit(Width - 1) ? Wire<Width>::ones() : Wire<Width>(0);

  Wire<Width> out = lshr<Width>(v, amount);
  if (!v.bit(Width - 1))
    return out;
  // Negative: fill high bits with 1.
  unsigned fillFrom = Width - amount;
  Wire<Width> fill = shl<Width>(Wire<Width>::ones(), fillFrom);
  return out | fill;
}

namespace detail {

template <unsigned Width>
constexpr bool isZero(Wire<Width> v) {
  for (unsigned i = 0; i < Wire<Width>::kWords; i++) {
    if (v.word(i) != 0u)
      return false;
  }
  return true;
}

template <unsigned Width>
constexpr bool isAllOnes(Wire<Width> v) {
  for (unsigned i = 0; i < Width; i++) {
    if (!v.bit(i))
      return false;
  }
  return true;
}

template <unsigned Width>
constexpr bool isMinSigned(Wire<Width> v) {
  if (!v.bit(Width - 1))
    return false;
  for (unsigned i = 0; i + 1 < Width; i++) {
    if (v.bit(i))
      return false;
  }
  return true;
}

template <unsigned Width>
constexpr Wire<Width> negate(Wire<Width> v) {
  return (~v) + Wire<Width>(1);
}

template <unsigned Width>
constexpr Wire<Width> setBit(Wire<Width> v, unsigned idx) {
  if (idx >= Width)
    return v;
  return v | shl<Width>(Wire<Width>(1), idx);
}

template <unsigned Width>
constexpr void udivrem(Wire<Width> numer, Wire<Width> denom, Wire<Width> &quot, Wire<Width> &rem) {
  quot = Wire<Width>(0);
  rem = Wire<Width>(0);
  if (isZero(denom))
    return;

  for (unsigned i = Width; i > 0; --i) {
    unsigned bit = i - 1;
    rem = shl<Width>(rem, 1);
    if (numer.bit(bit))
      rem = rem | Wire<Width>(1);
    if (rem >= denom) {
      rem = rem - denom;
      quot = setBit(quot, bit);
    }
  }
}

} // namespace detail

template <unsigned Width>
constexpr Wire<Width> udiv(Wire<Width> a, Wire<Width> b) {
  if (detail::isZero(b))
    return Wire<Width>(0);
  if constexpr (Width <= 64)
    return Wire<Width>(a.value() / b.value());
  Wire<Width> q;
  Wire<Width> r;
  detail::udivrem<Width>(a, b, q, r);
  return q;
}

template <unsigned Width>
constexpr Wire<Width> urem(Wire<Width> a, Wire<Width> b) {
  if (detail::isZero(b))
    return Wire<Width>(0);
  if constexpr (Width <= 64)
    return Wire<Width>(a.value() % b.value());
  Wire<Width> q;
  Wire<Width> r;
  detail::udivrem<Width>(a, b, q, r);
  return r;
}

template <unsigned Width>
constexpr Wire<Width> sdiv(Wire<Width> a, Wire<Width> b) {
  if (detail::isZero(b))
    return Wire<Width>(0);
  // Two's-complement overflow: MIN_INT / -1 -> MIN_INT.
  if (detail::isMinSigned(a) && detail::isAllOnes(b))
    return a;
  if constexpr (Width <= 64) {
    std::int64_t sa = asSigned<Width>(a);
    std::int64_t sb = asSigned<Width>(b);
    return Wire<Width>(static_cast<std::uint64_t>(sa / sb));
  }

  bool aNeg = a.bit(Width - 1);
  bool bNeg = b.bit(Width - 1);
  Wire<Width> ua = aNeg ? detail::negate(a) : a;
  Wire<Width> ub = bNeg ? detail::negate(b) : b;

  Wire<Width> q;
  Wire<Width> r;
  detail::udivrem<Width>(ua, ub, q, r);
  if (aNeg ^ bNeg)
    q = detail::negate(q);
  return q;
}

template <unsigned Width>
constexpr Wire<Width> srem(Wire<Width> a, Wire<Width> b) {
  if (detail::isZero(b))
    return Wire<Width>(0);
  if (detail::isMinSigned(a) && detail::isAllOnes(b))
    return Wire<Width>(0);
  if constexpr (Width <= 64) {
    std::int64_t sa = asSigned<Width>(a);
    std::int64_t sb = asSigned<Width>(b);
    return Wire<Width>(static_cast<std::uint64_t>(sa % sb));
  }

  bool aNeg = a.bit(Width - 1);
  bool bNeg = b.bit(Width - 1);
  Wire<Width> ua = aNeg ? detail::negate(a) : a;
  Wire<Width> ub = bNeg ? detail::negate(b) : b;

  Wire<Width> q;
  Wire<Width> r;
  detail::udivrem<Width>(ua, ub, q, r);
  if (aNeg)
    r = detail::negate(r);
  return r;
}

namespace detail {

template <unsigned... Ws>
struct SumWidth;

template <>
struct SumWidth<> {
  static constexpr unsigned value = 0;
};

template <unsigned W0, unsigned... Rest>
struct SumWidth<W0, Rest...> {
  static constexpr unsigned value = W0 + SumWidth<Rest...>::value;
};

} // namespace detail

template <unsigned OutWidth, unsigned InWidth>
constexpr Wire<OutWidth> trunc(Wire<InWidth> v) {
  static_assert(OutWidth <= InWidth, "trunc requires OutWidth <= InWidth");
  Wire<OutWidth> out;
  for (unsigned i = 0; i < Wire<OutWidth>::kWords; i++)
    out.setWord(i, v.word(i));
  return out;
}

template <unsigned OutWidth, unsigned InWidth>
constexpr Wire<OutWidth> zext(Wire<InWidth> v) {
  static_assert(OutWidth >= InWidth, "zext requires OutWidth >= InWidth");
  Wire<OutWidth> out;
  for (unsigned i = 0; i < Wire<OutWidth>::kWords; i++)
    out.setWord(i, v.word(i));
  return out;
}

template <unsigned OutWidth, unsigned InWidth>
constexpr Wire<OutWidth> sext(Wire<InWidth> v) {
  static_assert(OutWidth >= InWidth, "sext requires OutWidth >= InWidth");

  Wire<OutWidth> out;
  for (unsigned i = 0; i < Wire<OutWidth>::kWords; i++)
    out.setWord(i, v.word(i));

  if (!v.bit(InWidth - 1))
    return out;

  // Fill above the sign bit with 1s.
  unsigned signWord = (InWidth - 1) / Wire<OutWidth>::kWordBits;
  unsigned signBit = (InWidth - 1) % Wire<OutWidth>::kWordBits;
  std::uint64_t topFill = ~std::uint64_t{0};
  if (signBit < 63)
    topFill = (~std::uint64_t{0}) << (signBit + 1u);
  out.setWord(signWord, out.word(signWord) | topFill);
  for (unsigned i = signWord + 1; i < Wire<OutWidth>::kWords; i++)
    out.setWord(i, ~std::uint64_t{0});
  return out;
}

template <unsigned OutWidth, unsigned InWidth>
constexpr Wire<OutWidth> extract(Wire<InWidth> v, unsigned lsb) {
  return trunc<OutWidth, InWidth>(lshr<InWidth>(v, lsb));
}

template <unsigned A>
constexpr Wire<A> concat(Wire<A> a) {
  return a;
}

template <unsigned A, unsigned B>
constexpr Wire<A + B> concat(Wire<A> a, Wire<B> b) {
  static_assert(A > 0 && B > 0, "concat inputs must be non-zero width");
  Wire<A + B> aa = zext<A + B, A>(a);
  Wire<A + B> bb = zext<A + B, B>(b);
  return shl<A + B>(aa, B) | bb;
}

template <unsigned A, unsigned B, unsigned C, unsigned... Rest>
constexpr Wire<A + B + C + detail::SumWidth<Rest...>::value> concat(Wire<A> a, Wire<B> b, Wire<C> c, Wire<Rest>... rest) {
  return concat(a, concat(b, c, rest...));
}

template <unsigned Width>
constexpr bool slt(Wire<Width> a, Wire<Width> b) {
  // Signed-compare via sign-bit flip (two's complement ordering).
  Wire<Width> sign = shl<Width>(Wire<Width>(1), Width - 1u);
  return (a ^ sign) < (b ^ sign);
}

} // namespace pyc::cpp
