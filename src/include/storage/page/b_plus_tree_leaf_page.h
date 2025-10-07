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
 private:
  struct KvPair {
    KeyType x_;
    ValueType y_;
  };
  struct LeafPageHeader {
    /**
     * 分裂时, 设置为 Sibling 的 Key[0],
     * 用于在 Level 内沿着 Sibling Link 搜索 key.
     *
     * `highkey_` 不保证严格正确.
     * 若 key > `highkey_`, 则 key 不属于当前 Node.
     *
     * 只有在 `next_page_id_ != INVALID_PAGE_ID` 时才有效.
     */
    KeyType highkey_;

    /**
     * 分裂时, 新 Node 的 `lowkey_` 被设置为 Key[0].
     * `lowkey_` 用于在 parent 中路由到当前节点.
     *
     * 指示若 key >= `lowkey_` 则 key 一定位于此节点及之后.
     * > Root Leaf 在第一次 `Insert` 必须手动初始化 `lowkey_`.
     */
    KeyType lowkey_;
  } hdr_;

  constexpr static int LEAF_PAGE_HEADER_SIZE{sizeof(LeafPageHeader)};
  constexpr static int LEAF_PAGE_SLOT_CNT{(BUSTUB_PAGE_SIZE - GENERAL_PAGE_HEADER_SIZE - LEAF_PAGE_HEADER_SIZE) /
                                          sizeof(KvPair)};

  /**
   * Array members for page data.
   * 此处数组大小基于 1 Page 大小计算.
   */
  KeyType key_array_[LEAF_PAGE_SLOT_CNT];
  ValueType rid_array_[LEAF_PAGE_SLOT_CNT];
  // (Spring 2025) Feel free to add more fields and helper functions below if needed
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreeLeafPage() = delete;
  BPlusTreeLeafPage(const BPlusTreeLeafPage &other) = delete;

  constexpr static int LEAF_PAGE_SLOT_CNT_MAX{LEAF_PAGE_SLOT_CNT};

  /**
   * 直接调用此函数初始化, 其中封装了对 BPlusTreePage 的初始化.
   */
  void Init(int max_size = LEAF_PAGE_SLOT_CNT_MAX);

  // Helper methods
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
  auto GetHighKey() const -> const KeyType &;
  void SetHighKey(const KeyType &high_key);

  auto GetLowKey() const -> const KeyType &;
  void SetLowKey(const KeyType &low_key);

  /**
   * 和 InternalNode 不同,
   * LeafNode 的 `high_key_` 是 exclusive upperbound.
   *
   * 必须跳过空节点, 不允许复用空节点.
   * Level 内的空节点最后会被 Level head node 全部 Merge.
   */
  auto CoveredKey(const KeyType &key, KeyComparator &cmp) const -> bool;

  auto FindIndexUpperbound(const KeyType &key, KeyComparator &cmp) const -> int;

  auto FindIndexLowerbound(const KeyType &key, KeyComparator &cmp) const -> int;

  void NewKvAt(int index);
  void ModifyKvAt(int index, const KeyType &key, const ValueType &val);
  void DeleteKvAt(int index);

  void RedistributeWithRight(WritePageGuard &rpage_g_w, int l_size, int r_size);

  /**
   * 传统 B Plus Tree 在 LeafNode key[0] 修改时需要修改 parent InternalNode 对应 key,
   * 但此实现不会这么做.
   */
};

#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>

}  // namespace bustub
