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

  auto GetLowerlevelHeadId() const -> page_id_t;
  void SetLowerlevelHeadId(page_id_t standby_id);

  /**
   * 只有在 `Is(Over|Under)flowedWith` 返回 `false` 时,
   * 才应该采取 Split/Merge.
   */
  auto IsOverflowed() const -> bool;

  /**
   * Merge 会导致 Overflow，
   * 则不允许 Merge.
   */
  auto IsOverflowedAfterMergeWith(WritePageGuard &rpage_g) const -> bool;

  auto IsUnderflowed() const -> bool;

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
  int max_size_{};

  /**
   * Level.
   * 用于 backfix insert kv 重建路径向上回溯.
   */
  int level_{};

  /**
   * Backfix status.
   * 用于 backfix remove kv 向上回溯.
   */
  BackfixStatus backfix_status_{BackfixStatus::INVALID};

  /**
   * 当前 Level 下一个 Node id.
   */
  page_id_t next_page_id_{INVALID_PAGE_ID};

  /**
   * 是当前 Level 的起始 Node.
   * 在被从 parent unlink 之后, 可以被直接 QSBR Retire.
   * 对于其他 Node, 由于被 Level 左侧 Node 的 Sibling Link 连接,
   * 不可 Retire.
   *
   * Level 起始 Node 发生 Merge 后,
   * 应继续尝试 Merge.
   */
  bool is_level_head_{};

  /**
   * 指向下一层最左侧节点.
   * 若当前节点是最左侧 Internal 节点, 则和 `val[0]` 相等.
   * 否则, 作为待选, 在继承来自被 `MergeToRight` 的左侧 Internal 节点时,
   * 作为新的 `val[0]` 覆盖来自左侧的.
   *
   * 此字段实际维护指向下层最右侧节点的 id,
   * 当前 Level Head 通过 `val[0]` 来向 `ValueAt` 接口暴露此 id.
   */
  page_id_t lowerlevel_head_id_{INVALID_PAGE_ID};

  /**
   * BPlusTreePage 不需要额外的锁,
   * 所有的 Page Latch 基于 BPM PageGuard 管理.
   */
};

constexpr int GENERAL_PAGE_HEADER_SIZE{sizeof(BPlusTreePage)};

}  // namespace bustub
