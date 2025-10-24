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

#include "storage/page/b_plus_tree_leaf_page.h"
#include "common/rid.h"

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
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  SetLevel(1);
  SetBackfixStatus(BackfixStatus::INVALID);
  SetNextPageId(INVALID_PAGE_ID);
  SetIsLevelHead(false);
  SetLowerlevelHeadId(INVALID_PAGE_ID);
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
  // BUSTUB_ASSERT(GetNextPageId() != INVALID_PAGE_ID, "Memory corrupted?");
  hdr_.highkey_ = high_key;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetLowKey() const -> const KeyType & {
  BUSTUB_ASSERT(!IsLevelHead(), "Invalid operation.");
  return hdr_.lowkey_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetLowKey(const KeyType &low_key) {
  BUSTUB_ASSERT(!IsLevelHead(), "Invalid operation.");
  hdr_.lowkey_ = low_key;
}

/**
 * 和 InternalNode 不同,
 * LeafNode 的 `high_key_` 是 exclusive upperbound.
 * 即, 当前 Node 不包括 `high_key_`.
 *
 * 不允许复用 IS_UNLINKED/NEED_UNLINK 节点,
 * 其他的 `GetSize() == 0` 节点允许复用.
 *
 * 除了最右侧节点, 其他所有节点都不允许设置 `high_key_is_valid_ == false`,
 * 否则可能对空节点错误判断为 `CoveredKey() == true`.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::CoveredKey(const KeyType &key, KeyComparator &cmp) const -> bool {
  auto status{GetBackfixStatus()};
  if (status == BackfixStatus::IS_UNLINKED || status == BackfixStatus::NEED_UNLINK) {
    return false;
  }
  return GetNextPageId() == INVALID_PAGE_ID || cmp(key, GetHighKey()) < 0;
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

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MergeToRight(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeLeafPage>()};

  if (GetSize() > 0) {
    std::memmove(sib_data->key_array_ + GetSize(), sib_data->key_array_, sizeof(KeyType) * sib_data->GetSize());
    std::memmove(sib_data->rid_array_ + GetSize(), sib_data->rid_array_, sizeof(ValueType) * sib_data->GetSize());
    std::memcpy(sib_data->key_array_, key_array_, sizeof(KeyType) * GetSize());
    std::memcpy(sib_data->rid_array_, rid_array_, sizeof(ValueType) * GetSize());
  }

  sib_data->SetSize(sib_data->GetSize() + GetSize());
  SetSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MergeFromRight(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeLeafPage>()};

  if (sib_data->GetSize() > 0) {
    std::memcpy(key_array_ + GetSize(), sib_data->key_array_, sizeof(KeyType) * sib_data->GetSize());
    std::memcpy(rid_array_ + GetSize(), sib_data->rid_array_, sizeof(ValueType) * sib_data->GetSize());
  }

  SetSize(GetSize() + sib_data->GetSize());
  sib_data->SetSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SplitToRight(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeLeafPage>()};
  BUSTUB_ASSERT(GetSize() >= 2, "Unexpected.");
  BUSTUB_ASSERT(sib_data->GetSize() == 0, "Unexpected.");

  int kv_sum{GetSize() + sib_data->GetSize()};
  int l_kv_sum{(kv_sum + 1) / 2};
  int r_kv_sum{kv_sum / 2};

  std::memcpy(sib_data->key_array_, key_array_ + GetSize() - r_kv_sum, sizeof(KeyType) * r_kv_sum);
  std::memcpy(sib_data->rid_array_, rid_array_ + GetSize() - r_kv_sum, sizeof(ValueType) * r_kv_sum);
  SetSize(l_kv_sum);
  sib_data->SetSize(r_kv_sum);

  sib_data->SetLowKey(sib_data->KeyAt(0));
  sib_data->SetLevel(GetLevel());
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::LinkToNewSibling(WritePageGuard &sib_g_w) {
  auto sib_data{sib_g_w.AsMut<BPlusTreeLeafPage>()};

  sib_data->SetNextPageId(GetNextPageId());
  if (GetNextPageId() != INVALID_PAGE_ID) {
    sib_data->SetHighKey(GetHighKey());
  }

  SetNextPageId(sib_g_w.GetPageId());
  SetHighKey(sib_data->GetLowKey());
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
