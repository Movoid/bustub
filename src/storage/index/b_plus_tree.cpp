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
  page_id_t cur_page_id{GetRootPageId()};

  PathContainer backfix_path{};
  bool need_backfix{};
  KeyType backfix_key{};
  page_id_t backfix_page_id{INVALID_PAGE_ID};

  qsbr_mgr_.EnterCriticalZone();  // Remember to exit!

  // Phase 1.
  {
    ReadPageGuard page_g{bpm_->ReadPage(cur_page_id)};
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
    auto backfix_kv{TrySplit(leaf_g_w, backfix_path.Size() == 0)};
    // 注意此处, 不一定要移动到 new sibling,
    // 可能目标 key 仍在当前节点.
    leaf_g_w = GoSibUntilCoverWW(key, std::move(leaf_g_w));
    if (backfix_kv.has_value()) {  // Splited.
      need_backfix = true;
      backfix_key = std::move(backfix_kv.value().first);
      backfix_page_id = backfix_kv.value().second;
    }
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
    while (need_backfix) {
      if (backfix_path.Empty()) {
        // This is the worst path.
        GetPathTo(next_level, backfix_key, backfix_path);
        BUSTUB_ASSERT(!backfix_path.Empty(), "Unexpected.");
      }

      WritePageGuard page_g{bpm_->WritePage(backfix_path.Back())};
      page_g = GoSibUntilCoverWW(backfix_key, std::move(page_g));
      auto page_data{page_g.AsMut<InternalPage>()};
      // 检查 level.
      if (page_data->GetLevel() != next_level) {
        backfix_path.Clear();
        continue;
      }
      next_level++;

      bool is_root_hint{backfix_path.Size() == 1};
      auto new_backfix_kv{XorKv(page_g, backfix_key, backfix_page_id, is_root_hint)};
      if (new_backfix_kv.has_value()) {
        backfix_key = std::move(new_backfix_kv.value().first);
        backfix_page_id = new_backfix_kv.value().second;
      } else {
        need_backfix = false;
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
  auto page_data{page_g.As<InternalPage>()};
  path.EmplaceBack(page_g.GetPageId());
  while (page_data->GetLevel() > to_level) {
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
    page_g = bpm_->ReadPage(page_data->ValueAt(upperbound_idx - 1));
    page_data = page_g.As<InternalPage>();
    path.EmplaceBack(page_g.GetPageId());
  }
}

/**
 * 后期应该通过 template 减少此函数内的 `if`.
 *
 * 注意, 可能需要在 `TrySplit` 之后进行 `GoSib*`.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TrySplit(WritePageGuard &page_g, bool is_root_hint)
    -> std::optional<std::pair<KeyType, page_id_t>> {
  auto page_header_data{page_g.As<BPlusTreePage>()};

  if (!page_header_data->IsOverflowed()) {
    return std::nullopt;
  }

  // 此处先锁 Node 再锁 Tree Header Node.
  bool is_root{};
  WritePageGuard tree_header_g{};
  if (is_root_hint) {
    tree_header_g = bpm_->WritePage(header_page_id_);
    auto tree_header_data{tree_header_g.AsMut<BPlusTreeHeaderPage>()};
    if (tree_header_data->root_page_id_ == page_g.GetPageId()) {
      is_root = true;
      // Keep locking tree header.
    } else {
      tree_header_g.Drop();
    }
  }

  KeyType backfix_key{};
  page_id_t backfix_page_id{INVALID_PAGE_ID};

  if (page_header_data->IsLeafPage()) {  // Leaf
    auto page_data{page_g.AsMut<LeafPage>()};
    page_id_t new_sib_id{bpm_->NewPage()};
    WritePageGuard new_sib_g_w{bpm_->WritePage(new_sib_id)};
    auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
    new_sib_data->Init();
    int sum{page_data->GetSize() + new_sib_data->GetSize()};
    page_data->RedistributeWithRight(new_sib_g_w, (sum + 1) / 2, sum / 2);
    new_sib_data->SetNextPageId(page_data->GetNextPageId());
    page_data->SetNextPageId(new_sib_id);
    backfix_key = new_sib_data->KeyAt(0);
    backfix_page_id = new_sib_id;
    // Is unlocking sibling here legal?
    new_sib_g_w.Drop();
    /**
     * 考虑此处的设计.
     * 此处应 Split 当前节点后, 按需要创建新的 Root,
     * 并直接完成 backfix.
     * 如果留给上层处理,
     * 如果这次 Split 的是 Root layer 中的非 Root 节点,
     * 需要额外的 `GoSib*` 来找到此节点.
     */
    if (is_root) {
      page_id_t new_root_id{bpm_->NewPage()};
      WritePageGuard new_root_g{bpm_->WritePage(new_root_id)};
      auto new_root_data{new_root_g.AsMut<InternalPage>()};
      new_root_data->Init();
      new_root_data->NewKvAt(0);
      new_root_data->NewKvAt(1);
      // `low_key`.
      new_root_data->ModifyKvAt(0, KeyType{}, page_g.GetPageId());
      new_root_data->SetHasValidLowKey(true);
      new_root_data->ModifyKvAt(1, backfix_key, backfix_page_id);
      // Update level.
      new_root_data->SetLevel(page_data->GetLevel() + 1);
      auto tree_header_data{tree_header_g.AsMut<BPlusTreeHeaderPage>()};
      tree_header_data->root_page_id_ = new_root_id;
      /**
       * 注意, 对于 Sibling,
       * 其 backfix kv 已经加入 New Root,
       * backfix 流程结束, 不要返回 kv.
       */
      return std::nullopt;
    }
    return std::make_pair(backfix_key, backfix_page_id);
  } else {  // NOLINT
    auto page_data{page_g.AsMut<InternalPage>()};
    page_id_t new_sib_id{bpm_->NewPage()};
    WritePageGuard new_sib_g_w{bpm_->WritePage(new_sib_id)};
    auto new_sib_data{new_sib_g_w.AsMut<InternalPage>()};
    new_sib_data->Init();
    int sum{page_data->GetSize() + new_sib_data->GetSize()};
    page_data->RedistributeWithRight(new_sib_g_w, (sum + 1) / 2, sum / 2);
    new_sib_data->SetNextPageId(page_data->GetNextPageId());
    page_data->SetNextPageId(new_sib_id);
    backfix_key = new_sib_data->KeyAt(0);
    backfix_page_id = new_sib_id;
    // Is unlocking sibling here legal?
    new_sib_g_w.Drop();
    if (is_root) {
      page_id_t new_root_id{bpm_->NewPage()};
      WritePageGuard new_root_g{bpm_->WritePage(new_root_id)};
      auto new_root_data{new_root_g.AsMut<InternalPage>()};
      new_root_data->Init();
      new_root_data->NewKvAt(0);
      new_root_data->NewKvAt(1);
      new_root_data->ModifyKvAt(0, page_data->KeyAt(0), page_g.GetPageId());
      new_root_data->ModifyKvAt(1, backfix_key, backfix_page_id);
      new_root_data->SetLevel(page_data->GetLevel() + 1);
      auto tree_header_data{tree_header_g.AsMut<BPlusTreeHeaderPage>()};
      tree_header_data->root_page_id_ = new_root_id;
      return std::nullopt;
    }
    return std::make_pair(backfix_key, backfix_page_id);
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryMerge(WritePageGuard &page_g, bool is_root_hint)
    -> std::optional<std::pair<KeyType, page_id_t>> {
  // todo.
  auto page_header_data{page_g.As<BPlusTreePage>()};

  if (!page_header_data->IsUnderflowed()) {
    return std::nullopt;
  }

  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    WritePageGuard sib_g_w{bpm_->WritePage(page_data->GetNextPageId())};
    if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
      return std::nullopt;
    }
    auto sib_data{sib_g_w.AsMut<LeafPage>()};
    auto backfix_kv{std::make_pair(sib_data->KeyAt(0), sib_g_w.GetPageId())};
    int sum{page_data->GetSize() + sib_data->GetSize()};
    page_data->RedistributeWithRight(sib_g_w, sum, 0);
    page_data->SetNextPageId(sib_data->GetNextPageId());
    sib_data->SetNextPageId(page_g.GetPageId());
    qsbr_mgr_.Retire(sib_g_w.GetPageId());
    return backfix_kv;
  } else {  // NOLINT
    auto page_data{page_g.AsMut<LeafPage>()};
    WritePageGuard sib_g_w{bpm_->WritePage(page_data->GetNextPageId())};
    if (page_header_data->IsOverflowedAfterMergeWith(sib_g_w)) {
      return std::nullopt;
    }
    auto sib_data{sib_g_w.AsMut<LeafPage>()};
    auto backfix_kv{std::make_pair(sib_data->KeyAt(0), sib_g_w.GetPageId())};
    int sum{page_data->GetSize() + sib_data->GetSize()};
    page_data->RedistributeWithRight(sib_g_w, sum, 0);
    page_data->SetNextPageId(sib_data->GetNextPageId());
    sib_data->SetNextPageId(page_g.GetPageId());
    qsbr_mgr_.Retire(sib_g_w.GetPageId());
    return backfix_kv;
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryDeleteWhileScan(const KeyType &key, const InternalPageValueType &val, WritePageGuard &page_g)
    -> std::optional<std::pair<KeyType, page_id_t>> {
  // 这个函数用于从 lowerbound 一直扫描到 upperbound node,
  // 中途若遇到匹配的 `<key, val>` 则执行删除并返回.
  // 此函数只可用于 Internal node.
  page_g = GoSibUntilCoverWW(key, std::move(page_g));
  // 如果 `high_key_` 等于 `key` 则意味着下个 node 也可能存在 `key`.
  auto page_header_data{page_g.AsMut<BPlusTreePage>()};
  BUSTUB_ASSERT(page_header_data->IsLeafPage() == false, "Invalid args.");
  auto page_data{page_g.AsMut<InternalPage>()};
  while (true) {
    // 首先, 若已经到达 upperbound 位置则 break.
    if (!page_data->HasValidHighKey() || comparator_(page_data->GetHighKey(), key) > 0) {
      break;
    }
    // 若没有, 尝试找到已存在的 kv, 并删除.
    auto check_idx{page_data->FindKv(key, val, comparator_)};
    if (check_idx == page_data->GetSize()) {
      // 不存在. Go to siblnig.
      page_id_t sib_id{page_data->GetNextPageId()};
      page_g = bpm_->WritePage(sib_id);
      page_data = page_g.AsMut<InternalPage>();
      continue;
    }
    // 存在, 首先尝试 Merge, 并删除.
    // 先删除再 Merge 更好.
    page_data->DeleteKvAt(check_idx);
    /**
     * WARNING: `false` is not correct. Todo.
     */
    return TryMerge(page_g, false);
  }
  return std::nullopt;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::XorKv(WritePageGuard &page_g, const KeyType &key, page_id_t val, bool is_root_hint)
    -> std::optional<std::pair<KeyType, page_id_t>> {
  auto new_backfix_kv{TryDeleteWhileScan(key, val, page_g)};
  if (new_backfix_kv.has_value()) {
    return new_backfix_kv;
  }
  new_backfix_kv = TrySplit(page_g, is_root_hint);
  page_g = GoSibUntilCoverWW(key, std::move(page_g));
  auto page_data{page_g.AsMut<InternalPage>()};
  int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
  page_data->NewKvAt(upperbound_idx);
  page_data->ModifyKvAt(upperbound_idx, key, val);
  return new_backfix_kv;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
