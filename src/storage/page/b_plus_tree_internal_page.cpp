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
 * Internal 节点, 根据 `is_level_head_` 有不同行为.
 * Tree 中最 **左侧** 的 Internal 节点是 **特殊** 的,
 * 拥有一个 placeholder `key[0]`, 其 `val[0]` 指向最左侧 [-inf, ...) 区间.
 * 其他的 Internal 节点不包含此 kv.
 *
 * 在 `TryNewRoot` 创建 Internal 节点时,
 * 需要手动 `ModifyKvAt()` 为其设置 placeholder `key[0]`.
 *
 * 对于最左侧节点, `FindIndex*` 需要根据 `is_level_head_` 对 `key[0]` 特判.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  BUSTUB_ASSERT(max_size > 0 && static_cast<std::uint64_t>(max_size) <= INTERNAL_PAGE_SLOT_CNT_MAX, "Invalid init.");
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  SetLevel(-1);
  SetBackfixStatus(BackfixStatus::INVALID);
  SetNextPageId(INVALID_PAGE_ID);
  SetIsLevelHead(false);
  SetLowerlevelHeadId(INVALID_PAGE_ID);
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
  BUSTUB_ASSERT(GetNextPageId() != INVALID_PAGE_ID, "Invalid operation.");
  return hdr_.highkey_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetHighKey(const KeyType &high_key) {
  BUSTUB_ASSERT(GetNextPageId() != INVALID_PAGE_ID, "Invalid operation.");
  hdr_.highkey_ = high_key;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetLowKey() const -> const KeyType & {
  BUSTUB_ASSERT(!IsLevelHead(), "Invalid operation.");
  return hdr_.lowkey_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetLowKey(const KeyType &low_key) {
  BUSTUB_ASSERT(!IsLevelHead(), "Invalid operation.");
  hdr_.lowkey_ = low_key;
}

/**
 * InternalNode 的 `high_key_` 是 inclusive upperbound.
 * 即, 当前 Node 可能包含 `high_key_`.
 *
 * 无论如何, 当 `high_key_` invalid 时,
 * 必须停止.
 * 只有最右侧的节点允许 `high_key_` invalid,
 * 被 Merge 操作清空的节点不允许设置 invalid.
 *
 * 考虑空的 Internal Node,
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::CoveredKey(const KeyType &key, KeyComparator &cmp) const -> bool {
  auto status{GetBackfixStatus()};
  if (status == BackfixStatus::IS_UNLINKED || status == BackfixStatus::NEED_UNLINK) {
    return false;
  }
  return GetNextPageId() == INVALID_PAGE_ID || cmp(key, GetHighKey()) <= 0;
}

/**
 * 此函数需要适配 `low_key` 机制.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::FindIndexUpperbound(const KeyType &key, KeyComparator &cmp) const -> int {
  int l{IsLevelHead() ? 1 : 0};  // [0, l) -> x <= key.
  int r{GetSize()};              // [r, GetSize()) -> x > key.
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
  int l{IsLevelHead() ? 1 : 0};  // [0, l) -> x < key.
  int r{GetSize()};              // [r, GetSize()) -> x >= key.
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
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Locate(const KeyType &key, KeyComparator &cmp) const -> page_id_t {
  BUSTUB_ASSERT(!IsLevelHead() || GetLowerlevelHeadId() == ValueAt(0), "Unexpected.");
  BUSTUB_ASSERT(!(GetNextPageId() != INVALID_PAGE_ID) || cmp(key, GetHighKey()) <= 0, "Unexpected.");

  int lowerbound_idx{FindIndexLowerbound(key, cmp)};
  /**
   * 使用 lowerbound 路由, 必须 `-1`.
   * 考虑部分等于 `key` 的 kv 存在于 `lowerbound_idx` 的前一个节点,
   * 但此 kv 的 `lowkey_` 可能小于 `key`, 直接取 `lowerbound_idx` 将错过.
   */
  /**
   * 考虑 Level Head 向右 Merge 的情况.
   * 新的 Level Head 在 parent 中的 kv 被删除, 并被设置为 parent `lowerlevel_head_id_`.
   * 若 parent 暂时还不是 Level Head, 即, 向右 Merge 涉及的两个节点不属于同一个 parent,
   * 新 Level Head 的 kv 在其 parent 中是 `val[0]`,
   * 这需要我们访问还没有立刻转为 Level Head 的 parent 的 `lowerlevel_head_id_`.
   */
  page_id_t lower_page_id{lowerbound_idx == 0 ? GetLowerlevelHeadId() : ValueAt(lowerbound_idx - 1)};
  return lower_page_id;
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

/**
 * 关于 Level Head 的 placeholder `kv[0]` 的设置,
 * 由上层控制.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergeToRight(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeInternalPage>()};
  /**
   * Sibling 可以是 `GetSize() == 0`.
   */
  if (GetSize() > 0) {
    std::memmove(sib_data->key_array_ + GetSize(), sib_data->key_array_, sizeof(KeyType) * sib_data->GetSize());
    std::memmove(sib_data->page_id_array_ + GetSize(), sib_data->page_id_array_,
                 sizeof(ValueType) * sib_data->GetSize());
    std::memcpy(sib_data->key_array_, key_array_, sizeof(KeyType) * GetSize());
    std::memcpy(sib_data->page_id_array_, page_id_array_, sizeof(ValueType) * GetSize());
  }

  sib_data->SetSize(GetSize() + sib_data->GetSize());
  SetSize(0);

  /**
   * Do not modify `kv[0]` here.
   * This should be done outside.
   */

  /**
   * No need to set `highkey_`.
   * This node cannot be reused.
   * Must skip this node in `CoveredKey`.
   */
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergeFromRight(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeInternalPage>()};

  if (sib_data->GetSize() > 0) {
    std::memcpy(key_array_ + GetSize(), sib_data->key_array_, sizeof(KeyType) * sib_data->GetSize());
    std::memcpy(page_id_array_ + GetSize(), sib_data->page_id_array_, sizeof(ValueType) * sib_data->GetSize());
  }

  SetSize(sib_data->GetSize() + GetSize());
  sib_data->SetSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SplitToRight(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeInternalPage>()};
  BUSTUB_ASSERT(GetSize() >= 2, "Unexpected.");
  BUSTUB_ASSERT(sib_data->GetSize() == 0, "Unexpected.");

  int kv_sum{GetSize() + sib_data->GetSize()};
  int l_kv_sum{(kv_sum + 1) / 2};
  int r_kv_sum{kv_sum / 2};

  std::memcpy(sib_data->key_array_, key_array_ + GetSize() - r_kv_sum, sizeof(KeyType) * r_kv_sum);
  std::memcpy(sib_data->page_id_array_, page_id_array_ + GetSize() - r_kv_sum, sizeof(ValueType) * r_kv_sum);
  SetSize(l_kv_sum);
  sib_data->SetSize(r_kv_sum);

  // Init `lowkey_`.
  sib_data->SetLowKey(sib_data->KeyAt(0));
  sib_data->SetLevel(GetLevel());
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::LinkToNewSibling(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeInternalPage>()};

  sib_data->SetNextPageId(GetNextPageId());
  if (GetNextPageId() != INVALID_PAGE_ID) {
    sib_data->SetHighKey(GetHighKey());
  }

  SetNextPageId(sib_g_w.GetPageId());
  SetHighKey(sib_data->GetLowKey());
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
