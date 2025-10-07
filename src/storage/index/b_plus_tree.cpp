//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {
/**
 * 构造函数
 * 此处应该 **提前构造 root 节点** 更加方便.
 * 若 root 为空, 当作叶子节点进行操作.
 * root 节点只能在析构时被释放.
 */
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
  auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
  tree_header_data->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * 析构函数.
 */
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::~BPlusTree() { BUSTUB_ASSERT(qsbr_mgr_.GetRetiredCntGlobal() == 0, "Unexpected."); }

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 *
 * 先必须检查 page id 是否合法.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  if (header_page_id_ == INVALID_PAGE_ID) {
    return true;
  }
  // LC: Lock parent.
  auto header_page_g{bpm_->ReadPage(header_page_id_)};
  auto header_data{header_page_g.As<BPlusTreeHeaderPage>()};
  if (header_data->root_page_id_ == INVALID_PAGE_ID) {
    return true;
  }
  // LC: Lock child.
  auto root_page_g{bpm_->ReadPage(header_data->root_page_id_)};
  // LC: Unlock parent.
  header_page_g.Drop();

  if (root_page_g.As<BPlusTreePage>()->IsLeafPage()) {
    return root_page_g.As<LeafPage>()->GetSize() == 0;
  }
  // Root is InternalPage so the tree cannot be empty.
  BUSTUB_ASSERT(root_page_g.As<InternalPage>()->GetSize() > 0, "Broken root.");
  return false;
  // LC: Unlock child.
  // root_page_g.Drop();
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  qsbr_mgr_.EnterCriticalZone();
  page_id_t cur_page_id{GetRootPageId()};
  bool is_found{};

  if (cur_page_id == INVALID_PAGE_ID) {
    return false;
  }

  while (true) {
    auto page_g{bpm_->ReadPage(cur_page_id)};
    GoSibUntilCoverRR(page_g, key);

    if (page_g.As<BPlusTreePage>()->IsLeafPage()) {
      // page_g = GoSibUntilCoverRR(key, std::move(page_g));
      // 目标 kv 最多只会出现在一个节点, 沿着 SibLink walk 到 Covered 节点即可.
      auto page_data{page_g.As<LeafPage>()};
      // PrintTree(page_g.GetPageId(), page_data);
      int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
      if (lowerbound_idx == page_data->GetSize() || comparator_(page_data->KeyAt(lowerbound_idx), key) != 0) {
        is_found = false;
        break;
      }
      is_found = true;
      result->emplace_back(page_data->ValueAt(lowerbound_idx));
      break;
    }
    // Is internal node.
    auto page_data{page_g.As<InternalPage>()};
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
    BUSTUB_ASSERT(upperbound_idx >= 0, "Invariant broken.");
    cur_page_id = page_data->ValueAt(std::max(0, upperbound_idx - 1));
  }

  qsbr_mgr_.ExitCriticalZone();
  if (qsbr_mgr_.GetRetiredCntLocal() > 0) {
    qsbr_mgr_.ReclaimLocal();
  }
  return is_found;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  qsbr_mgr_.EnterCriticalZone();  // Remember to exit!
  page_id_t cur_root_id{GetRootPageId()};

  if (cur_root_id == INVALID_PAGE_ID) {
    TryNewRoot(cur_root_id, 1);
    BUSTUB_ASSERT(cur_root_id != INVALID_PAGE_ID, "Unexpected.");
  }

  PathContainer backfix_path{};
  IndexKvPack backfix_kv{std::nullopt};

  // Locate.
  {
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
    auto page_header_data{page_g.As<BPlusTreePage>()};
    while (!page_header_data->IsLeafPage()) {
      GoSibUntilCoverRR(page_g, key);
      // Is internal node. Go down.
      backfix_path.EmplaceBack(page_g.GetPageId());
      auto page_data{page_g.As<InternalPage>()};
      int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
      // BUSTUB_ASSERT(upperbound_idx >= 0, "Invariant broken.");
      page_id_t next_id{page_data->ValueAt(std::max(0, upperbound_idx - 1))};
      page_g.Drop();
      page_g = bpm_->ReadPage(next_id);
      page_header_data = page_g.As<BPlusTreePage>();
    }

    WritePageGuard leaf_g_w{GoSibUntilCoverRW(std::move(page_g), key)};
    page_id_t last_leaf_id{leaf_g_w.GetPageId()};
    // Split?
    while (TrySplitAndGo(leaf_g_w, key, backfix_kv)) {
      last_leaf_id = leaf_g_w.GetPageId();
      leaf_g_w.Drop();  // Drop here.

      // Backfix.
      PathContainer cur_backfix_path{backfix_path};

      int next_level{2};
      while (backfix_kv.has_value()) {
        auto &[backfix_key, backfix_val]{backfix_kv.value()};
        if (cur_backfix_path.Empty()) {
          if (TryNewRoot(cur_root_id, next_level)) {
            cur_backfix_path.EmplaceBack(cur_root_id);
            continue;
          }
          cur_backfix_path.Clear();
          GetPathTo(next_level, backfix_key, cur_backfix_path);
          if (cur_backfix_path.Empty()) {
            // As protocol, task is cancelled with `DONT_LINK`.
            break;
          }
          cur_root_id = cur_backfix_path.Front();
          continue;
        }
        {
          WritePageGuard page_g_w{bpm_->WritePage(cur_backfix_path.Back())};
          GoSibUntilCoverWW(page_g_w, backfix_key);
          auto page_header_data{page_g_w.As<BPlusTreePage>()};
          BUSTUB_ASSERT(!page_header_data->IsLeafPage(), "Unexpected.");
          BUSTUB_ASSERT(page_header_data->GetLevel() == next_level, "Unexpected.");
          auto page_data{page_g_w.AsMut<InternalPage>()};
          if (!page_data->CoveredKey(backfix_key, comparator_)) {
            // As protocol, this is level is deleted.
            // The backfix task should be `DONT_LINK`.
            break;
          }
          BackfixInsertKv(page_g_w, backfix_kv);
        }
        next_level++;
        cur_backfix_path.PopBack();
      }
      leaf_g_w = bpm_->WritePage(last_leaf_id);  // Go back to leaf.
      GoSibUntilCoverWW(leaf_g_w, key);
      last_leaf_id = leaf_g_w.GetPageId();  // Record.
    }
    auto page_data{leaf_g_w.AsMut<LeafPage>()};
    BUSTUB_ASSERT(!page_data->IsOverflowed(), "Unexpected.");
    // Insert.
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};

    // This leaf may have no kv.
    // BUSTUB_ASSERT(!page_data->HasValidLowKey() || comparator_(page_data->GetLowKey(), key) <= 0, "Invariant
    // broken.");
    BUSTUB_ASSERT(upperbound_idx > 0 || page_data->GetSize() == 0 || comparator_(key, page_data->KeyAt(0)) < 0,
                  "Invariant broken.");

    if (upperbound_idx > 0 && comparator_(page_data->KeyAt(upperbound_idx - 1), key) == 0) {
      qsbr_mgr_.ExitCriticalZone();
      return false;
    }
    page_data->NewKvAt(upperbound_idx);
    page_data->ModifyKvAt(upperbound_idx, key, value);
  }

  qsbr_mgr_.ExitCriticalZone();
  if (qsbr_mgr_.GetRetiredCntLocal() > 0) {
    qsbr_mgr_.ReclaimLocal();
  }
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  qsbr_mgr_.EnterCriticalZone();
  page_id_t cur_root_id{GetRootPageId()};

  if (cur_root_id == INVALID_PAGE_ID) {
    qsbr_mgr_.ExitCriticalZone();
    return;
  }

  PathContainer backfix_path{};
  IndexKvPack backfix_kv{std::nullopt};
  bool try_delete_root_hint{};

  {
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
    while (!page_g.As<BPlusTreePage>()->IsLeafPage()) {
      GoSibUntilCoverRR(page_g, key);
      backfix_path.EmplaceBack(page_g.GetPageId());
      auto page_data{page_g.As<InternalPage>()};
      int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
      // BUSTUB_ASSERT(upperbound_idx > 0, "Invariant broken.");
      page_id_t next_id{page_data->ValueAt(std::max(0, upperbound_idx - 1))};
      page_g.Drop();
      page_g = bpm_->ReadPage(next_id);
    }
    WritePageGuard leaf_g_w{GoSibUntilCoverRW(std::move(page_g), key)};
    auto page_data{leaf_g_w.AsMut<LeafPage>()};
    int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
    if (lowerbound_idx == page_data->GetSize() || comparator_(page_data->KeyAt(lowerbound_idx), key) != 0) {
      qsbr_mgr_.ExitCriticalZone();
      return;
    }
    page_data->DeleteKvAt(lowerbound_idx);
    // Merge one time is **NOT** enough. Todo.

    try_delete_root_hint =
        page_data->GetSize() == 0 && page_data->IsLevelHead() && page_data->GetNextPageId() == INVALID_PAGE_ID;

    page_id_t last_leaf_id{leaf_g_w.GetPageId()};
    while (TryMergeAndGo(leaf_g_w, backfix_kv)) {
      last_leaf_id = leaf_g_w.GetPageId();
      leaf_g_w.Drop();
      PathContainer cur_backfix_path{backfix_path};
      int next_level{2};
      while (backfix_kv.has_value()) {
        auto &[backfix_key, backfix_val]{backfix_kv.value()};
        if (cur_backfix_path.Empty()) {
          GetPathTo(next_level, backfix_key, cur_backfix_path);
          BUSTUB_ASSERT(!cur_backfix_path.Empty(), "Unexpected.");
        }
        {
          WritePageGuard page_g{bpm_->WritePage(cur_backfix_path.Back())};
          BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
          GoSibUntilCoverWW(page_g, backfix_key);
          BUSTUB_ASSERT(page_g.As<InternalPage>()->CoveredKey(backfix_key, comparator_), "Unexpected.");
          BackfixRemoveKv(page_g, backfix_kv);
          auto page_data{page_g.As<InternalPage>()};
          // todo
          try_delete_root_hint =
              page_data->GetSize() == 0 && page_data->IsLevelHead() && page_data->GetNextPageId() == INVALID_PAGE_ID;
        }
        next_level++;
        cur_backfix_path.PopBack();
      }
      leaf_g_w = bpm_->WritePage(last_leaf_id);
      GoSibUntilCoverWW(leaf_g_w, key);
      last_leaf_id = leaf_g_w.GetPageId();
    }
  }

  if (try_delete_root_hint) {
    while (cur_root_id != INVALID_PAGE_ID && TryDeleteRoot(cur_root_id)) {
      ;
    }
  }

  qsbr_mgr_.ExitCriticalZone();
  if (qsbr_mgr_.GetRetiredCntLocal() > 0) {
    qsbr_mgr_.ReclaimLocal();
  }
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  page_id_t cur_root_id{GetRootPageId()};
  if (cur_root_id == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE{std::nullopt, bpm_};
  }
  ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
  while (!page_g.As<BPlusTreePage>()->IsLeafPage()) {
    auto page_data{page_g.As<InternalPage>()};
    page_id_t next_id{page_data->ValueAt(0)};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
  }
  BUSTUB_ASSERT(page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");

  auto page_data{page_g.As<LeafPage>()};
  while (page_data->GetSize() == 0 && page_data->GetNextPageId() != INVALID_PAGE_ID) {
    page_id_t sib_id{page_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->ReadPage(sib_id);
    page_data = page_g.As<LeafPage>();
  }

  if (page_data->GetSize() == 0) {
    return INDEXITERATOR_TYPE{std::nullopt, bpm_};
  }
  return INDEXITERATOR_TYPE{std::move(page_g), 0, bpm_};
}

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  page_id_t cur_root_id{GetRootPageId()};
  if (cur_root_id == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE{std::nullopt, bpm_};
  }
  ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
  GoSibUntilCoverRR(page_g, key);
  while (!page_g.As<BPlusTreePage>()->IsLeafPage()) {
    // Is internal node. Go down.
    auto page_data{page_g.As<InternalPage>()};
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
    page_id_t next_id{page_data->ValueAt(std::max(0, upperbound_idx - 1))};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
    GoSibUntilCoverRR(page_g, key);
  }
  BUSTUB_ASSERT(page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
  auto page_data{page_g.As<LeafPage>()};
  int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
  BUSTUB_ASSERT(lowerbound_idx < page_data->GetSize(), "Invalid args.");
  BUSTUB_ASSERT(comparator_(page_data->KeyAt(lowerbound_idx), key) == 0, "Invalid args.");
  return INDEXITERATOR_TYPE{std::move(page_g), lowerbound_idx, bpm_};
}

/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE{std::nullopt, bpm_}; }

