#include "primer/hyperloglog.h"

#include <utility>

namespace bustub {

/** @brief Parameterized constructor. */
template <typename KeyType>
HyperLogLog<KeyType>::HyperLogLog(int16_t n_bits) : cardinality_(0) {
  idx_bits_ = static_cast<uint16_t>(std::max(std::min(n_bits, static_cast<int16_t>(63)), static_cast<int16_t>(0)));
  buckets_.resize(1 << idx_bits_);
}

/**
 * @brief Function that computes binary.
 *
 * @param[in] hash
 * @returns binary of a given hash
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeBinary(const hash_t &hash) const -> std::bitset<BITSET_CAPACITY> {
  /** @TODO(student) Implement this function! */
  return {hash};
}

/**
 * @brief Function that computes leading zeros.
 *
 * @param[in] bset - binary values of a given bitset
 * @returns leading zeros of given binary set
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::PositionOfLeftmostOne(const std::bitset<BITSET_CAPACITY> &bset) const -> uint64_t {
  /** @TODO(student) Implement this function! */
  uint64_t res{};
  for (int i = bset.size() - 1; i >= 0; i--) {
    if (static_cast<int>(bset[i]) == 0) {
      res++;
    } else {
      break;
    }
  }
  return res + 1;
}

/**
 * @brief Adds a value into the HyperLogLog.
 *
 * @param[in] val - value that's added into hyperloglog
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */
  hash_t key_hashval{CalculateHash(std::move(val))};

  size_t base_val{key_hashval};
  size_t idx{};
  if (idx_bits_ != 0) {
    base_val &= ((1ULL << (sizeof(size_t) * 8 - idx_bits_)) - 1);
    idx = key_hashval >> (sizeof(size_t) * 8 - idx_bits_);
  }

  // std::cout << "base_val \t" << base_val << std::endl;
  buckets_[idx] = std::max(buckets_[idx], PositionOfLeftmostOne(ComputeBinary(base_val)) - idx_bits_);
}

/**
 * @brief Function that computes cardinality.
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */
  auto m{1 << idx_bits_};
  double sum{};
  for (auto bval : buckets_) {
    sum += std::pow(2, -static_cast<double>(bval));
  }
  cardinality_ = CONSTANT * m * m / sum;
}

template class HyperLogLog<int64_t>;
template class HyperLogLog<std::string>;

}  // namespace bustub
