//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.cpp
//
// Identification: src/storage/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new leaf page
 *
 * After creating a new leaf page from buffer pool, must call initialize method to set default values,
 * including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size.
 *
 * @param max_size Max size of the leaf node
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  BUSTUB_ASSERT(max_size > 0 && static_cast<std::uint64_t>(max_size) <= LEAF_PAGE_SLOT_CNT_MAX, "Invalid init.");
  SetSize(0);
  SetMaxSize(max_size);
  SetPageType(IndexPageType::LEAF_PAGE);
  SetNextPageId(INVALID_PAGE_ID);
  SetLowKey(KeyType{});
  SetBackfixStatus(BackfixStatus::INVALID);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t {
  // BUSTUB_ASSERT(next_page_id_ > 0, "Memory corrupted?");
  return hdr_.next_page_id_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) {
  // BUSTUB_ASSERT(next_page_id_ > 0, "Memory corrupted?");
  hdr_.next_page_id_ = next_page_id;
}

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> const KeyType & {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  return key_array_[index];
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  key_array_[index] = key;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> const ValueType & {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  return rid_array_[index];
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetHighKey() const -> const KeyType & {
  BUSTUB_ASSERT(GetNextPageId() != INVALID_PAGE_ID, "Memory corrupted?");
  return hdr_.highkey_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetHighKey(const KeyType &high_key) {
  BUSTUB_ASSERT(GetNextPageId() != INVALID_PAGE_ID, "Memory corrupted?");
  hdr_.highkey_ = high_key;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetLowKey() const -> const KeyType & { return hdr_.lowkey_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetLowKey(const KeyType &low_key) { hdr_.lowkey_ = low_key; }

/**
 * 和 InternalNode 不同,
 * LeafNode 的 `high_key_` 是 exclusive upperbound.
 * 即, 当前 Node 不包括 `high_key_`.
 *
 * 注意! `GetSize() == 0` 的节点不可复用,
 * 否则导致 Use After Free.
 *
 * 除了最右侧节点, 其他所有节点都不允许设置 `high_key_is_valid_ == false`,
 * 否则可能对空节点错误判断为 `CoveredKey() == true`.
 *
 * WARNING: 存在问题.
 * 若当前 Leaf 是最右侧节点且发生 Merge,
 * 其 `HasValidHighKey() == false` 误导了 `GoSib*` 留在当前节点.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::CoveredKey(const KeyType &key, KeyComparator &cmp) const -> bool {
  return GetNextPageId() == INVALID_PAGE_ID || (GetSize() && cmp(key, GetHighKey()) < 0);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::FindIndexUpperbound(const KeyType &key, KeyComparator &cmp) const -> int {
  int l{0};
  int r{GetSize()};
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
auto B_PLUS_TREE_LEAF_PAGE_TYPE::FindIndexLowerbound(const KeyType &key, KeyComparator &cmp) const -> int {
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
void B_PLUS_TREE_LEAF_PAGE_TYPE::NewKvAt(int index) {
  int begin{index};
  int to{index + 1};
  int mv_cnt{GetSize() - begin};
  BUSTUB_ASSERT(mv_cnt >= 0, "Invalid args.");
  /**
   * NOTICE: `index` cannot be `0`. This would break invariants.
   */
  BUSTUB_ASSERT(0 <= index && index <= GetSize(), "Invalid args.");
  std::memmove(key_array_ + to, key_array_ + begin, sizeof(KeyType) * mv_cnt);
  std::memmove(rid_array_ + to, rid_array_ + begin, sizeof(ValueType) * mv_cnt);
  SetSize(GetSize() + 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::ModifyKvAt(int index, const KeyType &key, const ValueType &val) {
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  key_array_[index] = key;
  rid_array_[index] = val;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::DeleteKvAt(int index) {
  int begin{index + 1};
  int to{index};
  int mv_cnt{GetSize() - begin};
  BUSTUB_ASSERT(mv_cnt >= 0, "Invalid args.");
  BUSTUB_ASSERT(0 <= index && index < GetSize(), "Invalid args.");
  std::memmove(key_array_ + to, key_array_ + begin, sizeof(KeyType) * mv_cnt);
  std::memmove(rid_array_ + to, rid_array_ + begin, sizeof(ValueType) * mv_cnt);
  SetSize(GetSize() - 1);
}

/**
 * WARNING: 此函数不修改 SibLink.
 *
 * 此函数需要优化. Todo.
 * 实际只会有两种操作, Split 和 Merge.
 * 不存在均分操作.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::RedistributeWithRight(WritePageGuard &rpage_g_w, int l_size, int r_size) {
  int l_diff_cnt{l_size - GetSize()};
  int r_diff_cnt{r_size - rpage_g_w.As<BPlusTreeLeafPage>()->GetSize()};

  BUSTUB_ASSERT(l_diff_cnt + r_diff_cnt == 0, "Invalid args.");
  BUSTUB_ASSERT(r_size != 0, "This operation is denied.");

  auto rpage_data{rpage_g_w.AsMut<BPlusTreeLeafPage>()};

  if (l_diff_cnt > 0) {
    // l need `+l_diff_cnt` kv pairs.
    std::memmove(key_array_ + GetSize(), rpage_data->key_array_, sizeof(KeyType) * l_diff_cnt);
    std::memmove(rid_array_ + GetSize(), rpage_data->rid_array_, sizeof(ValueType) * l_diff_cnt);
    std::memmove(rpage_data->key_array_, rpage_data->key_array_ + l_diff_cnt,
                 sizeof(KeyType) * (rpage_data->GetSize() - l_diff_cnt));
    std::memmove(rpage_data->rid_array_, rpage_data->rid_array_ + l_diff_cnt,
                 sizeof(ValueType) * (rpage_data->GetSize() - l_diff_cnt));

  } else if (r_diff_cnt > 0) {
    // r need `+r_diff_cnt` kv pairs.
    std::memmove(rpage_data->key_array_ + r_diff_cnt, rpage_data->key_array_, sizeof(KeyType) * rpage_data->GetSize());
    std::memmove(rpage_data->rid_array_ + r_diff_cnt, rpage_data->rid_array_,
                 sizeof(ValueType) * rpage_data->GetSize());
    std::memmove(rpage_data->key_array_, key_array_ + GetSize() - r_diff_cnt, sizeof(KeyType) * r_diff_cnt);
    std::memmove(rpage_data->rid_array_, rid_array_ + GetSize() - r_diff_cnt, sizeof(ValueType) * r_diff_cnt);
  }

  SetSize(l_size);
  rpage_data->SetSize(r_size);

  /**
   * WARNING: Leaf Node 的 lowkey 允许和 `key[0]` 不相等.
   * 不要随意修改 lowkey.
   */

  if (GetSize() == 0) {  // Merged to sibling.
    SetHighKey(rpage_data->KeyAt(0));
  } else {  // Splited to sibling.
    BUSTUB_ASSERT(GetSize() != 0 && rpage_data->GetSize() != 0, "Invalid args.");
    if (GetNextPageId() != INVALID_PAGE_ID) {
      rpage_data->SetHighKey(GetHighKey());
    }
    rpage_data->SetLowKey(rpage_data->KeyAt(0));
    SetHighKey(rpage_data->KeyAt(0));
    // rpage_data->SetNextPageId(GetNextPageId());
    // SetNextPageId(rpage_g_w.GetPageId());
  }
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
