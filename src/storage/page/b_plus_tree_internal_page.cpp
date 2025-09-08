//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new internal page.
 *
 * Writes the necessary header information to a newly created page,
 * including set page type, set current size, set page id, set parent id and set max page size,
 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  // UNIMPLEMENTED("TODO(P2): Add implementation.");
  BUSTUB_ASSERT(max_size > 0 && static_cast<std::uint64_t>(max_size) <= INTERNAL_PAGE_SLOT_CNT, "Invalid init.");
  SetSize(0);
  SetMaxSize(max_size);
  SetPageType(IndexPageType::INTERNAL_PAGE);
}

/**
 * @brief Helper method to get/set the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // UNIMPLEMENTED("TODO(P2): Add implementation.");
  // BUSTUB_ASSERT(0 < index && index < INTERNAL_PAGE_SLOT_CNT, "Invalid index.");
  BUSTUB_ASSERT(0 <= index && static_cast<std::uint64_t>(index) < INTERNAL_PAGE_SLOT_CNT, "Invalid index.");
  return key_array_[index];
}

/**
 * @brief Set key at the specified index.
 *
 * @param index The index of the key to set. Index must be non-zero.
 * @param key The new value for key
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  // UNIMPLEMENTED("TODO(P2): Add implementation.");
  BUSTUB_ASSERT(0 <= index && static_cast<std::uint64_t>(index) < INTERNAL_PAGE_SLOT_CNT, "Invalid index.");
  key_array_[index] = key;
}

/**
 * @brief Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 *
 * @param index The index of the value to get.
 * @return Value at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  // UNIMPLEMENTED("TODO(P2): Add implementation.");
  BUSTUB_ASSERT(0 <= index && static_cast<std::uint64_t>(index) < INTERNAL_PAGE_SLOT_CNT, "Invalid index.");
  return page_id_array_[index];
}

// INDEX_TEMPLATE_ARGUMENTS
// auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetValidKeyCnt() const -> int {
//   BUSTUB_ASSERT(GetSize() > 0, "Memory corrupted?");
//   return GetSize() - 1;
// }

// INDEX_TEMPLATE_ARGUMENTS
// auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetMaxValidKeyCnt() const -> int { return GetMaxSize() - 1; }

// INDEX_TEMPLATE_ARGUMENTS
// auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetMinValidKeyCnt() const -> int { return GetMinSize() - 1; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::HasHighKey() const -> bool { return high_key_is_valid_; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetHighKey() const -> const KeyType & {
  BUSTUB_ASSERT(high_key_is_valid_, "Memory corrupted?");
  return high_key_;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::IsOverflowed() const -> bool { return GetSize() >= GetMaxSize(); }

/** 无法通过均分解决 Overflow. */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::IsOverflowedWith(ReadPageGuard &rpage_g) const -> bool {
  int cnt_sum{GetSize() + rpage_g.As<BPlusTreeInternalPage>()->GetSize()};
  /** Ceil. */
  return (cnt_sum + 1) / 2 >= GetMaxSize();
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::IsUnderflowed() const -> bool { return GetSize() <= GetMinSize(); }

/** 无法通过均分解决 Underflow. */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::IsUnderflowedWith(ReadPageGuard &rpage_g) const -> bool {
  int cnt_sum{GetSize() + rpage_g.As<BPlusTreeInternalPage>()->GetSize()};
  /** Floor. */
  return (cnt_sum / 2) <= GetMinSize();
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindIndexUpperbound(const KeyType &key, KeyComparator &cmp) const -> int {
  /** 先检查 high_key. */
  if (high_key_is_valid_ && cmp(key, high_key_) >= 0) {
    return GetSize();  // end();
  }

  int l{0};          // [0, l) -> x <= key.
  int r{GetSize()};  // [r, GetSize()) -> x > key.
  while (r - l > 0) {
    int mid{l + ((r - l) / 2)};
    if (cmp(key_array_[mid], key) <= 0) {
      l = mid + 1;
    } else {
      r = mid;
    }
  }
  return r;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindIndex(const KeyType &key, const ValueType &val, KeyComparator &cmp) const
    -> int {
  if (high_key_is_valid_ && cmp(key, high_key_) >= 0) {
    return GetSize();
  }

  int upperbound_idx{FindIndexUpperbound(key, cmp)};
  for (int i = upperbound_idx - 1; i > 0; i--) {
    if (cmp(key, key_array_[i]) != 0) {
      break;
    }
    if (page_id_array_[i] == val) {
      return i;
    }
  }
  return GetSize();
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::NewKeyAt(int index) {
  // todo.
  int begin{index};
  int to{index + 1};
  int mv_cnt{GetSize() - begin};
  std::memmove(key_array_ + to, key_array_ + begin, sizeof(KeyType) * mv_cnt);
  std::memmove(page_id_array_ + to, page_id_array_ + begin, sizeof(ValueType) * mv_cnt);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::ModifyKeyAt(int index, const KeyType &key, const ValueType &val) {
  key_array_[index] = key;
  page_id_array_[index] = val;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::DeleteKeyAt(int index) {
  int begin{index + 1};
  int to{index};
  int mv_cnt{GetSize() - begin};
  std::memmove(key_array_ + to, key_array_ + begin, sizeof(KeyType) * mv_cnt);
  std::memmove(page_id_array_ + to, page_id_array_ + begin, sizeof(ValueType) * mv_cnt);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
