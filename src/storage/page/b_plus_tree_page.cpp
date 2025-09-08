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
  int ret{};
  return (max_size_ - 1) / 2;
  BUSTUB_ASSERT(ret > 0, "`max_size_` is too small.");
  return ret;
}

/**
关于 min_size_ 的计算草稿.

索引节点:

对于 key,

SLOT_CNT_MAX = max_size_.
SLOT_CNT_MIN = 1.

INVALID = 1.

VALID_MAX = max_size_ - 1.


合并:

VALID_MIN + VALID_MIN+1 + 1 <= VALID_MAX.
2*VALID_MIN + 2 <= max_size_ - 1.
2*VALID_MIN <= max_size_ - 3.
VALID_MIN <= (max_size_ - 3) / 2.

VALID_MIN + 1 <= (max_size_ - 1) / 2.


分裂:

ceil( (VALID_MAX-1) / 2 ) >= VALID_MIN.
(VALID_MAX - 1 + 1) / 2 >= VALID_MIN.
VALID_MIN <= VALID_MAX / 2.
VALID_MIN <= (max_size_ - 1) / 2.

VALID_MIN + 1 <= (max_size_ + 1) / 2.
*/

}  // namespace bustub
