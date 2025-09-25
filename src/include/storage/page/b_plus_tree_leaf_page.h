//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.h
//
// Identification: src/include/storage/page/b_plus_tree_leaf_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>
#define LEAF_PAGE_HEADER_SIZE (sizeof(page_id_t) + sizeof(bool) + sizeof(KeyType))
#define LEAF_PAGE_SLOT_CNT \
  ((BUSTUB_PAGE_SIZE - GENERAL_PAGE_HEADER_SIZE - LEAF_PAGE_HEADER_SIZE) / (sizeof(KeyType) + sizeof(ValueType)))

/**
 * Store indexed key and record id (record id = page id combined with slot id,
 * see `include/common/rid.h` for detailed implementation) together within leaf
 * page. Only support unique key.
 *
 * Leaf page format (keys are stored in order):
 *  ---------
 * | HEADER |
 *  ---------
 *  ---------------------------------
 * | KEY(1) | KEY(2) | ... | KEY(n) |
 *  ---------------------------------
 *  ---------------------------------
 * | RID(1) | RID(2) | ... | RID(n) |
 *  ---------------------------------
 *
 *  Header format (size in byte, 16 bytes in total):
 *  -----------------------------------------------
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |
 *  -----------------------------------------------
 *  -----------------
 * | NextPageId (4) |
 *  -----------------
 *
 * 如果 KEY(1) 被修改, 这可能需要修改 parent 的对应 key.
 * 要么在 node layout 中添加一个额外的不变 KEY, 并使 parent 对应 key 不变,
 * 要么在 parent 中先删除再添加.
 * 也许可以选择后者, 对应的算法模块正在逐渐完善, 实现并不困难.
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTreeLeafPage : public BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreeLeafPage() = delete;
  BPlusTreeLeafPage(const BPlusTreeLeafPage &other) = delete;

  /**
   * 直接调用此函数初始化, 其中封装了对 BPlusTreePage 的初始化.
   */
  void Init(int max_size = LEAF_PAGE_SLOT_CNT);

  // Helper methods
  auto GetNextPageId() const -> page_id_t;
  void SetNextPageId(page_id_t next_page_id);
  auto KeyAt(int index) const -> const KeyType &;
  void SetKeyAt(int index, const KeyType &key);

  auto ValueAt(int index) const -> const ValueType &;

  /**
   * @brief For test only return a string representing all keys in
   * this leaf page formatted as "(key1,key2,key3,...)"
   *
   * @return The string representation of all keys in the current internal page
   */
  auto ToString() const -> std::string {
    std::string kstr = "(";
    bool first = true;

    for (int i = 0; i < GetSize(); i++) {
      KeyType key = KeyAt(i);
      if (first) {
        first = false;
      } else {
        kstr.append(",");
      }

      kstr.append(std::to_string(key.ToString()));
    }
    kstr.append(")");

    return kstr;
  }

  /**
   * Custom helpers.
   */
  auto HasValidHighKey() const -> bool;
  void SetHasValidHighKey(bool);
  auto GetHighKey() const -> const KeyType &;
  void SetHighKey(const KeyType &key);

  /**
   * 和 InternalNode 不同,
   * LeafNode 的 `high_key_` 是 exclusive upperbound.
   */
  auto CoveredKey(const KeyType &key, KeyComparator &cmp) const -> bool;

  /**
   * 找 upper bound.
   * 在需要新增 KV Pair 的时候寻找 Slot.
   *
   * 如果任何区间都不匹配 `key`,
   * 如, `key >= this->high_key_`,
   * 则返回 -1.
   */
  auto FindIndexUpperbound(const KeyType &key, KeyComparator &cmp) const -> int;

  auto FindIndexLowerbound(const KeyType &key, KeyComparator &cmp) const -> int;

  void NewKvAt(int index);
  void ModifyKvAt(int index, const KeyType &key, const ValueType &val);
  void DeleteKvAt(int index);

  void RedistributeWithRight(WritePageGuard &rpage_g, int l_size, int r_size);

  /**
   * 传统 B Plus Tree 在 LeafNode key[0] 修改时需要修改 parent InternalNode 对应 key,
   * 但此实现不会这么做.
   */
 private:
  /**
   * Sibling leaf page id.
   */
  page_id_t next_page_id_;

  /**
   * 基于 B Link Tree,
   * 需要维护一个 `high_key_` 指明当前 Node 覆盖范围的 upper bound.
   * 分裂时, 其更新为分隔 key 的值,
   * 合并时, 合并后节点更新为较大值.
   *
   * 必须先检查 `high_key_is_valid_`.
   */
  bool high_key_is_valid_;
  KeyType high_key_;

  /**
   * Array members for page data.
   * 此处数组大小基于 1 Page 大小计算.
   */
  KeyType key_array_[LEAF_PAGE_SLOT_CNT];
  ValueType rid_array_[LEAF_PAGE_SLOT_CNT];
  // (Spring 2025) Feel free to add more fields and helper functions below if needed
};

}  // namespace bustub
