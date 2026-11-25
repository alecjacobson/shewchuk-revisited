// Minimal signed big integer for the exact sign oracle.
//
// Sign-magnitude, base 2^32, schoolbook multiplication. Only what the oracle
// needs: construction from int64, +, -, *, left shift and sign. It is slow by
// design: clarity matters more than speed here, because every other result in
// the project is judged against it. It shares no code with predicates.c.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace shewchuk_test {

class BigInt {
 public:
  BigInt() = default;
  BigInt(std::int64_t value) {  // NOLINT: implicit on purpose
    if (value == 0) return;
    sign_ = value < 0 ? -1 : 1;
    // Negate in unsigned arithmetic so that INT64_MIN is handled.
    std::uint64_t magnitude =
        value < 0 ? ~static_cast<std::uint64_t>(value) + 1 : static_cast<std::uint64_t>(value);
    limbs_.push_back(static_cast<std::uint32_t>(magnitude));
    if (magnitude >> 32) limbs_.push_back(static_cast<std::uint32_t>(magnitude >> 32));
  }

  // -1, 0 or +1.
  int sign() const { return sign_; }
  // Magnitude, least significant limb first, without leading zero limbs.
  const std::vector<std::uint32_t>& limbs() const { return limbs_; }

  BigInt operator-() const {
    BigInt result = *this;
    result.sign_ = -sign_;
    return result;
  }

  friend BigInt operator+(const BigInt& a, const BigInt& b) {
    if (a.sign_ == 0) return b;
    if (b.sign_ == 0) return a;
    BigInt result;
    if (a.sign_ == b.sign_) {
      result.limbs_ = add_magnitudes(a.limbs_, b.limbs_);
      result.sign_ = a.sign_;
      return result;
    }
    const int order = compare_magnitudes(a.limbs_, b.limbs_);
    if (order == 0) return result;  // exact cancellation
    if (order > 0) {
      result.limbs_ = subtract_magnitudes(a.limbs_, b.limbs_);
      result.sign_ = a.sign_;
    } else {
      result.limbs_ = subtract_magnitudes(b.limbs_, a.limbs_);
      result.sign_ = b.sign_;
    }
    return result;
  }

  friend BigInt operator-(const BigInt& a, const BigInt& b) { return a + (-b); }

  friend BigInt operator*(const BigInt& a, const BigInt& b) {
    BigInt result;
    if (a.sign_ == 0 || b.sign_ == 0) return result;
    result.limbs_.assign(a.limbs_.size() + b.limbs_.size(), 0);
    for (std::size_t i = 0; i < a.limbs_.size(); i++) {
      std::uint64_t carry = 0;
      for (std::size_t j = 0; j < b.limbs_.size(); j++) {
        // At most (2^32-1)^2 + 2*(2^32-1) = 2^64 - 1: cannot overflow.
        carry += static_cast<std::uint64_t>(a.limbs_[i]) * b.limbs_[j] + result.limbs_[i + j];
        result.limbs_[i + j] = static_cast<std::uint32_t>(carry);
        carry >>= 32;
      }
      result.limbs_[i + b.limbs_.size()] = static_cast<std::uint32_t>(carry);
    }
    trim(result.limbs_);
    result.sign_ = a.sign_ * b.sign_;
    return result;
  }

  // this * 2^bits
  BigInt shifted_left(unsigned bits) const {
    if (sign_ == 0 || bits == 0) return *this;
    BigInt result;
    result.sign_ = sign_;
    const unsigned whole = bits / 32, part = bits % 32;
    result.limbs_.assign(limbs_.size() + whole + 1, 0);
    for (std::size_t i = 0; i < limbs_.size(); i++) {
      const std::uint64_t shifted = static_cast<std::uint64_t>(limbs_[i]) << part;
      result.limbs_[i + whole] |= static_cast<std::uint32_t>(shifted);
      result.limbs_[i + whole + 1] |= static_cast<std::uint32_t>(shifted >> 32);
    }
    trim(result.limbs_);
    return result;
  }

  friend bool operator==(const BigInt& a, const BigInt& b) {
    return a.sign_ == b.sign_ && a.limbs_ == b.limbs_;
  }

 private:
  using Limbs = std::vector<std::uint32_t>;

  static void trim(Limbs& limbs) {
    while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
  }

  static int compare_magnitudes(const Limbs& a, const Limbs& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i-- > 0;) {
      if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
  }

  static Limbs add_magnitudes(const Limbs& a, const Limbs& b) {
    const Limbs& longer = a.size() >= b.size() ? a : b;
    const Limbs& shorter = a.size() >= b.size() ? b : a;
    Limbs sum(longer.size() + 1);
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < longer.size(); i++) {
      carry += static_cast<std::uint64_t>(longer[i]) + (i < shorter.size() ? shorter[i] : 0);
      sum[i] = static_cast<std::uint32_t>(carry);
      carry >>= 32;
    }
    sum[longer.size()] = static_cast<std::uint32_t>(carry);
    trim(sum);
    return sum;
  }

  // Requires |a| >= |b|.
  static Limbs subtract_magnitudes(const Limbs& a, const Limbs& b) {
    Limbs difference(a.size());
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); i++) {
      const std::uint64_t subtrahend = (i < b.size() ? b[i] : 0) + borrow;
      const std::uint64_t minuend = a[i];
      borrow = minuend < subtrahend ? 1 : 0;
      difference[i] = static_cast<std::uint32_t>(minuend + (borrow << 32) - subtrahend);
    }
    trim(difference);
    return difference;
  }

  Limbs limbs_;
  int sign_ = 0;
};

}  // namespace shewchuk_test
