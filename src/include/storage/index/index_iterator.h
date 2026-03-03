//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.h
//
// Identification: src/include/storage/index/index_iterator.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <utility>
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

/**
 * BPlusTreeIndex 的 Iterator.
 * 这不是基类.
 */

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
  using InternalPageValueType = page_id_t;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  // you may define your own constructor based on your member variables
  ~IndexIterator();  // NOLINT
  IndexIterator(IndexIterator &&that) noexcept;

  explicit IndexIterator(ReadPageGuard &&guard, int offset, BufferPoolManager *bpm)
      : page_g_opt_{std::move(guard)}, offset_{offset}, bpm_{bpm} {}
  explicit IndexIterator(std::nullopt_t nope, BufferPoolManager *bpm)
      : page_g_opt_{std::nullopt}, offset_{-1}, bpm_{bpm} {}

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool;

  auto operator!=(const IndexIterator &itr) const -> bool;

 private:
  // add your own private member variables here
  std::optional<ReadPageGuard> page_g_opt_;
  int offset_{};
  BufferPoolManager *bpm_;
};

}  // namespace bustub
