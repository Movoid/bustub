#include "primer/hyperloglog_presto.h"

#include <utility>

namespace bustub {

constexpr static size_t DENSE_MAXVAL{(1ULL << 4) - 1};

/** @brief Parameterized constructor. */
template <typename KeyType>
HyperLogLogPresto<KeyType>::HyperLogLogPresto(int16_t n_leading_bits) : cardinality_(0) {
  idx_bits_ =
      static_cast<uint16_t>(std::max(std::min(n_leading_bits, static_cast<int16_t>(16)), static_cast<int16_t>(0)));
  dense_bucket_.resize(1 << idx_bits_);
}

/** @brief Element is added for HLL calculation. */
template <typename KeyType>
auto HyperLogLogPresto<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */
  hash_t key_hashval{CalculateHash(std::move(val))};
  size_t base_val{key_hashval};
  size_t idx{};
  if (idx_bits_ != 0) {
    base_val &= ((1ULL << (sizeof(size_t) * 8 - idx_bits_)) - 1);
    idx = key_hashval >> (sizeof(size_t) * 8 - idx_bits_);
  }

  size_t old_low{dense_bucket_[idx].to_ullong()};
  auto overflow_iter{overflow_bucket_.find(idx)};
  size_t old_high{overflow_iter == overflow_bucket_.end() ? 0ULL : overflow_iter->second.to_ullong()};
  size_t old_cntval{(old_high << 4) | old_low};

  std::bitset<64> base_bs{base_val};
  size_t res{};
  for (size_t i = 0; i < (sizeof(size_t) * 8 - idx_bits_); i++) {
    if (static_cast<int>(base_bs[i]) == 0) {
      res++;
    } else {
      break;
    }
  }

  if (res > old_cntval) {
    if (res > DENSE_MAXVAL) {
      overflow_bucket_[idx] = (res >> 4) & ((1ULL << 3) - 1);
      dense_bucket_[idx] = res & ((1ULL << 4) - 1);
    } else {
      dense_bucket_[idx] = res;
    }
  }
}

/** @brief Function to compute cardinality. */
template <typename T>
auto HyperLogLogPresto<T>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */
  auto m{1 << idx_bits_};
  double sum{};
  for (size_t i = 0; i < dense_bucket_.size(); i++) {
    auto overflow_iter{overflow_bucket_.find(i)};
    size_t high{overflow_iter == overflow_bucket_.end() ? 0ULL : overflow_iter->second.to_ullong()};
    size_t low{dense_bucket_[i].to_ullong()};
    size_t val{(high << 4) | low};
    sum += std::pow(2, -static_cast<double>(val));
  }
  cardinality_ = CONSTANT * m * m / sum;
}

template class HyperLogLogPresto<int64_t>;
template class HyperLogLogPresto<std::string>;
}  // namespace bustub
