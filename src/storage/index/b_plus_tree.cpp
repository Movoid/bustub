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

// #define CUSTOM_LOG
// #include "common/custom_debug.h"

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
 * 正常情况下, 树 empty 后会删除 Root.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  BUSTUB_ASSERT(header_page_id_ != INVALID_PAGE_ID, "Unexpected.");
  page_id_t cur_root_id{GetRootPageId()};
  return cur_root_id == INVALID_PAGE_ID;
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
  page_id_t cur_root_id{GetRootPageId()};

  if (cur_root_id == INVALID_PAGE_ID) {
    return false;
  }

  ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
  GoSibUntilCoverRR(page_g, key);
  while (!page_g.As<BPlusTreePage>()->IsLeafPage()) {
    auto page_data{page_g.As<InternalPage>()};
    int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
    page_id_t next_id{lowerbound_idx == 0 ? page_data->GetLowLevelHeadId() : page_data->ValueAt(lowerbound_idx - 1)};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
    GoSibUntilCoverRR(page_g, key);
  }

  BUSTUB_ASSERT(page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
  auto page_data{page_g.As<LeafPage>()};
  bool is_found{};
  int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
  if (lowerbound_idx != page_data->GetSize() && comparator_(page_data->KeyAt(lowerbound_idx), key) == 0) {
    result->emplace_back(page_data->ValueAt(lowerbound_idx));
    is_found = true;
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

  // Insert/Split 不会变化 `cur_root_id`, 不需要频繁更新.

  if (cur_root_id == INVALID_PAGE_ID) {
    TryNewRoot(cur_root_id);
    BUSTUB_ASSERT(cur_root_id != INVALID_PAGE_ID, "Unexpected.");
  }

  PathContainer backfix_path{};
  IndexKvPack backfix_kv{std::nullopt};

  // Locate.
  {
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
    GoSibUntilCoverRR(page_g, key);
    while (!page_g.As<BPlusTreePage>()->IsLeafPage()) {
      // Is internal node. Go down.
      backfix_path.EmplaceBack(page_g.GetPageId());
      auto page_data{page_g.As<InternalPage>()};
      int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
      page_id_t next_id{lowerbound_idx == 0 ? page_data->GetLowLevelHeadId() : page_data->ValueAt(lowerbound_idx - 1)};
      page_g.Drop();
      page_g = bpm_->ReadPage(next_id);
      GoSibUntilCoverRR(page_g, key);
    }

    WritePageGuard leaf_g_w{GoSibUntilCoverRW(std::move(page_g), key)};
    page_id_t last_leaf_id{leaf_g_w.GetPageId()};
    // Split
    while (TrySplitAndGo(leaf_g_w, key, backfix_kv)) {
      last_leaf_id = leaf_g_w.GetPageId();
      leaf_g_w.Drop();

      // Drop and start backfix.
      PathContainer cur_backfix_path{backfix_path};
      int next_level{2};

      while (backfix_kv.has_value()) {
        auto &[backfix_key, backfix_val, ign1, ign2]{backfix_kv.value()};
        if (cur_backfix_path.Empty()) {
          TryNewRoot(cur_root_id);
          cur_backfix_path.Clear();
          GetPathTo(next_level, backfix_key, cur_backfix_path);
          if (cur_backfix_path.Empty()) {
            // Backfix Task 可能被取消, 需要重新检查.
            {
              ReadPageGuard check_page_g{bpm_->ReadPage(backfix_val)};
              auto check_page_header_data{check_page_g.As<BPlusTreePage>()};
              if (check_page_header_data->GetBackfixStatus() != BackfixStatus::NEED_LINK) {
                backfix_kv = std::nullopt;
                break;
              }
            }
          }
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
            backfix_kv = std::nullopt;
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

    // BUSTUB_ASSERT(page_data->IsLevelHead() || upperbound_idx > 0, "Unexpected.");
    BUSTUB_ASSERT(page_data->IsLevelHead() || comparator_(page_data->GetLowKey(), key) <= 0, "Unexpected.");
    BUSTUB_ASSERT(page_data->GetNextPageId() == INVALID_PAGE_ID || comparator_(key, page_data->GetHighKey()) < 0,
                  "Unexpected.");

    // 查重
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

  // Merge 会改变 `cur_root_id`.

  if (cur_root_id == INVALID_PAGE_ID) {
    qsbr_mgr_.ExitCriticalZone();
    return;
  }

  PathContainer backfix_path{};
  IndexKvPack backfix_kv{std::nullopt};
  bool try_delete_root_hint{};

  {
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
    GoSibUntilCoverRR(page_g, key);
    while (!page_g.As<BPlusTreePage>()->IsLeafPage()) {
      backfix_path.EmplaceBack(page_g.GetPageId());
      auto page_data{page_g.As<InternalPage>()};
      int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
      // page_id_t next_id{page_data->ValueAt(std::max(0, upperbound_idx - 1))};
      page_id_t next_id{lowerbound_idx == 0 ? page_data->GetLowLevelHeadId() : page_data->ValueAt(lowerbound_idx - 1)};
      page_g.Drop();
      page_g = bpm_->ReadPage(next_id);
      GoSibUntilCoverRR(page_g, key);
    }
    WritePageGuard leaf_g_w{GoSibUntilCoverRW(std::move(page_g), key)};
    auto leaf_data{leaf_g_w.AsMut<LeafPage>()};
    int lowerbound_idx{leaf_data->FindIndexLowerbound(key, comparator_)};
    if (lowerbound_idx == leaf_data->GetSize() || comparator_(leaf_data->KeyAt(lowerbound_idx), key) != 0) {
      // BUSTUB_ASSERT(false, "Unexpected.");
      qsbr_mgr_.ExitCriticalZone();
      return;
    }
    leaf_data->DeleteKvAt(lowerbound_idx);

    try_delete_root_hint |=
        leaf_data->GetSize() == 0 && leaf_data->IsLevelHead() && leaf_data->GetNextPageId() == INVALID_PAGE_ID;

    // Merge.
    page_id_t last_leaf_id{leaf_g_w.GetPageId()};
    while (TryMergeAndGo(leaf_g_w, backfix_kv)) {
      try_delete_root_hint |= true;
      last_leaf_id = leaf_g_w.GetPageId();
      leaf_g_w.Drop();

      // Drop and start backfix.
      PathContainer cur_backfix_path{backfix_path};
      int next_level{2};
      while (backfix_kv.has_value()) {
        auto &[backfix_key, backfix_val, ign1, ign2]{backfix_kv.value()};
        if (cur_backfix_path.Empty()) {
          cur_backfix_path.Clear();
          GetPathTo(next_level, backfix_key, cur_backfix_path);
          BUSTUB_ASSERT(!cur_backfix_path.Empty(), "Unexpected.");
        }
        {
          WritePageGuard page_g{bpm_->WritePage(cur_backfix_path.Back())};
          BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
          GoSibUntilCoverWW(page_g, backfix_key);
          // 这里可能到达一个被删除的 Root.
          auto page_data{page_g.As<InternalPage>()};
          if (!page_data->CoveredKey(backfix_key, comparator_)) {
            page_g.Drop();
            cur_backfix_path.Clear();
            GetPathTo(next_level, backfix_key, cur_backfix_path);
            continue;
          }
          BackfixRemoveKv(page_g, backfix_kv);
          // auto page_data{page_g.As<InternalPage>()};
          page_data = page_g.As<InternalPage>();
          try_delete_root_hint |=
              page_data->GetSize() == 0 && page_data->IsLevelHead() && page_data->GetNextPageId() == INVALID_PAGE_ID;
        }
        next_level++;
        cur_backfix_path.PopBack();
      }

      leaf_g_w = bpm_->WritePage(last_leaf_id);
      // 此处不要 GoSib , 否则可能会跳过可以 Merge 的节点.
      last_leaf_id = leaf_g_w.GetPageId();
    }
  }

  // 这里应该让 TryDeleteRoot 自己寻找 root id.
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
    page_id_t next_id{page_data->GetLowLevelHeadId()};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
  }
  BUSTUB_ASSERT(page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");

  auto page_data{page_g.As<LeafPage>()};
  while (!page_data->IsLevelHead() && page_data->GetNextPageId() != INVALID_PAGE_ID) {
    BUSTUB_ASSERT(page_data->GetSize() == 0, "Unexpected.");
    page_id_t sib_id{page_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->ReadPage(sib_id);
    page_data = page_g.As<LeafPage>();
  }

  BUSTUB_ASSERT(page_data->IsLevelHead(), "Unexpected.");

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
    int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
    page_id_t next_id{lowerbound_idx == 0 ? page_data->GetLowLevelHeadId() : page_data->ValueAt(lowerbound_idx - 1)};
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
auto BPLUSTREE_TYPE::GetRootPageId() const -> page_id_t {
  BUSTUB_ASSERT(header_page_id_ != INVALID_PAGE_ID, "Unexpected.");
  ReadPageGuard tree_header_g{bpm_->ReadPage(header_page_id_)};
  auto tree_header_data{tree_header_g.As<BPlusTreeHeaderPage>()};
  return tree_header_data->root_page_id_;
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
    int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
    // page_id_t next_id{page_data->ValueAt(std::max(0, upperbound_idx - 1))};
    page_id_t next_id{lowerbound_idx == 0 ? page_data->GetLowLevelHeadId() : page_data->ValueAt(lowerbound_idx - 1)};
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
      page_id_t new_sib_id{bpm_->NewPage()};
      WritePageGuard new_sib_g_w{bpm_->WritePage(new_sib_id)};
      auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
      new_sib_data->Init(leaf_max_size_);
      int kv_sum{page_data->GetSize() + new_sib_data->GetSize()};
      page_data->RedistributeWithRight(new_sib_g_w, (kv_sum + 1) / 2, kv_sum / 2);
      new_sib_data->SetNextPageId(page_data->GetNextPageId());
      page_data->SetNextPageId(new_sib_g_w.GetPageId());
      new_sib_data->SetLevel(page_data->GetLevel());
      backfix_kv = {new_sib_data->GetLowKey(), new_sib_g_w.GetPageId(), false, INVALID_PAGE_ID};
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
      page_id_t new_sib_id{bpm_->NewPage()};
      WritePageGuard new_sib_g_w{bpm_->WritePage(new_sib_id)};
      auto new_sib_data{new_sib_g_w.AsMut<InternalPage>()};
      new_sib_data->Init(internal_max_size_);
      int kv_sum{page_data->GetSize() + new_sib_data->GetSize()};
      page_data->RedistributeWithRight(new_sib_g_w, (kv_sum + 1) / 2, kv_sum / 2);
      new_sib_data->SetNextPageId(page_data->GetNextPageId());
      page_data->SetNextPageId(new_sib_g_w.GetPageId());
      new_sib_data->SetLevel(page_data->GetLevel());
      backfix_kv = {new_sib_data->GetLowKey(), new_sib_g_w.GetPageId(), false, INVALID_PAGE_ID};
      new_sib_data->SetBackfixStatus(BackfixStatus::NEED_LINK);
      if (!page_data->CoveredKey(key, comparator_)) {
        page_g = std::move(new_sib_g_w);
      }
    }
  }
  return true;
}

/**
 * 返回 true 表示 Merge 发生.
 *
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryMergeAndGo(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto page_header_data{page_g.AsMut<BPlusTreePage>()};
  if (!page_header_data->IsUnderflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }

  page_id_t sib_id{page_header_data->GetNextPageId()};
  if (sib_id == INVALID_PAGE_ID) {
    backfix_kv = std::nullopt;
    return false;
  }

  WritePageGuard sib_g_w{bpm_->WritePage(sib_id)};
  auto sib_header_data{sib_g_w.AsMut<BPlusTreePage>()};
  if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
    backfix_kv = std::nullopt;
    return false;
  }

  auto sib_status{sib_header_data->GetBackfixStatus()};
  int kv_sum{page_header_data->GetSize() + sib_header_data->GetSize()};

  switch (sib_status) {
    case BackfixStatus::NEED_LINK:
      /**
       * 向左合并并撤销 Sibling 的 `NEED_LINK` 为 `IS_UNLINKED`.
       */
      if (page_header_data->IsLeafPage()) {
        auto page_data{page_g.AsMut<LeafPage>()};
        page_data->RedistributeWithRight(sib_g_w, kv_sum, 0);
      } else {
        auto page_data{page_g.AsMut<InternalPage>()};
        page_data->RedistributeWithRight(sib_g_w, kv_sum, 0);
      }
      page_header_data->SetNextPageId(sib_header_data->GetNextPageId());
      sib_header_data->SetNextPageId(page_g.GetPageId());
      sib_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
      qsbr_mgr_.Retire(sib_g_w.GetPageId());
      backfix_kv = std::nullopt;
      break;

    case BackfixStatus::NEED_UNLINK:
      // 在 BackfixRemoveKv 里 Retire.
    case BackfixStatus::IS_UNLINKED:
      BUSTUB_ASSERT(sib_header_data->GetSize() == 0, "Unexpected.");
      page_header_data->SetNextPageId(sib_header_data->GetNextPageId());
      backfix_kv = std::nullopt;
      break;

    case BackfixStatus::IS_LINKED:

      // 向右合并.
      if (!page_header_data->IsLevelHead()) {
        backfix_kv = std::nullopt;
        return false;
      }

      BUSTUB_ASSERT(page_header_data->GetBackfixStatus() == BackfixStatus::IS_LINKED, "Protocol error.");

      // 如果 page_data 是原有的 Root, 其 BackfixStatus 怎么更新?
      // 向右合并会变更 Level Head. Tree Header 中的 Root Id 需要被正确更新.

      page_header_data->SetIsLevelHead(false);
      if (page_header_data->IsLeafPage()) {
        auto page_data{page_g.AsMut<LeafPage>()};
        page_data->RedistributeWithRight(sib_g_w, 0, kv_sum);
        auto sib_data{sib_g_w.AsMut<LeafPage>()};
        backfix_kv = {sib_data->GetLowKey(), page_g.GetPageId(), true, sib_g_w.GetPageId()};
      } else {
        auto page_data{page_g.AsMut<InternalPage>()};
        page_data->RedistributeWithRight(sib_g_w, 0, kv_sum);
        auto sib_data{sib_g_w.AsMut<InternalPage>()};
        backfix_kv = {sib_data->GetLowKey(), page_g.GetPageId(), true, sib_g_w.GetPageId()};
      }

      // 如果是 Internal 节点, Sibling 需要继承 `low_level_head_id_`.
      if (!page_header_data->IsLeafPage()) {  // Internal Node.
        /**
         * 考虑 Sibling 的子节点是 Level Head, 其成为 Head 后立刻向右 Merge,
         * 使得本不是 Level Head 的 Sibling 持有有效的 `low_level_head_id_`, 此时不该被覆盖.
         */
        if (sib_header_data->GetLowLevelHeadId() == INVALID_PAGE_ID) {
          sib_header_data->SetLowLevelHeadId(page_header_data->GetLowLevelHeadId());
        } else {
          page_id_t low_id{page_header_data->GetLowLevelHeadId()};
          qsbr_mgr_.Retire(std::move(low_id));  // NOLINT
        }
      }

      /**
       * 此节点不应该被设为 false.
       * 此节点若是 Internal Node, 此时右侧 Internal Node 应该持有有效的 `low_level_head_id_`.
       */
      page_header_data->SetIsLevelHead(false);
      sib_header_data->SetIsLevelHead(true);
      page_header_data->SetBackfixStatus(BackfixStatus::NEED_UNLINK);
      /**
       * 谁 Link 谁 Retire.
       * 此处 Retire 被 Merge 的节点不合适,
       * 因为其仍被其 parent `low_level_head_id_` 连接.
       * 在对 parent 的 BackfixRemoveKv 中 Retire.
       */
      page_g = std::move(sib_g_w);
      break;

    default:
      BUSTUB_ASSERT(false, "Unexpected.");
  };

  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryNewRoot(page_id_t &new_root_id) -> bool {
  page_id_t expected_root_id{GetRootPageId()};
  page_id_t desired_root_id{bpm_->NewPage()};
  WritePageGuard desired_root_g_w{bpm_->WritePage(desired_root_id)};
  if (expected_root_id == INVALID_PAGE_ID) {  // new root is leaf.
    auto desired_root_data{desired_root_g_w.AsMut<LeafPage>()};
    desired_root_data->Init(leaf_max_size_);
  } else {  // new root is internal.
    auto desired_root_data{desired_root_g_w.AsMut<InternalPage>()};
    desired_root_data->Init(internal_max_size_);
  }
  auto desired_root_header_data{desired_root_g_w.AsMut<BPlusTreePage>()};
  desired_root_header_data->SetIsLevelHead(true);

  // Root 属于 `IS_LINKED`.
  desired_root_header_data->SetBackfixStatus(BackfixStatus::IS_LINKED);

  if (expected_root_id == INVALID_PAGE_ID) {
    {
      WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
      auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
      if (tree_header_data->root_page_id_ != INVALID_PAGE_ID) {
        desired_root_g_w.Drop();
        bpm_->DeletePage(desired_root_id);
        new_root_id = tree_header_data->root_page_id_;
        return false;
      }
      auto desired_root_data{desired_root_g_w.AsMut<LeafPage>()};
      desired_root_data->SetLevel(1);
      desired_root_data->SetLowLevelHeadId(INVALID_PAGE_ID);
      tree_header_data->root_page_id_ = desired_root_id;
    }
  } else {
    {
      ReadPageGuard expected_root_g{bpm_->ReadPage(expected_root_id)};
      auto expected_root_header_data{expected_root_g.As<BPlusTreePage>()};
      /**
       * `expected_root` 必须是 `IS_LINKED`.
       */
      if (expected_root_header_data->GetBackfixStatus() != BackfixStatus::IS_LINKED ||
          !expected_root_header_data->IsLevelHead() || expected_root_header_data->GetNextPageId() == INVALID_PAGE_ID) {
        desired_root_g_w.Drop();
        bpm_->DeletePage(desired_root_id);
        new_root_id = GetRootPageId();
        return false;
      }
      {
        WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
        auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
        if (tree_header_data->root_page_id_ != expected_root_id) {
          desired_root_g_w.Drop();
          bpm_->DeletePage(desired_root_id);
          new_root_id = tree_header_data->root_page_id_;
          return false;
        }
        // Update root.
        auto desired_root_data{desired_root_g_w.AsMut<InternalPage>()};
        desired_root_data->SetLevel(expected_root_header_data->GetLevel() + 1);
        desired_root_data->SetLowLevelHeadId(expected_root_g.GetPageId());
        tree_header_data->root_page_id_ = desired_root_id;
      }
    }
  }
  new_root_id = desired_root_id;
  return true;
}

/**
 * `TryNewRoot` 和 `TryDeleteRoot` 必须和 Split/Merge 解耦.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryDeleteRoot(page_id_t &new_root_id) -> bool {
  page_id_t expected_root_id{GetRootPageId()};

  if (expected_root_id == INVALID_PAGE_ID) {
    new_root_id = INVALID_PAGE_ID;
    return false;
  }

  {
    WritePageGuard expected_root_g{bpm_->WritePage(expected_root_id)};
    auto expected_root_header_data{expected_root_g.AsMut<BPlusTreePage>()};
    if (expected_root_header_data->GetBackfixStatus() != BackfixStatus::IS_LINKED ||
        !expected_root_header_data->IsLevelHead() || expected_root_header_data->GetSize() != 0 ||
        expected_root_header_data->GetNextPageId() != INVALID_PAGE_ID) {
      expected_root_g.Drop();
      new_root_id = GetRootPageId();
      return false;
    }
    {
      WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
      auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
      if (tree_header_data->root_page_id_ != expected_root_id) {
        new_root_id = tree_header_data->root_page_id_;
        return false;
      }
      // If expected root is leaf, then new root is `INVALID_PAGE_ID`.
      tree_header_data->root_page_id_ = expected_root_header_data->GetLowLevelHeadId();
      new_root_id = expected_root_header_data->GetLowLevelHeadId();
    }
    /**
     * DeleteRoot 发生后, 可能有其他线程根据 backfix_path 走到此节点, 并插入 kv.
     * 这些 kv 不该被插入, 应该把此旧 Root 标记为 IS_UNLINKED.
     */
    expected_root_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
    qsbr_mgr_.Retire(expected_root_g.GetPageId());
  }
  return true;
}

/**
 * 疑似存在问题. 这个函数貌似不该存在.
 */
// INDEX_TEMPLATE_ARGUMENTS
// auto BPLUSTREE_TYPE::TryUpdateRoot(page_id_t &new_root_id) -> bool {
//   page_id_t expected_root_id{GetRootPageId()};
//   if (expected_root_id == INVALID_PAGE_ID) {
//     new_root_id = INVALID_PAGE_ID;
//     return false;
//   }

//   {
//     ReadPageGuard expected_root_g{bpm_->ReadPage(expected_root_id)};
//     auto expected_root_header_data{expected_root_g.As<BPlusTreePage>()};
//     while (!expected_root_header_data->IsLevelHead() && expected_root_header_data->GetNextPageId() !=
//     INVALID_PAGE_ID) {
//       page_id_t sib_id{expected_root_header_data->GetNextPageId()};
//       expected_root_g.Drop();
//       expected_root_g = bpm_->ReadPage(sib_id);
//       expected_root_header_data = expected_root_g.As<BPlusTreePage>();
//     }
//     BUSTUB_ASSERT(expected_root_header_data->IsLevelHead(), "Unexpected.");
//     page_id_t desired_root_id{expected_root_g.GetPageId()};
//     {
//       WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
//       auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
//       if (tree_header_data->root_page_id_ != expected_root_id) {
//         new_root_id = tree_header_data->root_page_id_;
//         return false;
//       }
//       tree_header_data->root_page_id_ = desired_root_id;
//     }
//   }
//   return true;
// }

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixInsertKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto &[backfix_key, backfix_val, m1, m2]{backfix_kv.value()};
  IndexKvPack new_backfix_kv{std::nullopt};
  GoSibUntilCoverWW(page_g, backfix_key);
  // 注意此处顺序. 此位置, backfix_kv 是将要被插入到当前节点的 kv.
  // 此处的 TrySplit 不应该覆盖 backfix_kv 否则出错.
  bool ret{TrySplitAndGo(page_g, backfix_key, new_backfix_kv)};
  auto page_data{page_g.AsMut<InternalPage>()};
  {
    WritePageGuard check_page_g{bpm_->WritePage(backfix_val)};
    auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
    if (check_page_header_data->GetBackfixStatus() != BackfixStatus::NEED_LINK) {
      BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::IS_UNLINKED, "Protocol error.");
      backfix_kv = std::move(new_backfix_kv);
      return ret;
    }
    BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::NEED_LINK, "Protocol error.");
    // Should hold `check` lock.
    int upperbound_idx{page_data->FindIndexUpperbound(backfix_key, comparator_)};
    check_page_header_data->SetBackfixStatus(BackfixStatus::IS_LINKED);
    page_data->NewKvAt(upperbound_idx);
    page_data->ModifyKvAt(upperbound_idx, backfix_key, backfix_val);
  }
  backfix_kv = std::move(new_backfix_kv);
  return ret;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixRemoveKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto &[backfix_key, backfix_val, is_level_head, new_level_head_id]{backfix_kv.value()};
  BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Invalid args.");

  GoSibUntilCoverWW(page_g, backfix_key);
  auto page_data{page_g.AsMut<InternalPage>()};
  int lowerbound_idx{page_data->FindIndexLowerbound(backfix_key, comparator_)};
  int upperbound_idx{page_data->FindIndexUpperbound(backfix_key, comparator_)};
  if (upperbound_idx - lowerbound_idx > 1) {
    if (is_level_head) {
      for (; lowerbound_idx < upperbound_idx; lowerbound_idx++) {
        if (page_data->ValueAt(lowerbound_idx) == new_level_head_id) {
          break;
        }
      }
    } else {
      for (; lowerbound_idx < upperbound_idx; lowerbound_idx++) {
        if (page_data->ValueAt(lowerbound_idx) == backfix_val) {
          break;
        }
      }
    }
  }
  // 找不到应该尝试向 Sibling 走.
  while (lowerbound_idx == page_data->GetSize() && page_data->GetNextPageId() != INVALID_PAGE_ID) {
    page_id_t sib_id{page_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->WritePage(sib_id);
    GoSibUntilCoverWW(page_g, backfix_key);
    page_data = page_g.AsMut<InternalPage>();
    lowerbound_idx = page_data->FindIndexLowerbound(backfix_key, comparator_);
    upperbound_idx = page_data->FindIndexUpperbound(backfix_key, comparator_);
    if (upperbound_idx - lowerbound_idx > 1) {
      if (is_level_head) {
        for (; lowerbound_idx < upperbound_idx; lowerbound_idx++) {
          if (page_data->ValueAt(lowerbound_idx) == new_level_head_id) {
            break;
          }
        }
      } else {
        for (; lowerbound_idx < upperbound_idx; lowerbound_idx++) {
          if (page_data->ValueAt(lowerbound_idx) == backfix_val) {
            break;
          }
        }
      }
    }
  }

  // BUSTUB_ASSERT(upperbound_idx - lowerbound_idx == 1, "Unexpected.");

  BUSTUB_ASSERT(lowerbound_idx != page_data->GetSize(), "Unexpected.");
  BUSTUB_ASSERT(comparator_(page_data->KeyAt(lowerbound_idx), backfix_key) == 0, "Unexpected.");

  {
    WritePageGuard check_page_g{bpm_->WritePage(backfix_val)};
    auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
    BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::NEED_UNLINK, "Protocol error.");
    page_data->DeleteKvAt(lowerbound_idx);
    qsbr_mgr_.Retire(std::move(backfix_val));  // NOLINT
    check_page_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
  }

  if (is_level_head) {  // Level head mechanisim.
    WritePageGuard check_page_g{bpm_->WritePage(new_level_head_id)};
    auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
    if (check_page_header_data->IsLevelHead()) {
      // `page_data` 可能有旧的 `low_level_head_id_`.
      // 这个旧的 low level head page 应该在此前被标记为 `NEED_UNLINK`.
      page_data->SetLowLevelHeadId(check_page_g.GetPageId());
    }
  }

  TryMergeAndGo(page_g, backfix_kv);

  return true;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
