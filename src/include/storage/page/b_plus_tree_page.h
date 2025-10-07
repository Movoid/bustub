//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_page.h
//
// Identification: src/include/storage/page/b_plus_tree_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <climits>
#include <cstdlib>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "storage/index/generic_key.h"

namespace bustub {

#define MappingType std::pair<KeyType, ValueType>

#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>

// define page type enum
enum class IndexPageType : std::uint8_t {
  INVALID_INDEX_PAGE = 0,
  LEAF_PAGE,
  INTERNAL_PAGE,
};

/**
 * Backfix Protocol Status.
 */
enum class BackfixStatus : std::uint8_t {
  INVALID,
  NEED_LINK,
  NEED_UNLINK,
  IS_LINKED,
  IS_UNLINKED,
};

enum class BackfixType : std::uint8_t {
  INVALID,
  INSERT,
  REMOVE,
};

/**
 * Both internal and leaf page are inherited from this page.
 *
 * It actually serves as a header part for each B+ tree page and
 * contains information shared by both leaf page and internal page.
 *
 * Header format (size in byte, 12 bytes in total):
 * ---------------------------------------------------------
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |  ...   |
 * ---------------------------------------------------------
 *
 * 此实现中, 所有的 size 语义都是指 key 的数量.
 * 所有 internal page 的 key[0] 都是无效,
 * 然而, 无效的 key **也参与 size 计数** .
 * 所有 leaf page 的 val[0] 有效.
 *
 * 关于对 BPlusTreePage 的操作, 都必须基于 page id,
 * 从 bpm 获得 PageGuard, 并 `reinterpret_cast` 为 BPlusTreePage 来操作.
 *
 * 这不是 class template, 涉及 value type 的东西不应在此处定义.
 */
class BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreePage() = delete;
  BPlusTreePage(const BPlusTreePage &other) = delete;
  ~BPlusTreePage() = delete;

  auto IsLeafPage() const -> bool;
  void SetPageType(IndexPageType page_type);

  auto GetSize() const -> int;
  void SetSize(int size);
  void ChangeSizeBy(int amount);

  auto GetMaxSize() const -> int;
  void SetMaxSize(int max_size);
  auto GetMinSize() const -> int;

  auto GetLevel() const -> int;
  void SetLevel(int level);

  auto GetBackfixStatus() const -> BackfixStatus;
  void SetBackfixStatus(BackfixStatus status);

  // Helper methods
  auto GetNextPageId() const -> page_id_t;
  void SetNextPageId(page_id_t next_page_id);

  auto IsLevelHead() const -> bool;
  void SetIsLevelHead(bool is_level_head);

  /**
   * 对 Level Head Node 的特殊机制.
   * 指向 [-inf, ...) 区间, 此 Node 在当前节点无 kv 对应.
   *
   * 关于 `lowkey_`, Level Head Node 没有 `lowkey_`,
   * 不以正常方式 link 在 parent 中.
   *
   * 非 Level Head Node 保留 `low_level_head_id_` Slot,
   * 以便在从左向右的 Merge 中继承来自旧 Level Head 的字段.
   */
  auto GetLowLevelHeadId() const -> page_id_t;
  void SetLowLevelHeadId(page_id_t low_level_head_id);

  /**
   * 只有在 `Is{Over, Under}flowedWith` 返回 `false` 时,
   * 才应该采取 Split/Merge.
   */
  auto IsOverflowed() const -> bool;

  /**
   * Merge 会导致 Overflow，
   * 则不允许 Merge.
   */
  auto IsOverflowedAfterMergeWith(WritePageGuard &rpage_g) const -> bool;

  auto IsUnderflowed() const -> bool;

  // /** 无法通过均分解决 Underflow. */
  // auto IsUnderflowedWith(WritePageGuard &rpage_g) const -> bool;

  /*
   * TODO(P2): Remove __attribute__((__unused__)) if you intend to use the fields.
   */
 private:
  // Member variables, attributes that both internal and leaf page share
  IndexPageType page_type_{IndexPageType::INVALID_INDEX_PAGE};
  /**
   * Number of key & value pairs in a page
   * 无论什么类型节点, 都指示当前已有的 key 数量.
   */
  int size_{};
  /**
   * Max number of key & value pairs in a page
   * 无论什么类型节点, 都指示最大的 key 数量.
   */
  int max_size_;

  /**
   * Level.
   * 用于 backfix insert kv 重建路径向上回溯.
   */
  int level_;

  /**
   * Backfix status.
   * 用于 backfix remove kv 向上回溯.
   */
  BackfixStatus backfix_status_;

  /**
   * 当前 Level 下一个 Node id.
   */
  page_id_t next_page_id_;

  /**
   * 是当前 Level 的起始 Node.
   * 在被从 parent unlink 之后, 可以被直接 QSBR Retire.
   * 对于其他 Node, 由于被 Level 左侧 Node 的 Sibling Link 连接,
   * 不可 Retire.
   *
   * Level 起始 Node 发生 Merge 后,
   * 应继续尝试 Merge.
   */
  bool is_level_head_;

  /**
   * 若向下遍历时 `upperbound_idx == 0` 则前往此 page.
   * 只有 Level head 才能设置此字段.
   *
   * 由于向右合并,
   * 若被合并的是其 Level 的 head,
   * 则需要在 backfix 过程中更新此 id 为新的 Level head.
   * 且这个过程必须和 `DeleteKvAt` 是原子的.
   * > 在修改此字段前, 二次校验新的 id 是否是 level head.
   */
  page_id_t low_level_head_id_;

  /**
   * BPlusTreePage 不需要额外的锁,
   * 所有的 Page Latch 基于 BPM PageGuard 管理.
   */
};

constexpr int GENERAL_PAGE_HEADER_SIZE{sizeof(BPlusTreePage)};

}  // namespace bustub
