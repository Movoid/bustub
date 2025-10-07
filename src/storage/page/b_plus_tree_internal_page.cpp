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
  BUSTUB_ASSERT(max_size > 0 && static_cast<std::uint64_t>(max_size) <= INTERNAL_PAGE_SLOT_CNT_MAX, "Invalid init.");
  SetSize(0);
  SetMaxSize(max_size);
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetNextPageId(INVALID_PAGE_ID);
  SetLowKey(KeyType{});
  SetBackfixStatus(BackfixStatus::IS_LINKED);
}
/**
 * @brief Helper method to get/set the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> const KeyType & {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
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
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
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
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> const ValueType & {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  return page_id_array_[index];
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetHighKey() const -> const KeyType & {
  BUSTUB_ASSERT(GetNextPageId() != INVALID_PAGE_ID, "Memory corrupted?");
  return hdr_.highkey_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetHighKey(const KeyType &high_key) { hdr_.highkey_ = high_key; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetLowKey() const -> const KeyType & { return hdr_.lowkey_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetLowKey(const KeyType &low_key) { hdr_.lowkey_ = low_key; }

/**
 * InternalNode 的 `high_key_` 是 inclusive upperbound.
 * 即, 当前 Node 可能包含 `high_key_`.
 *
 * 无论如何, 当 `high_key_` invalid 时,
 * 必须停止.
 * 只有最右侧的节点允许 `high_key_` invalid,
 * 被 Merge 操作清空的节点不允许设置 invalid.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::CoveredKey(const KeyType &key, KeyComparator &cmp) const -> bool {
  return GetNextPageId() == INVALID_PAGE_ID || (GetSize() > 0 && cmp(key, GetHighKey()) <= 0);
}

/**
 * 此函数需要适配 `low_key` 机制.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindIndexUpperbound(const KeyType &key, KeyComparator &cmp) const -> int {
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

/**
 * 此函数需要适配 `low_key` 机制.
 * 如果 lowkey is invalid 则指向最左侧 -inf 区间.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindIndexLowerbound(const KeyType &key, KeyComparator &cmp) const -> int {
  int l{0};          // [0, l) -> x < key.
  int r{GetSize()};  // [r, GetSize()) -> x >= key.
  while (r - l > 0) {
    int mid{l + ((r - l) / 2)};
    if (cmp(key_array_[mid], key) < 0) {
      l = mid + 1;
    } else {
      r = mid;
    }
  }
  return r;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindKv(const KeyType &key, const ValueType &val, KeyComparator &cmp) const -> int {
  int lowerbound_idx{FindIndexLowerbound(key, cmp)};
  int upperbound_idx{FindIndexUpperbound(key, cmp)};
  for (int i = lowerbound_idx; i < upperbound_idx; i++) {
    if (ValueAt(i) == val) {
      return i;
    }
  }
  return GetSize();
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::NewKvAt(int index) {
  int begin{index};
  int to{index + 1};
  int mv_cnt{GetSize() - begin};
  BUSTUB_ASSERT(mv_cnt >= 0, "Invalid args.");
  BUSTUB_ASSERT(0 <= index && index <= GetSize(), "Invalid args.");
  std::memmove(key_array_ + to, key_array_ + begin, sizeof(KeyType) * mv_cnt);
  std::memmove(page_id_array_ + to, page_id_array_ + begin, sizeof(ValueType) * mv_cnt);
  SetSize(GetSize() + 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::ModifyKvAt(int index, const KeyType &key, const ValueType &val) {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  key_array_[index] = key;
  page_id_array_[index] = val;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::DeleteKvAt(int index) {
  int begin{index + 1};
  int to{index};
  int mv_cnt{GetSize() - begin};
  BUSTUB_ASSERT(mv_cnt >= 0, "Invalid args.");
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  std::memmove(key_array_ + to, key_array_ + begin, sizeof(KeyType) * mv_cnt);
  std::memmove(page_id_array_ + to, page_id_array_ + begin, sizeof(ValueType) * mv_cnt);
  SetSize(GetSize() - 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RedistributeWithRight(WritePageGuard &rpage_g, int l_size, int r_size) {
  int l_diff_cnt{l_size - GetSize()};
  int r_diff_cnt{r_size - rpage_g.As<BPlusTreeInternalPage>()->GetSize()};

  BUSTUB_ASSERT(l_diff_cnt + r_diff_cnt == 0, "Invalid args.");
  BUSTUB_ASSERT(r_size != 0 || l_size == 0, "This operation is denied.");

  auto rpage_data{rpage_g.AsMut<BPlusTreeInternalPage>()};

  if (l_diff_cnt > 0) {
    // l need `+l_diff_cnt` kv pairs.
    std::memmove(key_array_ + GetSize(), rpage_data->key_array_, sizeof(KeyType) * l_diff_cnt);
    std::memmove(page_id_array_ + GetSize(), rpage_data->page_id_array_, sizeof(ValueType) * l_diff_cnt);
    std::memmove(rpage_data->key_array_, rpage_data->key_array_ + l_diff_cnt,
                 sizeof(KeyType) * (rpage_data->GetSize() - l_diff_cnt));
    std::memmove(rpage_data->page_id_array_, rpage_data->page_id_array_ + l_diff_cnt,
                 sizeof(ValueType) * (rpage_data->GetSize() - l_diff_cnt));

  } else if (r_diff_cnt > 0) {
    // r need `+r_diff_cnt` kv pairs.
    std::memmove(rpage_data->key_array_ + r_diff_cnt, rpage_data->key_array_, sizeof(KeyType) * rpage_data->GetSize());
    std::memmove(rpage_data->page_id_array_ + r_diff_cnt, rpage_data->page_id_array_,
                 sizeof(ValueType) * rpage_data->GetSize());
    std::memmove(rpage_data->key_array_, key_array_ + GetSize() - r_diff_cnt, sizeof(KeyType) * r_diff_cnt);
    std::memmove(rpage_data->page_id_array_, page_id_array_ + GetSize() - r_diff_cnt, sizeof(ValueType) * r_diff_cnt);
  }

  SetSize(l_size);
  rpage_data->SetSize(r_size);

  if (GetSize() == 0) {  // Merge to right.
    if (rpage_data->GetSize() != 0) {
      SetHighKey(rpage_data->KeyAt(0));
    }
  } else if (rpage_data->GetSize() == 0) {  // Merge to left.
    if (rpage_data->GetNextPageId() != INVALID_PAGE_ID) {
      SetHighKey(rpage_data->GetHighKey());
    }
    rpage_data->SetHighKey(KeyAt(0));
  } else {  // Split.
    BUSTUB_ASSERT(GetSize() != 0 && rpage_data->GetSize() != 0, "Invalid args.");
    if (GetNextPageId() != INVALID_PAGE_ID) {
      rpage_data->SetHighKey(GetHighKey());
    }
    rpage_data->SetLowKey(rpage_data->KeyAt(0));
    SetHighKey(rpage_data->KeyAt(0));
  }
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
