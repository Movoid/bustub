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
#include <memory_resource>
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
  // LC: Lock parent.
  auto header_page_g{bpm_->WritePage(header_page_id_)};
  auto header_data{header_page_g.AsMut<BPlusTreeHeaderPage>()};
  page_id_t init_root{bpm_->NewPage()};
  header_data->root_page_id_ = init_root;
  header_data->first_leaf_id_ = init_root;
  // LC: Lock child.
  auto root_page_g_w{bpm_->WritePage(header_data->root_page_id_)};
  // LC: Unlock parent.
  header_page_g.Drop();

  auto root_data{root_page_g_w.AsMut<LeafPage>()};
  root_data->Init();
  root_data->SetLevel(1);
  // The most left nodes are not controlled by this protocol.
  root_data->SetBackfixStatus(BackfixStatus::INVALID);

  // LC: Unlock child.
  root_page_g_w.Drop();
}

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
  page_id_t cur_page_id{GetRootPageId()};
  bool is_found{};

  qsbr_mgr_.EnterCriticalZone();
  while (true) {
    auto page_g{bpm_->ReadPage(cur_page_id)};
    page_g = GoSibUntilCoverRR(key, std::move(page_g));

    if (page_g.As<BPlusTreePage>()->IsLeafPage()) {
      qsbr_mgr_.ExitCriticalZone();
      // page_g = GoSibUntilCoverRR(key, std::move(page_g));
      // 目标 kv 最多只会出现在一个节点, 沿着 SibLink walk 到 Covered 节点即可.
      auto page_data{page_g.As<LeafPage>()};
      PrintTree(page_g.GetPageId(), page_data);
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
    // BUSTUB_ASSERT(upperbound_idx > 0, "All keys in this node are above `key`? Invariant broken.");
    cur_page_id = page_data->ValueAt(upperbound_idx - 1);
  }

  if (qsbr_mgr_.GetRetiredCntLocal() > 32) {
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
  page_id_t cur_root_id{GetRootPageId()};

  PathContainer backfix_path{};
  IndexKvPack backfix_kv{std::nullopt};
  BackfixType backfix_type{BackfixType::INVALID};

  qsbr_mgr_.EnterCriticalZone();  // Remember to exit!

  // Phase 1.
  {
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
    auto page_header_data{page_g.As<BPlusTreePage>()};
    while (!page_header_data->IsLeafPage()) {
      page_g = GoSibUntilCoverRR(key, std::move(page_g));
      // Is internal node. Go down.
      backfix_path.EmplaceBack(page_g.GetPageId());
      auto page_data{page_g.As<InternalPage>()};
      int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
      // BUSTUB_ASSERT(upperbound_idx > 0, "All keys in this node are above `key`? Invariant broken.");
      page_g = bpm_->ReadPage(page_data->ValueAt(upperbound_idx - 1));
      page_header_data = page_g.As<BPlusTreePage>();
    }

    WritePageGuard leaf_g_w{GoSibUntilCoverRW(key, std::move(page_g))};
    // Split leaf?
    TrySplit(leaf_g_w, backfix_kv, backfix_path.Size() == 0, cur_root_id);
    backfix_type = backfix_kv.has_value() ? BackfixType::INSERT : BackfixType::INVALID;
    // 注意此处, 不一定要移动到 new sibling,
    // 可能目标 key 仍在当前节点.
    leaf_g_w = GoSibUntilCoverWW(key, std::move(leaf_g_w));
    auto leaf_data{leaf_g_w.AsMut<LeafPage>()};
    BUSTUB_ASSERT(!leaf_data->IsOverflowed(), "Unexpected. This should be fixed before.");
    // Do some **REAL** modifications.
    int upperbound_idx{leaf_data->FindIndexUpperbound(key, comparator_)};
    // BUSTUB_ASSERT(upperbound_idx > 0, "Invariant broken.");
    if (upperbound_idx > 0 && comparator_(leaf_data->KeyAt(upperbound_idx - 1), key) == 0) {
      return false;
    }
    leaf_data->NewKvAt(upperbound_idx);
    leaf_data->ModifyKvAt(upperbound_idx, key, value);
  }

  // Phase 2.
  // Insert `backfix_key` to parent.
  {
    int next_level{2};
    while (backfix_kv.has_value()) {
      auto &[key, val]{backfix_kv.value()};
      if (backfix_path.Empty()) {
        GetPathTo(next_level, key, backfix_path);
        BUSTUB_ASSERT(!backfix_path.Empty(), "Unexpected.");
        continue;
      }
      WritePageGuard page_g{bpm_->WritePage(backfix_path.Back())};
      page_g = GoSibUntilCoverWW(key, std::move(page_g));
      auto page_header_data{page_g.AsMut<BPlusTreePage>()};
      if (page_header_data->GetLevel() != next_level) {
        // ? DONT_LINK?
        BUSTUB_ASSERT(backfix_type == BackfixType::INSERT, "Protocol error.");
        ReadPageGuard check_page_g{bpm_->ReadPage(val)};
        auto check_page_header_data{check_page_g.As<BPlusTreePage>()};
        BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::DONT_LINK_IN_PARENT,
                      "Protocol error.");
        break;
      }
      next_level++;
      if (backfix_type == BackfixType::INSERT) {
        BackfixInsertKv(page_g, backfix_kv, backfix_path.Size() == 1, cur_root_id);
      } else if (backfix_type == BackfixType::REMOVE) {
        BackfixRemoveKv(page_g, backfix_kv, backfix_path.Size() == 2, cur_root_id);
      } else {
        BUSTUB_ASSERT(false, "Protocol error.");
      }
      backfix_path.PopBack();
    }
  }

  qsbr_mgr_.ExitCriticalZone();
  if (qsbr_mgr_.GetRetiredCntLocal() > 32) {
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
  // Declaration of context instance.
  Context ctx;
  UNIMPLEMENTED("TODO(P2): Add implementation.");
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
  page_id_t beg_id{GetFirstLeafId()};
  if (beg_id == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE{std::nullopt, bpm_};
  }
  return INDEXITERATOR_TYPE{bpm_->ReadPage(beg_id), 0, bpm_};
}

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

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
  if (header_page_id_ == INVALID_PAGE_ID) {
    return INVALID_PAGE_ID;
  }
  auto root_data{bpm_->ReadPage(header_page_id_).As<BPlusTreeHeaderPage>()};
  return root_data->root_page_id_;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetFirstLeafId() -> page_id_t {
  if (header_page_id_ == INVALID_PAGE_ID) {
    return INVALID_PAGE_ID;
  }
  auto root_data{bpm_->ReadPage(header_page_id_).As<BPlusTreeHeaderPage>()};
  return root_data->first_leaf_id_;
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
auto BPLUSTREE_TYPE::GoSibUntilCoverRR(const KeyType &key, ReadPageGuard &&page_g) -> ReadPageGuard {
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {  // leaf
    while (!page_g.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<LeafPage>()};
      page_g = std::move(bpm_->ReadPage(page_data->GetNextPageId()));
    }
  } else {  // internal
    while (!page_g.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<InternalPage>()};
      page_g = std::move(bpm_->ReadPage(page_data->GetNextPageId()));
    }
  }
  return page_g;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverWW(const KeyType &key, WritePageGuard &&page_g) -> WritePageGuard {
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {  // leaf
    while (!page_g.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<LeafPage>()};
      page_g = std::move(bpm_->WritePage(page_data->GetNextPageId()));
    }
  } else {  // internal
    while (!page_g.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<InternalPage>()};
      page_g = std::move(bpm_->WritePage(page_data->GetNextPageId()));
    }
  }
  return page_g;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverRW(const KeyType &key, ReadPageGuard &&page_g) -> WritePageGuard {
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {  // leaf
    while (!page_g.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<LeafPage>()};
      page_g = std::move(bpm_->ReadPage(page_data->GetNextPageId()));
    }
  } else {  // internal
    while (!page_g.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_g.As<InternalPage>()};
      page_g = std::move(bpm_->ReadPage(page_data->GetNextPageId()));
    }
  }

  page_id_t cur_page_id{page_g.GetPageId()};
  page_g.Drop();

  auto page_wg{bpm_->WritePage(cur_page_id)};
  if (page_wg.As<BPlusTreePage>()->IsLeafPage()) {  // leaf
    while (!page_wg.As<LeafPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_wg.As<LeafPage>()};
      page_wg = std::move(bpm_->WritePage(page_data->GetNextPageId()));
    }
  } else {  // internal
    while (!page_wg.As<InternalPage>()->CoveredKey(key, comparator_)) {
      auto page_data{page_wg.As<InternalPage>()};
      page_wg = std::move(bpm_->WritePage(page_data->GetNextPageId()));
    }
  }
  return page_wg;
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
  page_id_t cur_page_id{GetRootPageId()};
  ReadPageGuard page_g{bpm_->ReadPage(cur_page_id)};
  if (page_g.As<BPlusTreePage>()->IsLeafPage()) {
    return;
  }
  page_g = GoSibUntilCoverRR(key, std::move(page_g));
  auto page_header_data{page_g.As<BPlusTreePage>()};
  auto page_data{page_g.As<InternalPage>()};
  path.EmplaceBack(page_g.GetPageId());
  while (page_header_data->GetLevel() > to_level) {
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
    page_g = bpm_->ReadPage(page_data->ValueAt(upperbound_idx - 1));
    page_header_data = page_g.As<BPlusTreePage>();
    page_data = page_g.As<InternalPage>();
    path.EmplaceBack(page_g.GetPageId());
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
auto BPLUSTREE_TYPE::TrySplit(WritePageGuard &page_g, IndexKvPack &backfix_kv, bool update_root_hint,
                              page_id_t root_hint) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  if (!page_header_data->IsOverflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }

  int level{0};
  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    WritePageGuard new_sib_g_w{bpm_->WritePage(bpm_->NewPage())};
    auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
    new_sib_data->Init();
    int kv_sum{page_data->GetSize() + new_sib_data->GetSize()};
    page_data->RedistributeWithRight(new_sib_g_w, (kv_sum + 1) / 2, kv_sum / 2);
    new_sib_data->SetNextPageId(page_data->GetNextPageId());
    page_data->SetNextPageId(new_sib_g_w.GetPageId());
    level = page_data->GetLevel();
    backfix_kv = {new_sib_data->KeyAt(0), new_sib_g_w.GetPageId()};
  } else {
    auto page_data{page_g.AsMut<InternalPage>()};
    WritePageGuard new_sib_g_w{bpm_->WritePage(bpm_->NewPage())};
    auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
    new_sib_data->Init();
    int kv_sum{page_data->GetSize() + new_sib_data->GetSize()};
    page_data->RedistributeWithRight(new_sib_g_w, (kv_sum + 1) / 2, kv_sum / 2);
    new_sib_data->SetNextPageId(page_data->GetNextPageId());
    page_data->SetNextPageId(new_sib_g_w.GetPageId());
    level = page_data->GetLevel();
    backfix_kv = {new_sib_data->KeyAt(0), new_sib_g_w.GetPageId()};
  }
  if (update_root_hint) {
    TryNewRoot(root_hint, level + 1);
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryMerge(WritePageGuard &page_g, IndexKvPack &backfix_kv, bool update_root_hint,
                              page_id_t root_hint) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  if (!page_header_data->IsUnderflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }
  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    if (page_data->GetNextPageId() == INVALID_PAGE_ID) {
      backfix_kv = std::nullopt;
      return false;
    }
    WritePageGuard sib_g_w{bpm_->WritePage(page_data->GetNextPageId())};
    if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
      backfix_kv = std::nullopt;
      return false;
    }

    // Merge.
    auto sib_data{sib_g_w.AsMut<LeafPage>()};
    int kv_sum{page_data->GetSize() + sib_data->GetSize()};
    backfix_kv = {sib_data->KeyAt(0), sib_g_w.GetPageId()};
    page_data->RedistributeWithRight(sib_g_w, kv_sum, 0);
    page_data->SetNextPageId(sib_data->GetNextPageId());
    sib_data->SetNextPageId(page_g.GetPageId());
    // 在此处 Retire 不合适, 但可以工作.
    qsbr_mgr_.Retire(std::move(sib_g_w.GetPageId()));  // NOLINT

    // Protocol check.
    if (sib_data->GetBackfixStatus() == BackfixStatus::NOT_LINKED_IN_PARENT) {
      sib_data->SetBackfixStatus(BackfixStatus::DONT_LINK_IN_PARENT);
      backfix_kv = std::nullopt;
      return false;
    }
  } else {
    auto page_data{page_g.AsMut<InternalPage>()};
    if (page_data->GetNextPageId() == INVALID_PAGE_ID) {
      backfix_kv = std::nullopt;
      return false;
    }
    WritePageGuard sib_g_w{bpm_->WritePage(page_data->GetNextPageId())};
    if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
      backfix_kv = std::nullopt;
      return false;
    }

    // Merge.
    auto sib_data{sib_g_w.AsMut<InternalPage>()};
    int kv_sum{page_data->GetSize() + sib_data->GetSize()};
    backfix_kv = {sib_data->KeyAt(0), sib_g_w.GetPageId()};
    page_data->RedistributeWithRight(sib_g_w, kv_sum, 0);
    page_data->SetNextPageId(sib_data->GetNextPageId());
    sib_data->SetNextPageId(page_g.GetPageId());
    qsbr_mgr_.Retire(std::move(sib_g_w.GetPageId()));  // NOLINT

    // Protocol check.
    if (sib_data->GetBackfixStatus() == BackfixStatus::NOT_LINKED_IN_PARENT) {
      sib_data->SetBackfixStatus(BackfixStatus::DONT_LINK_IN_PARENT);
      backfix_kv = std::nullopt;
      return false;
    }
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryNewRoot(page_id_t check_root_id, int new_level) -> bool {
  WritePageGuard new_root_g_w{bpm_->WritePage(bpm_->NewPage())};
  auto new_root_data{new_root_g_w.AsMut<InternalPage>()};
  new_root_data->Init();
  new_root_data->NewKvAt(0);
  new_root_data->SetLevel(new_level);
  new_root_data->ModifyKvAt(0, KeyType{}, check_root_id);
  new_root_data->SetHasValidLowKey(true);

  // Check
  WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
  auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
  if (tree_header_data->root_page_id_ != check_root_id) {
    bpm_->DeletePage(new_root_g_w.GetPageId());
    return false;
  }
  tree_header_data->root_page_id_ = new_root_g_w.GetPageId();
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryDeleteRoot(page_id_t check_root_id, page_id_t new_root_id) -> bool {
  WritePageGuard tree_header_g_w{bpm_->WritePage(header_page_id_)};
  auto tree_header_data{tree_header_g_w.AsMut<BPlusTreeHeaderPage>()};
  if (tree_header_data->root_page_id_ != check_root_id) {
    return false;
  }
  tree_header_data->root_page_id_ = new_root_id;
  qsbr_mgr_.Retire(std::move(check_root_id));  // NOLINT
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixInsertKv(WritePageGuard &page_g, IndexKvPack &backfix_kv, bool update_root_hint,
                                     page_id_t root_hint) -> bool {
  auto &[key, val]{backfix_kv.value()};
  page_g = GoSibUntilCoverWW(key, std::move(page_g));
  auto page_data{page_g.AsMut<InternalPage>()};
  int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
  WritePageGuard check_page_g{bpm_->WritePage(val)};
  auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
  bool ret{TrySplit(page_g, backfix_kv, update_root_hint, root_hint)};
  page_g = GoSibUntilCoverWW(key, std::move(page_g));
  if (check_page_header_data->GetBackfixStatus() == BackfixStatus::DONT_LINK_IN_PARENT) {
    return ret;
  }
  BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::NOT_LINKED_IN_PARENT, "Protocol error.");
  page_data->NewKvAt(upperbound_idx);
  page_data->ModifyKvAt(upperbound_idx, key, val);
  check_page_header_data->SetBackfixStatus(BackfixStatus::IS_LINKED_IN_PARENT);
  return ret;
}

/**
 * Todo.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixRemoveKv(WritePageGuard &page_g, IndexKvPack &backfix_kv, bool update_root_hint,
                                     page_id_t root_hint) -> bool {
  auto &[key, val]{backfix_kv.value()};
  page_g = GoSibUntilCoverWW(key, std::move(page_g));
  auto page_header_data{page_g.AsMut<BPlusTreePage>()};
  BUSTUB_ASSERT(!page_header_data->IsLeafPage(), "Invalid args.");
  auto page_data{page_g.AsMut<InternalPage>()};
  while (true) {
    if (!page_data->HasValidHighKey() || comparator_(page_data->GetHighKey(), key) > 0) {
      break;
    }
    auto check_idx{page_data->FindKv(key, val, comparator_)};
    if (check_idx == page_data->GetSize()) {
      // 不存在. Go to siblnig.
      page_id_t sib_id{page_data->GetNextPageId()};
      page_g = bpm_->WritePage(sib_id);
      page_data = page_g.AsMut<InternalPage>();
      continue;
    }
    page_data->DeleteKvAt(check_idx);
    return TryMerge(page_g, backfix_kv, update_root_hint, update_root_hint);
  }
  // This is unreachable.
  BUSTUB_ASSERT(false, "Protocol error.");
  return false;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
