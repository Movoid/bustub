//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.h
//
// Identification: src/include/storage/index/b_plus_tree.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * b_plus_tree.h
 *
 * Implementation of simple b+ tree data structure where internal pages direct
 * the search and leaf pages contain actual data.
 * (1) We only support unique key
 * (2) support insert & remove
 * (3) The structure should shrink and grow dynamically
 * (4) Implement index iterator for range scan
 */

// ============================================================

#pragma once
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "common/config.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

struct PrintableBPlusTree;

/**
 * @brief Definition of the Context class.
 *
 * Hint: This class is designed to help you keep track of the pages
 * that you're modifying or accessing.
 *
 * 此 class 应该是在遍历时存储 PageGuard,
 * 便于调试时观察. 可选用.
 */
class Context {
 public:
  // When you insert into / remove from the B+ tree, store the write guard of header page here.
  // Remember to drop the header page guard and set it to nullopt when you want to unlock all.
  std::optional<WritePageGuard> header_page_{std::nullopt};

  // Save the root page id here so that it's easier to know if the current page is the root page.
  page_id_t root_page_id_{INVALID_PAGE_ID};

  // Store the write guards of the pages that you're modifying here.
  std::deque<WritePageGuard> write_set_;

  // You may want to use this when getting value, but not necessary.
  std::deque<ReadPageGuard> read_set_;

  auto IsRootPage(page_id_t page_id) -> bool { return page_id == root_page_id_; }
};

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator>

