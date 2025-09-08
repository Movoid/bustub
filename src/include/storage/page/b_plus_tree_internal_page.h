//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.h
//
// Identification: src/include/storage/page/b_plus_tree_internal_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <queue>
#include <string>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define B_PLUS_TREE_INTERNAL_PAGE_TYPE BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>
#define INTERNAL_PAGE_HEADER_SIZE (sizeof(page_id_t) + sizeof(bool) + sizeof(KeyType))
#define INTERNAL_PAGE_SLOT_CNT                                                 \
  ((BUSTUB_PAGE_SIZE - GENERAL_PAGE_HEADER_SIZE - INTERNAL_PAGE_HEADER_SIZE) / \
   ((int)(sizeof(KeyType) + sizeof(ValueType))))  // NOLINT

/**
 * Store `n` indexed keys and `n + 1` child pointers (page_id) within internal page.
 * Pointer PAGE_ID(i) points to a subtree in which all keys K satisfy:
 * K(i) <= K < K(i+1).
 * NOTE: Since the number of keys does not equal to number of child pointers,
 * the first key in key_array_ always remains invalid. That is to say, any search / lookup
 * should ignore the first key.
 *
 * Internal page format (keys are stored in increasing order):
 *  ---------
 * | HEADER |
 *  ---------
 *  ------------------------------------------
 * | KEY(1)(INVALID) | KEY(2) | ... | KEY(n) |
 *  ------------------------------------------
 *  ---------------------------------------------
 * | PAGE_ID(1) | PAGE_ID(2) | ... | PAGE_ID(n) |
 *  ---------------------------------------------
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTreeInternalPage : public BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreeInternalPage() = delete;
  BPlusTreeInternalPage(const BPlusTreeInternalPage &other) = delete;

  /**
   * 直接调用此函数初始化, 其中封装了对 BPlusTreePage 的初始化.
   */
  void Init(int max_size = INTERNAL_PAGE_SLOT_CNT);

  auto KeyAt(int index) const -> KeyType;

  void SetKeyAt(int index, const KeyType &key);

  /**
   * @param value The value to search for
   * @return The index that corresponds to the specified value
   */
  auto ValueIndex(const ValueType &value) const -> int;

  auto ValueAt(int index) const -> ValueType;

  /**
   * @brief For test only, return a string representing all keys in
   * this internal page, formatted as "(key1,key2,key3,...)"
   *
   * @return The string representation of all keys in the current internal page
   */
  auto ToString() const -> std::string {
    std::string kstr = "(";
    bool first = true;

    // First key of internal page is always invalid
    for (int i = 1; i < GetSize(); i++) {
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

  /**
   * `key[0]` 是 Invalid,
   * 不算入 ValidKeyCnt.
   */
  auto GetValidKeyCnt() -> int;
  auto GetMaxValidKeyCnt() -> int;

  auto HasHighKey() const -> bool;
  auto GetHighKey() const -> const KeyType &;
  void SetHighKey(const KeyType &high_key);

  /**
   * 只有在 `Is{Over, Under}flowedWith` 返回 `false` 时,
   * 才应该采取 Split/Merge.
   */
  auto IsOverflowed() const -> bool;
  auto IsOverflowedWith(ReadPageGuard &rpage_g) -> bool;
  auto IsUnderflowed() const -> bool;
  auto IsUnderflowedWith(ReadPageGuard &rpage_g) -> bool;

  /**
   * 找 upper bound.
   * 在需要新增 KV Pair 的时候寻找 Slot.
   *
   * 如果任何区间都不匹配 `key`,
   * 如, `key >= this->high_key_`,
   * 则返回 -1.
   */
  auto FindIndexUpperbound(const KeyType &key) const -> int;

  /**
   * 找明确 KV Pair.
   * 在需要删除明确的 KV Pair 时用于检查 Slot.
   */
  auto FindIndex(const KeyType &key, const ValueType &val) const -> int;

  void NewKeyAt(int index);
  void ModifyKeyAt(int index, const ValueType &val);
  void DeleteKeyAt(int index);

  auto RedistributeWithRight(WritePageGuard &rpage_g, int l_size, int r_size) -> KeyType;

 private:
  /**
   * Sibling internal page id,
   * B Link Tree 的额外结构.
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
  KeyType key_array_[INTERNAL_PAGE_SLOT_CNT];
  ValueType page_id_array_[INTERNAL_PAGE_SLOT_CNT];
  // (Spring 2025) Feel free to add more fields and helper functions below if needed
};

}  // namespace bustub
