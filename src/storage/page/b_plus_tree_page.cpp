//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_page.cpp
//
// Identification: src/storage/page/b_plus_tree_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/b_plus_tree_page.h"

namespace bustub {

/*
 * Helper methods to get/set page type
 * Page type enum class is defined in b_plus_tree_page.h
 */
auto BPlusTreePage::IsLeafPage() const -> bool { return page_type_ == IndexPageType::LEAF_PAGE; }
void BPlusTreePage::SetPageType(IndexPageType page_type) { page_type_ = page_type; }

/*
 * Helper methods to get/set size (number of key/value pairs stored in that
 * page)
 */
auto BPlusTreePage::GetSize() const -> int { return size_; }
void BPlusTreePage::SetSize(int size) { size_ = size; }
void BPlusTreePage::ChangeSizeBy(int amount) { size_ += amount; }

/*
 * Helper methods to get/set max size (capacity) of the page
 */
auto BPlusTreePage::GetMaxSize() const -> int { return max_size_; }
void BPlusTreePage::SetMaxSize(int size) { max_size_ = size; }

/*
 * Helper method to get min page size
 * Generally, min page size == max page size / 2
 * But whether you will take ceil() or floor() depends on your implementation
 *
 * B+ 索引节点合并, 合并时新增来自 parent 的分隔 kv, 节点数量最多:
 *
 * B+ 叶子节点合并,
 * MIN + MIN+1 <= MAX
 * MIN <= (MAX - 1) / 2.
 *
 * B+ 索引节点分裂, 失去一个分隔 kv 移动到 parent 节点.
 *
 * B+ 叶子节点分裂, 分裂不丢失分隔 kv, 节点数量最多:
 * ceil(MAX / 2) >= MIN
 * MIN <= (MAX + 1) / 2.
 *
 * 优先尝试均分, 若失败,
 * 在插入且 >= MAX 时分裂, 删除且 <= MIN 时合并.
 */
auto BPlusTreePage::GetMinSize() const -> int {
  BUSTUB_ASSERT(page_type_ != IndexPageType::INVALID_INDEX_PAGE, "Invalid page type.");
  int ret{(max_size_ - 1) / 2};
  /** Maybe 0 is legal. */
  BUSTUB_ASSERT(ret >= 0, "`max_size_` is too small.");
  return ret;
}

auto BPlusTreePage::GetLevel() const -> int {
  BUSTUB_ASSERT(!IsLeafPage() || level_ == 1, "Unexpected.");
  return level_;
}
void BPlusTreePage::SetLevel(int level) {
  BUSTUB_ASSERT(!IsLeafPage() || level == 1, "Unexpected.");
  level_ = level;
}

auto BPlusTreePage::GetBackfixStatus() const -> BackfixStatus {
  BUSTUB_ASSERT(backfix_status_ != BackfixStatus::INVALID, "Protocol error.");
  return backfix_status_;
}

void BPlusTreePage::SetBackfixStatus(BackfixStatus status) {
  // BUSTUB_ASSERT(status != BackfixStatus::INVALID, "Protocol error.");
  backfix_status_ = status;
}

auto BPlusTreePage::GetNextPageId() const -> page_id_t { return next_page_id_; }

void BPlusTreePage::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

auto BPlusTreePage::IsLevelHead() const -> bool { return is_level_head_; }

void BPlusTreePage::SetIsLevelHead(bool is_level_head) {
  BUSTUB_ASSERT(GetSize() > 0, "Invalid operaton.");
  is_level_head_ = is_level_head;
}

auto BPlusTreePage::GetLowLevelHeadId() const -> page_id_t {
  BUSTUB_ASSERT(IsLevelHead(), "Invalid operation.");
  return low_level_head_id_;
}
void BPlusTreePage::SetLowLevelHeadId(page_id_t low_level_head_id) {
  // BUSTUB_ASSERT(IsLevelHead(), "Invalid operation.");
  low_level_head_id_ = low_level_head_id;
}

auto BPlusTreePage::IsOverflowed() const -> bool { return GetSize() >= GetMaxSize(); }

/** 无法通过均分解决 Overflow. */
auto BPlusTreePage::IsOverflowedAfterMergeWith(WritePageGuard &rpage_g) const -> bool {
  int cnt_sum{GetSize() + rpage_g.As<BPlusTreePage>()->GetSize()};
  /** Ceil. */
  return cnt_sum > GetMaxSize();
}

auto BPlusTreePage::IsUnderflowed() const -> bool { return GetSize() <= GetMinSize(); }

/**
关于 min_size_ 的计算草稿.
索引节点

合并:

MIN + MIN+1 <= MAX
MIN <= (MAX-1) / 2.

分裂:

ceil(MAX / 2) >= MIN
(MAX + 1) / 2 >= MIN
MIN <= (MAX + 1) / 2.
*/

}  // namespace bustub