/**
 * 并发控制, 采用 B Link Tree 方案.
 *
 * 树高只在 Root Split/Merge 时变化.
 * > 仅 Read 时, 先解锁 Header 再锁 Root.
 * > 若要 Write Root, 先锁 Root 再锁 Header.
 *
 * Split, 节点向右 Split 出新节点.
 * Merge, 节点向右寻找节点进行 Merge.
 * > Tree 最右侧可能出现空节点, 是合法状态.
 * > Merge 后变空的节点作为 Forwarding Node 存在, 指向被 Merge 到的节点.
 * > 所有节点的 `key[0]` 被修改后, 都 **不应该** 修改父节点.
 *
 * 当 Root 只有两个 Child 且可合并, 则需要将这两个 Child Merge 为新的 Root,
 * Root 的类型取决于这两个 Child 的类型.
 * > 最开始的 Root 永远不会被 destroy,
 * > 其会随着树高增长, 始终为最左下的节点.
 *
 * B Link Tree 中,
 * Split 和 Merge 操作, 无论是 Internal 还是 Leaf,
 * 都需要更新 Sibling Link.
 *
 *
 * Split 细节:
 * node1 向右创建新节点 node2, 将 node1 后半部分移动到 node2.
 * 下阶段将 node2 对应 key 插入 parent.
 *
 * Merge 细节:
 * node1 通过 Sibling Link 找到 node2, 若不存在则结束操作.
 * 将 node2 合并入 node1 后半部分.
 * 下阶段在 parent 删除 node2 对应 key.
 *
 * > 教科书 B Plus Tree 会采用节点均分方法平衡节点,
 * > 此处 B Link Tree 实现无此流程.
 * > 这会导致在将右侧节点 kv pairs 移动到左侧节点时,
 * > 正在沿着 Sibling Link 从左向右遍历的线程,
 * > 在没有锁的时候, 错失对这些移动 kv pairs 的访问.
 *
 * 对 Root 的操作细节:
 * 如果 Root 为 InternalNode, 且 `GetSize()` 返回 `1` 则说明 Root 只有一个 Child Node.
 * 应在持有 Header + Root Latch 时将 Root page id 修改为此 Child,
 * 此过程不需要锁 Child.
 *
 * 关于对 parent 的查找和插入 key:
 * 插入 key, 如果当前 Node covered, 则可以插入.
 * 遍历时查找, 优先找 lowerbound 的 kv pair.
 *
 * 如果 Root Overflowed, 无论节点类型,
 * 应在持有 Header + Root Latch 时修改 Root page id 为新的 Root.
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTree {
  /**
   * 注意此处, InternalPage 存储 <KeyType, page_id_t> pair,
   * 其 page_id_t 用于指引遍历.
   */
  using InternalPageValueType = page_id_t;
  using InternalPage = BPlusTreeInternalPage<KeyType, InternalPageValueType, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  using PathContainer = meow_utils::SmallVector<page_id_t, 8>;

  struct IndexKvPackImpl {
    KeyType key_;
    page_id_t val_;
    bool update_lowerlevel_head_{};  // Used in level head update.
    page_id_t new_lowerlevel_head_id_;
  };

  using IndexKvPack = std::optional<IndexKvPackImpl>;

 public:
  /**
   * BPlusTree 构造函数.
   * 其中 `leaf_max_size` 和 `internal_max_size` 直接用于空间分配大小.
   * `BPlusTreeInternalPage` 中第一个 `key` 是 invalid 的, 仍算入 `size` 中.
   */
  explicit BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                     const KeyComparator &comparator, int leaf_max_size = LeafPage::LEAF_PAGE_SLOT_CNT_MAX,
                     int internal_max_size = InternalPage::INTERNAL_PAGE_SLOT_CNT_MAX);

  ~BPlusTree();

  // Returns true if this B+ tree has no keys and values.
  auto IsEmpty() const -> bool;

  // Insert a key-value pair into this B+ tree.
  auto Insert(const KeyType &key, const ValueType &value) -> bool;

  // Remove a key and its value from this B+ tree.
  void Remove(const KeyType &key);

  // Return the value associated with a given key
  auto GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool;

  // Return the page id of the root node
  auto GetRootPageId() const -> page_id_t;

  // Index iterator
  auto Begin() -> INDEXITERATOR_TYPE;

  auto End() -> INDEXITERATOR_TYPE;

  auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;

  void Print(BufferPoolManager *bpm);

  void Draw(BufferPoolManager *bpm, const std::filesystem::path &outf);

  auto DrawBPlusTree() -> std::string;

  // read data from file and insert one by one
  void InsertFromFile(const std::filesystem::path &file_name);

  // read data from file and remove one by one
  void RemoveFromFile(const std::filesystem::path &file_name);

  void BatchOpsFromFile(const std::filesystem::path &file_name);

 private:
  // auto GoByLink(const KeyType &key, ReadPageGuard &&page_g) -> ReadPageGuard;
  // auto GoByLink(const KeyType &key, WritePageGuard &&page_g) -> WritePageGuard;

  [[nodiscard]] auto GoSibUntilCoverRR(ReadPageGuard &page_g, const KeyType &key) -> bool;
  [[nodiscard]] auto GoSibUntilCoverWW(WritePageGuard &page_g, const KeyType &key) -> bool;
  [[deprecated("Do not use.")]] auto GoSibUntilCoverRW(ReadPageGuard &&page_g, WritePageGuard &page_g_w,
                                                       const KeyType &key) -> bool;

  [[nodiscard]] auto GoSibUntilHeadRR(ReadPageGuard &page_g) -> bool;
  [[nodiscard]] auto GoSibUntilHeadWW(WritePageGuard &page_g) -> bool;

  [[nodiscard]] auto GoDownUntilLevel(ReadPageGuard &page_g, const KeyType &key, int to_level, PathContainer &path)
      -> bool;

  [[nodiscard]] auto GoDownUntilLeafHead(ReadPageGuard &page_g) -> bool;

  [[nodiscard]] auto TryGetPathTo(const KeyType &key, int to_level, PathContainer &path) -> bool;

  void CleanTombstone(WritePageGuard &page_g);

  auto TrySplitAndGo(WritePageGuard &page_g, const KeyType &key, IndexKvPack &backfix_kv) -> bool;
  auto TryMergeAndGo(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool;

  auto TryExpandLevel(page_id_t &new_root_id) -> bool;
  auto TryShrinkLevel(page_id_t &new_root_id) -> bool;

  auto BackfixInsertKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool;

  auto BackfixRemoveKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool;

 private:
  void ToGraph(page_id_t page_id, const BPlusTreePage *page, std::ofstream &out);

  void PrintTree(page_id_t page_id, const BPlusTreePage *page);

  auto ToPrintableBPlusTree(page_id_t root_id) -> PrintableBPlusTree;

  // member variable
  std::string index_name_;
  BufferPoolManager *bpm_;
  KeyComparator comparator_;
  std::vector<std::string> log;  // NOLINT
  int leaf_max_size_;
  int internal_max_size_;

  /**
   * `header_page_id_` 记录了 BPlusTree root 的 page id.
   * 每次遍历应该从此 page 读取 root page id.
   *
   * 为 root id 单独使用一个 page 是必要的,
   * 因为需要支持多线程并发修改 root.
   */
  page_id_t header_page_id_;

  /**
   * 神秘小工具
   */
  simple_cu::qsbr::QSBRManager<32, page_id_t, BufferPoolManager::PageDeleter> qsbr_mgr_{{bpm_}};

  static constexpr int LEAF_LEVEL{1};
};