/**
 * @return Page id of the root of this tree
 *
 * You may want to implement this while implementing Task #3.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  BUSTUB_ASSERT(header_page_id_ != INVALID_PAGE_ID, "Unexpected.");
  auto root_data{bpm_->ReadPage(header_page_id_).As<BPlusTreeHeaderPage>()};
  return root_data->root_page_id_;
}

/**
 * 辅助函数.
 * 在走到最右侧 Sibling 时必须停止,
 * 最右侧的 Sibling 其 `high_key_` 应该表示 +inf.
 *
 * 使用 `std::move` 来赋值 `page_g` 实际是 Lock Coupling,
 * 后续尝试修改为 Optimistic.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverRR(ReadPageGuard &page_g, const KeyType &key) -> bool {
  bool ret{};
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {
    while (!page_g.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<LeafPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g.Drop();
      page_g = bpm_->ReadPage(next_page_id);
      ret = true;
    }
  } else {
    while (!page_g.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<InternalPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g.Drop();
      page_g = bpm_->ReadPage(next_page_id);
      ret = true;
    }
  }
  return ret;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverWW(WritePageGuard &page_g, const KeyType &key) -> bool {
  bool ret{};
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {
    while (!page_g.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<LeafPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g.Drop();
      page_g = bpm_->WritePage(next_page_id);
      ret = true;
    }
  } else {
    while (!page_g.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<InternalPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g.Drop();
      page_g = bpm_->WritePage(next_page_id);
      ret = true;
    }
  }
  return ret;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverRW(ReadPageGuard &&page_g, const KeyType &key) -> WritePageGuard {
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {  // leaf
    while (!page_g.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<LeafPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g.Drop();
      page_g = bpm_->ReadPage(next_page_id);
    }
  } else {  // internal
    while (!page_g.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<InternalPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g.Drop();
      page_g = bpm_->ReadPage(next_page_id);
    }
  }

  page_id_t cur_page_id{page_g.GetPageId()};
  page_g.Drop();

  WritePageGuard page_g_w{bpm_->WritePage(cur_page_id)};
  if (page_g_w.As<BPlusTreePage>()->IsLeafPage()) {  // leaf
    while (!page_g_w.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g_w.As<LeafPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g_w.Drop();
      page_g_w = bpm_->WritePage(next_page_id);
    }
  } else {  // internal
    while (!page_g_w.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g_w.As<InternalPage>()};
      page_id_t next_page_id{page_data->GetNextPageId()};
      if (next_page_id == INVALID_PAGE_ID) {
        break;
      }
      page_g_w.Drop();
      page_g_w = bpm_->WritePage(next_page_id);
    }
  }

  return page_g_w;
}

/**
 * 存在缺陷.
 *
 * 被 backfix 的 Level, 不需要也不应该再次 backfix,
 * 如果没有对称的 Merge, 其 backfix kv 不会消失.
 * 所以新的 backfix **必须** 发生在比上次 backfix 更高的 level.
 *
 * 以及, 每次 backfix 应该发生在对应 `backfix_page_id` 的直接 parent level.
 * 可问题在于, 如果这个直接 parent level 已经消失了呢?
 *
 * 可能需要修改机制, 比如在 Page 中标记是否已经完成了 backfix insert,
 * 或开启了 backfix erase.
 *
 * 对每个 node 都引入 NOT_LINKED_IN_PARENT, IS_LINKED_IN_PARENT, DONT_LINK_IN_PARENT 三种状态.
 * 每个新的节点初始化为 NOT_LINKED,
 * 若 Split 后因为需要 backfix insert, 则继续沿用 `backfix_path` 机制回溯.
 * 若某时刻发生 Merge, 若 NOT_LINKED 则 DONT_LINK 并结束. 否则只能回溯.
 * 在真正需要 insert backfix 的 Node, 再次检查, 若 DONT_LINK 则直接返回,
 * 否则 NOT_LINKED, 需要转变为 IS_LINKED.
 * 在真正需要 erase backfix 的 Node, 再次检查, 此时必定是 IS_LINKED, 执行删除.
 *
 * 若 `GetPathTo` 返回的 path level 和先前不一致,
 * 则必须检查对应 node 是否变为了 DONT_LINK.
 * 按照逻辑, 此时一定是 DONT_LINK.
 *
 * 这意味着, XorKv 机制必须被废除,
 * 因为其不能真正解决问题.
 *
 * 关于状态的修改, 必须发生在 Split/Merge 发生的同时.
 * 若 node 非 DONT_LINK, 则其没有发生 Merge, 其 parent level 存在对应 kv.
 * 此 kv 没有被删除, 如果在 Root 中, 则阻止了树高减小.
 * 这保证在 NOT_LINKED 时, backfix 一定能成功,
 * 若 parent level 消失, 则当前一定是 DONT_LINK.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::GetPathTo(int to_level, const KeyType &key, PathContainer &path) {
  BUSTUB_ASSERT(to_level > 1, "Invalid args.");
  BUSTUB_ASSERT(path.Empty(), "Invalid args.");
  page_id_t cur_root_id{GetRootPageId()};
  if (cur_root_id == INVALID_PAGE_ID) {
    return;
  }
  ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
  GoSibUntilCoverRR(page_g, key);
  while (page_g.As<BPlusTreePage>()->GetLevel() >= to_level) {
    BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
    auto page_data{page_g.As<InternalPage>()};
    path.EmplaceBack(page_g.GetPageId());
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
    page_id_t next_id{page_data->ValueAt(std::max(0, upperbound_idx - 1))};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
    GoSibUntilCoverRR(page_g, key);
  }
}

/**
 * 需要处理需要 New Root 的情况.
 * 乐观策略, 先创建新 Node, 再判断 Root page id 是否未变.
 * 若未变, 正确更新 Root,
 * 否则把 backfix_kv 返回, 让上层重新遍历找到新的 Root.
 *
 * Update: 这应该拆分出另一个函数 `TryNewRoot`,
 * 只负责尝试新建 Root, 不负责任何 backfix.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TrySplitAndGo(WritePageGuard &page_g, const KeyType &key, IndexKvPack &backfix_kv) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  if (!page_header_data->IsOverflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }

  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    {
      WritePageGuard new_sib_g_w{bpm_->WritePage(bpm_->NewPage())};
      auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
      new_sib_data->Init();
      new_sib_data->SetMaxSize(page_data->GetMaxSize());
      int kv_sum{page_data->GetSize() + new_sib_data->GetSize()};
      page_data->RedistributeWithRight(new_sib_g_w, (kv_sum + 1) / 2, kv_sum / 2);
      new_sib_data->SetNextPageId(page_data->GetNextPageId());
      page_data->SetNextPageId(new_sib_g_w.GetPageId());
      new_sib_data->SetLevel(page_data->GetLevel());
      backfix_kv = {new_sib_data->GetLowKey(), new_sib_g_w.GetPageId()};
      new_sib_data->SetBackfixStatus(BackfixStatus::NEED_LINK);
      // 注意 CoveredKey 根据 highkey 判断,
      // 此处不能因为 Sibling covered 而 goto sibling.
      if (!page_data->CoveredKey(key, comparator_)) {
        page_g = std::move(new_sib_g_w);
      }
    }
  } else {
    auto page_data{page_g.AsMut<InternalPage>()};
    {
      WritePageGuard new_sib_g_w{bpm_->WritePage(bpm_->NewPage())};
      auto new_sib_data{new_sib_g_w.AsMut<InternalPage>()};
      new_sib_data->Init();
      new_sib_data->SetMaxSize(page_data->GetMaxSize());
      int kv_sum{page_data->GetSize() + new_sib_data->GetSize()};
      page_data->RedistributeWithRight(new_sib_g_w, (kv_sum + 1) / 2, kv_sum / 2);
      new_sib_data->SetNextPageId(page_data->GetNextPageId());
      page_data->SetNextPageId(new_sib_g_w.GetPageId());
      new_sib_data->SetLevel(page_data->GetLevel());
      backfix_kv = {new_sib_data->GetLowKey(), new_sib_g_w.GetPageId()};
      new_sib_data->SetBackfixStatus(BackfixStatus::NEED_LINK);
      if (!page_data->CoveredKey(key, comparator_)) {
        page_g = std::move(new_sib_g_w);
      }
    }
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryMergeAndGo(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};

  if (!page_header_data->IsUnderflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }

  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    // There is no sibling.
    if (page_data->GetNextPageId() == INVALID_PAGE_ID) {
      backfix_kv = std::nullopt;
      return false;
    }

    {
      WritePageGuard sib_g_w{bpm_->WritePage(page_data->GetNextPageId())};
      auto sib_data{sib_g_w.AsMut<LeafPage>()};
      if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
        backfix_kv = std::nullopt;
        return false;
      }
      int kv_sum{page_data->GetSize() + sib_data->GetSize()};

      // 若 Sibling 是 IS_UNLINKED/NEED_LINK 状态,
      // 将 `is_level_head_` 的 page_data 数据 Merge 到 Sibling,
      // 将导致这部分数据失去路由.

      // 解决方法是, 如果 Sibling 是 IS_UNLINKED/NEED_LINK 则将其 Merge 到左侧.
      // 否则将 page_data Merge 到右侧.

      // 只有 Sibling 为 IS_LINKED/NEED_LINK 状态才需要向右 Merge.

      /**
       * WARNING: Merge 方向不同, Protocol 修改的节点不同.
       */

      auto sib_status{sib_data->GetBackfixStatus()};
      if (sib_status == BackfixStatus::NEED_LINK || sib_status == BackfixStatus::IS_UNLINKED) {
        // Currently not linked.
        // Merge to left.
        page_data->RedistributeWithRight(sib_g_w, kv_sum, 0);
        page_data->SetNextPageId(sib_data->GetNextPageId());
        sib_data->SetNextPageId(page_g.GetPageId());

        sib_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
        qsbr_mgr_.Retire(sib_g_w.GetPageId());  // Retire.
        backfix_kv = std::nullopt;
        return false;
      } else if (sib_status == BackfixStatus::NEED_UNLINK) {  // NEED_UNLINK // NOLINT
        BUSTUB_ASSERT(sib_data->GetSize() == 0, "Unexpected.");
        page_data->SetNextPageId(sib_data->GetNextPageId());
        qsbr_mgr_.Retire(sib_g_w.GetPageId());
        backfix_kv = std::nullopt;
        return false;
      } else if (sib_status == BackfixStatus::IS_LINKED) {
        // Check if is level head.
        page_data->RedistributeWithRight(sib_g_w, 0, kv_sum);
        if (page_data->IsLevelHead()) {
          qsbr_mgr_.Retire(page_g.GetPageId());
          sib_data->SetIsLevelHead(true);
        }

        auto page_status{page_data->GetBackfixStatus()};

        if (page_status == BackfixStatus::IS_LINKED) {
          page_data->SetBackfixStatus(BackfixStatus::NEED_UNLINK);
          backfix_kv = {page_data->GetLowKey(), page_g.GetPageId()};
          page_g = std::move(sib_g_w);
          return true;
        } else if (page_status == BackfixStatus::NEED_LINK) {  // NOLINT
          page_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
          backfix_kv = std::nullopt;
          page_g = std::move(sib_g_w);
          return false;
        }

        // `page_status` 不可能是 NEED_UNLINK/IS_UNLINKED, 因为此时 `page_data` 应该为空.
        BUSTUB_ASSERT(false, "Protocol error.");
      }
    }

  } else {
    auto page_data{page_g.AsMut<InternalPage>()};
    if (page_data->GetNextPageId() == INVALID_PAGE_ID) {
      backfix_kv = std::nullopt;
      return false;
    }

    {
      WritePageGuard sib_g_w{bpm_->WritePage(page_data->GetNextPageId())};
      auto sib_data{sib_g_w.AsMut<InternalPage>()};
      if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
        backfix_kv = std::nullopt;
        return false;
      }
      int kv_sum{page_data->GetSize() + sib_data->GetSize()};

      auto sib_status{sib_data->GetBackfixStatus()};
      if (sib_status == BackfixStatus::NEED_LINK || sib_status == BackfixStatus::IS_UNLINKED) {
        // Currently not linked.
        // Merge to left.
        page_data->RedistributeWithRight(sib_g_w, kv_sum, 0);
        page_data->SetNextPageId(sib_data->GetNextPageId());
        sib_data->SetNextPageId(page_g.GetPageId());

        sib_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
        qsbr_mgr_.Retire(sib_g_w.GetPageId());  // Retire.
        backfix_kv = std::nullopt;
        return false;
      } else if (sib_status == BackfixStatus::NEED_UNLINK) {  // NEED_UNLINK // NOLINT
        BUSTUB_ASSERT(sib_data->GetSize() == 0, "Unexpected.");
        page_data->SetNextPageId(sib_data->GetNextPageId());
        qsbr_mgr_.Retire(sib_g_w.GetPageId());
        backfix_kv = std::nullopt;
        return false;
      } else if (sib_status == BackfixStatus::IS_LINKED) {
        // Check if is level head.
        page_data->RedistributeWithRight(sib_g_w, 0, kv_sum);
        if (page_data->IsLevelHead()) {  // Level head 一定没有左 Sibling.
          qsbr_mgr_.Retire(page_g.GetPageId());
          sib_data->SetIsLevelHead(true);
        }

        auto page_status{page_data->GetBackfixStatus()};

        if (page_status == BackfixStatus::IS_LINKED) {
          page_data->SetBackfixStatus(BackfixStatus::NEED_UNLINK);
          backfix_kv = {page_data->GetLowKey(), page_g.GetPageId()};
          page_g = std::move(sib_g_w);
          return true;
        } else if (page_status == BackfixStatus::NEED_LINK) {  // NOLINT
          page_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
          backfix_kv = std::nullopt;
          page_g = std::move(sib_g_w);
          return false;
        }

        // `page_status` 不可能是 NEED_UNLINK/IS_UNLINKED, 因为此时 `page_data` 应该为空.
        BUSTUB_ASSERT(false, "Protocol error.");
      }
    }
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryNewRoot(page_id_t &check_root_id, int new_level) -> bool {
  page_id_t new_root_id{INVALID_PAGE_ID};
  {
    WritePageGuard new_root_g_w{bpm_->WritePage(bpm_->NewPage())};
    if (check_root_id == INVALID_PAGE_ID) {  // Create a new leaf root.
      auto new_root_data{new_root_g_w.AsMut<LeafPage>()};
      new_root_data->Init();
      new_root_data->SetLevel(new_level);
      new_root_data->SetIsLevelHead(true);
    } else {
      auto new_root_data{new_root_g_w.AsMut<InternalPage>()};
      new_root_data->Init();
      new_root_data->NewKvAt(0);
      new_root_data->ModifyKvAt(0, KeyType{}, check_root_id);
      new_root_data->SetLevel(new_level);
      new_root_data->SetIsLevelHead(true);
    }
    new_root_id = new_root_g_w.GetPageId();
  }

  BUSTUB_ASSERT(new_root_id != INVALID_PAGE_ID, "Unexpected.");

  if (check_root_id == INVALID_PAGE_ID) {
    WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
    auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
    if (tree_header_data->root_page_id_ != check_root_id) {
      bpm_->DeletePage(new_root_id);
      check_root_id = tree_header_data->root_page_id_;
      return false;
    }
    tree_header_data->root_page_id_ = new_root_id;
  } else {
    {
      ReadPageGuard root_page_g{bpm_->ReadPage(check_root_id)};
      auto root_header_data{root_page_g.As<BPlusTreePage>()};
      if (root_header_data->IsLeafPage()) {
        auto root_data{root_page_g.As<LeafPage>()};
        // BUSTUB_ASSERT(root_data->IsLevelHead(), "Invalid args.");  // legacy !hasvalidlowkey == islevelhead.
        if (root_data->GetNextPageId() == INVALID_PAGE_ID) {
          bpm_->DeletePage(new_root_id);
          check_root_id = GetRootPageId();
          return false;
        }
        {
          WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
          auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
          if (tree_header_data->root_page_id_ != check_root_id) {
            bpm_->DeletePage(new_root_id);
            check_root_id = tree_header_data->root_page_id_;
            return false;
          }
          tree_header_data->root_page_id_ = new_root_id;
        }
      } else {
        auto root_data{root_page_g.As<InternalPage>()};
        // BUSTUB_ASSERT(root_data->IsLevelHead(), "Invalid args.");
        if (root_data->GetNextPageId() == INVALID_PAGE_ID) {
          bpm_->DeletePage(new_root_id);
          check_root_id = GetRootPageId();
          return false;
        }
        {
          WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
          auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
          if (tree_header_data->root_page_id_ != check_root_id) {
            bpm_->DeletePage(new_root_id);
            check_root_id = tree_header_data->root_page_id_;
            return false;
          }
          tree_header_data->root_page_id_ = new_root_id;
        }
      }
    }
  }
  check_root_id = new_root_id;
  return true;
}