/**
 * @brief for test only. PrintableBPlusTree is a printable B+ tree.
 * We first convert B+ tree into a printable B+ tree and the print it.
 */
struct PrintableBPlusTree {
  int size_;
  std::string keys_;
  std::vector<PrintableBPlusTree> children_;

  /**
   * @brief BFS traverse a printable B+ tree and print it into
   * into out_buf
   *
   * @param out_buf
   */
  void Print(std::ostream &out_buf) {
    std::vector<PrintableBPlusTree *> que = {this};
    while (!que.empty()) {
      std::vector<PrintableBPlusTree *> new_que;

      for (auto &t : que) {
        int padding = (t->size_ - t->keys_.size()) / 2;
        out_buf << std::string(padding, ' ');
        out_buf << t->keys_;
        out_buf << std::string(padding, ' ');

        for (auto &c : t->children_) {
          new_que.push_back(&c);
        }
      }
      out_buf << "\n";
      que = new_que;
    }
  }
};

}  // namespace bustub

/**
  问题日志.
  每个 Leaf/Internal Node 的 key[0] 应该记录了当前 Node 在 parent 中的 key.
  但 Root 开始时为空. 这可能需要特殊处理.

  现在缺少对 Root 的 Split/Merge 处理,

  如果 Root 所在的 Layer 有多个节点，且其中非 Root 节点发生 Split/Merge,
  这貌似 **也应该** 提升树高，且必须把原 Root id 加入到新 Root 进行索引。
  - Status: Solved. Root Layer 发生分裂. 创建新 Root.

  问题日志.
  考虑按倒序将所有 kv 插入 Tree.
  由于 Invariant: Key[0] 禁止修改, 否则会导致前一个节点的 SibLink 被破坏.
  我们必须对最左侧的 Internal 节点 Key[0] 引入特殊状态.
  使用 `has_valid_lowkey_`, 若为 `false`, 则此 `Key[0]` 相当于无穷小.
  - Status: Solved. 对 Internal 节点引入 lowkey 机制.

  问题日志.
  Leaf Node 的 kv[0] 删除机制存在缺陷.
  必须给 Leaf Node 添加一个持久的 lowkey, 类似 Internal Node.
  以及, 从左向右的 Merge 永远不被允许, 这会破坏 Sibling 的 lowkey.
  同理, 从右向左的 Split 也不被允许, 同样破坏当前节点的 lowkey.
  - Statue: Solved. 对 Leaf 节点引入 lowkey 机制.

  问题日志.
  在当前的 LINK Protocol 中, `linked_parent_id_` 带来了问题.
  若 page x 的 `linked_parent_id_` page 中的 kv 被删除,
  而导致此 parent page 被 Merge 而 QSBR Retire,
  此时 ReclaimLocal 回收这个 parent page 是合理的,
  因为对 page x 的 `Remove` 可能还未启动, 没有进入 QSBR 临界区.

  这疑似是当前 QSBR 算法的错误, 严格按照 Quetient State,
  `Remove` 未启动, Epoch 没有变化, 则 ReclaimLocal 不应该回收.

  分析后结论, 并不是 QSBR 算法问题,
  而是 LINK Protocol 的 parent id 生命周期脱离了控制.
  必须废除 `linked_parent_id_` 机制, 采取类似 `Insert` 的 path 机制.
  - Status: In progress.

*/