/**
 * `TryNewRoot` 和 `TryDeleteRoot` 必须和 Split/Merge 解耦.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryDeleteRoot(page_id_t &check_root_id) -> bool {
  BUSTUB_ASSERT(check_root_id != INVALID_PAGE_ID, "Invalid args.");
  page_id_t new_root_id{INVALID_PAGE_ID};
  {
    ReadPageGuard root_page_g{bpm_->ReadPage(check_root_id)};
    auto root_header_data{root_page_g.As<BPlusTreePage>()};
    if (root_header_data->IsLeafPage()) {
      auto root_data{root_page_g.As<LeafPage>()};
      // BUSTUB_ASSERT(root_data->IsLevelHead(), "Invalid args.");
      if (root_data->GetSize() != 0 || root_data->GetNextPageId() != INVALID_PAGE_ID) {
        check_root_id = GetRootPageId();
        return false;
      }
      {
        WritePageGuard tree_header_g{bpm_->WritePage(header_page_id_)};
        auto tree_header_data{tree_header_g.AsMut<BPlusTreeHeaderPage>()};
        if (tree_header_data->root_page_id_ != check_root_id) {
          check_root_id = tree_header_data->root_page_id_;
          return false;
        }
        tree_header_data->root_page_id_ = INVALID_PAGE_ID;
        new_root_id = INVALID_PAGE_ID;
      }
      qsbr_mgr_.Retire(std::move(check_root_id));  // NOLINT
    } else {
      auto root_data{root_page_g.As<InternalPage>()};
      // BUSTUB_ASSERT(root_data->IsLevelHead(), "Invalid args.");
      if (root_data->GetSize() != 1 || root_data->GetNextPageId() != INVALID_PAGE_ID) {
        check_root_id = GetRootPageId();
        return false;
      }
      {
        WritePageGuard tree_header_g{bpm_->WritePage(header_page_id_)};
        auto tree_header_data{tree_header_g.AsMut<BPlusTreeHeaderPage>()};
        if (tree_header_data->root_page_id_ != check_root_id) {
          check_root_id = tree_header_data->root_page_id_;
          return false;
        }
        tree_header_data->root_page_id_ = root_data->ValueAt(0);
        new_root_id = root_data->ValueAt(0);
      }
      qsbr_mgr_.Retire(std::move(check_root_id));  // NOLINT
    }
  }
  check_root_id = new_root_id;
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixInsertKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto &[backfix_key, backfix_val]{backfix_kv.value()};
  IndexKvPack new_backfix_kv{std::nullopt};
  GoSibUntilCoverWW(page_g, backfix_key);
  auto page_data{page_g.AsMut<InternalPage>()};
  // 注意此处顺序. 此位置, backfix_kv 是将要被插入到当前节点的 kv.
  // 此处的 TrySplit 不应该覆盖 backfix_kv 否则出错.
  bool ret{TrySplitAndGo(page_g, backfix_key, new_backfix_kv)};
  page_data = page_g.AsMut<InternalPage>();
  {
    WritePageGuard check_page_g{bpm_->WritePage(backfix_val)};
    auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
    if (check_page_header_data->GetBackfixStatus() != BackfixStatus::NEED_LINK) {
      BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::IS_UNLINKED, "Protocol error.");
      backfix_kv = std::move(new_backfix_kv);
      return ret;
    }
    BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::NEED_LINK, "Protocol error.");
    check_page_header_data->SetBackfixStatus(BackfixStatus::IS_LINKED);
    // Should hold `check` lock.
    int upperbound_idx{page_data->FindIndexUpperbound(backfix_key, comparator_)};
    page_data->NewKvAt(upperbound_idx);
    page_data->ModifyKvAt(upperbound_idx, backfix_key, backfix_val);
  }
  backfix_kv = std::move(new_backfix_kv);
  return ret;
}

/**
 * Todo.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixRemoveKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto &[backfix_key, backfix_val]{backfix_kv.value()};
  GoSibUntilCoverWW(page_g, backfix_key);
  BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Invalid args.");
  auto page_data{page_g.AsMut<InternalPage>()};

  int check_idx{page_data->FindKv(backfix_key, backfix_val, comparator_)};
  while (check_idx == page_data->GetSize() && page_data->GetNextPageId() != INVALID_PAGE_ID &&
         comparator_(page_data->GetHighKey(), backfix_key) <= 0) {
    page_id_t next_id{page_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->WritePage(next_id);
    page_data = page_g.AsMut<InternalPage>();
    check_idx = page_data->FindKv(backfix_key, backfix_val, comparator_);
  }

  {
    WritePageGuard check_page_g{bpm_->WritePage(backfix_val)};
    auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
    if (check_page_header_data->GetBackfixStatus() != BackfixStatus::NEED_UNLINK) {
      BUSTUB_ASSERT(false, "Protocol error.");
    }
    BUSTUB_ASSERT(check_idx < page_data->GetSize(), "Protocol error.");
    page_data->DeleteKvAt(check_idx);
    check_page_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
    TryMergeAndGo(page_g, backfix_kv);
  }

  return true;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
