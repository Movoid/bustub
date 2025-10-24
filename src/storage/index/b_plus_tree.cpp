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
#include "storage/page/b_plus_tree_header_page.h"

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
  fmt::println("BPlusTree: leaf_max_size {}, internal_max_size {}.", leaf_max_size, internal_max_size);
}

/**
 * 析构函数.
 */
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::~BPlusTree() {
  /**
   * 不能在此处清理. 此时 BPM 已经被 delete.
   */
  BUSTUB_ASSERT(qsbr_mgr_.GetRetiredCntGlobal() == 0, "Unexpected.");
}

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
  while (true) {
    simple_cu::qsbr::QSBRGuard qsbr_guard{qsbr_mgr_};

    page_id_t cur_root_id{GetRootPageId()};

    if (cur_root_id == INVALID_PAGE_ID) {
      return false;
    }

    PathContainer path{};  // Not neccessary.
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};

    if (!GoDownUntilLevel(page_g, key, LEAF_LEVEL, path)) {
      continue;  // Restart.
    }

    if (!GoSibUntilCoverRR(page_g, key)) {
      continue;
    }

    BUSTUB_ASSERT(page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
    auto page_data{page_g.As<LeafPage>()};
    int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
    if (lowerbound_idx == page_data->GetSize() || comparator_(page_data->KeyAt(lowerbound_idx), key) != 0) {
      return false;
    }

    if (page_data->ValueAt(lowerbound_idx) == RID{INVALID_PAGE_ID, 0}) {
      return false;
    }

    result->emplace_back(page_data->ValueAt(lowerbound_idx));
    return true;
  }
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
 * 必须先 Split 再 NewKv .
 * 若先 NewKv 再 Split, 可能在 NewKv 之后需要再次 Split.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  while (true) {
    simple_cu::qsbr::QSBRGuard qsbr_guard{qsbr_mgr_};

    page_id_t cur_root_id{GetRootPageId()};

    if (cur_root_id == INVALID_PAGE_ID) {
      TryExpandLevel(cur_root_id);
      continue;  // Restart.
    }
    PathContainer backfix_path{};
    IndexKvPack backfix_kv{std::nullopt};

    // Locate.
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};

    if (!GoDownUntilLevel(page_g, key, LEAF_LEVEL, backfix_path)) {
      continue;
    }
    BUSTUB_ASSERT(backfix_path.Size() >= 1, "Unexpected.");
    BUSTUB_ASSERT(backfix_path.Back() == page_g.GetPageId(), "Unexpected.");
    backfix_path.PopBack();

    page_id_t tmp{page_g.GetPageId()};
    page_g.Drop();
    WritePageGuard leaf_g_w{bpm_->WritePage(tmp)};
    if (!GoSibUntilCoverWW(leaf_g_w, key)) {
      continue;
    }

    bool locate_ok{true};
    // Split.
    page_id_t last_leaf_id{leaf_g_w.GetPageId()};
    while (TrySplitAndGo(leaf_g_w, key, backfix_kv)) {
      last_leaf_id = leaf_g_w.GetPageId();
      leaf_g_w.Drop();

      // Drop and start backfix.
      PathContainer cur_backfix_path{backfix_path};
      int next_level{LEAF_LEVEL + 1};

      while (backfix_kv.has_value()) {
        auto &[backfix_key, backfix_val, ign1, ign2]{backfix_kv.value()};
        if (cur_backfix_path.Empty()) {
          // Check backfix status.
          {
            ReadPageGuard check_page_g{bpm_->ReadPage(backfix_val)};
            auto check_page_header_data{check_page_g.As<BPlusTreePage>()};
            if (check_page_header_data->GetBackfixStatus() != BackfixStatus::NEED_LINK) {
              backfix_kv = std::nullopt;
              break;
            }
          }
          TryExpandLevel(cur_root_id);
          if (!TryGetPathTo(backfix_key, next_level, cur_backfix_path)) {
            cur_backfix_path.Clear();
            continue;
          }
          BUSTUB_ASSERT(!cur_backfix_path.Empty(), "Unexpected.");
        }
        {
          WritePageGuard page_g_w{bpm_->WritePage(cur_backfix_path.Back())};
          /**
           * `backfix_path` 中的路径可能已经失效.
           */
          if (!GoSibUntilCoverWW(page_g_w, backfix_key)) {
            cur_backfix_path.Clear();
            continue;
          }
          auto page_header_data{page_g_w.As<BPlusTreePage>()};
          BUSTUB_ASSERT(!page_header_data->IsLeafPage(), "Unexpected.");
          BUSTUB_ASSERT(page_header_data->GetLevel() == next_level, "Unexpected.");
          auto page_data{page_g_w.AsMut<InternalPage>()};
          BUSTUB_ASSERT(page_data->CoveredKey(backfix_key, comparator_), "Unexpected.");
          BackfixInsertKv(page_g_w, backfix_kv);
        }
        next_level++;
        cur_backfix_path.PopBack();
      }
      leaf_g_w = bpm_->WritePage(last_leaf_id);  // Go back to leaf.
      if (!GoSibUntilCoverWW(leaf_g_w, key)) {
        /**
         * Locate 失败, 必须重启 `Insert`.
         */
        locate_ok = false;
        break;
      }
      last_leaf_id = leaf_g_w.GetPageId();  // Record.
    }

    if (!locate_ok) {
      continue;
    }

    auto leaf_data{leaf_g_w.AsMut<LeafPage>()};
    BUSTUB_ASSERT(!leaf_data->IsOverflowed(), "Unexpected.");
    BUSTUB_ASSERT(leaf_data->CoveredKey(key, comparator_), "Unexpected.");
    // Insert.
    int upperbound_idx{leaf_data->FindIndexUpperbound(key, comparator_)};

    // BUSTUB_ASSERT(page_data->IsLevelHead() || upperbound_idx > 0, "Unexpected.");
    BUSTUB_ASSERT(leaf_data->IsLevelHead() || comparator_(leaf_data->GetLowKey(), key) <= 0, "Unexpected.");
    BUSTUB_ASSERT(leaf_data->GetNextPageId() == INVALID_PAGE_ID || comparator_(key, leaf_data->GetHighKey()) < 0,
                  "Unexpected.");

    // 查重
    if (upperbound_idx > 0 && comparator_(leaf_data->KeyAt(upperbound_idx - 1), key) == 0) {
      // 复用 tombstone
      if (leaf_data->ValueAt(upperbound_idx - 1) == RID{INVALID_PAGE_ID, 0}) {
        leaf_data->ModifyKvAt(upperbound_idx - 1, key, value);
        leaf_data->SetInvalidSize(leaf_data->GetInvalidSize() - 1);
        return true;
      }
      return false;
    }
    leaf_data->NewKvAt(upperbound_idx);
    leaf_data->ModifyKvAt(upperbound_idx, key, value);

    return true;
  }
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
 * 先 RemoveKv 再 Merge.
 * 否则在 Merge 后 RemoveKv 可能需要再次 Merge.
 *
 * @param key input key
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  while (true) {
    simple_cu::qsbr::QSBRGuard qsbr_guard{qsbr_mgr_};

    page_id_t cur_root_id{GetRootPageId()};
    if (cur_root_id == INVALID_PAGE_ID) {
      return;
    }

    PathContainer backfix_path{};
    IndexKvPack backfix_kv{std::nullopt};
    bool try_delete_root_hint{};
    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};

    if (!GoDownUntilLevel(page_g, key, LEAF_LEVEL, backfix_path)) {
      continue;
    }

    BUSTUB_ASSERT(backfix_path.Size() >= 1, "Unexpected.");
    BUSTUB_ASSERT(backfix_path.Back() == page_g.GetPageId(), "Unexpected.");
    backfix_path.PopBack();
    page_id_t tmp{page_g.GetPageId()};
    page_g.Drop();
    {
      WritePageGuard leaf_g_w{bpm_->WritePage(tmp)};
      if (!GoSibUntilCoverWW(leaf_g_w, key)) {
        continue;
      }

      auto leaf_data{leaf_g_w.AsMut<LeafPage>()};

      BUSTUB_ASSERT(leaf_data->CoveredKey(key, comparator_), "Unexpected.");

      int lowerbound_idx{leaf_data->FindIndexLowerbound(key, comparator_)};
      if (lowerbound_idx == leaf_data->GetSize() || comparator_(leaf_data->KeyAt(lowerbound_idx), key) != 0) {
        return;
      }

      if (leaf_data->ValueAt(lowerbound_idx) == RID{INVALID_PAGE_ID, 0}) {
        return;
      }
      leaf_data->ModifyKvAt(lowerbound_idx, leaf_data->KeyAt(lowerbound_idx), RID{INVALID_PAGE_ID, 0});
      leaf_data->SetInvalidSize(leaf_data->GetInvalidSize() + 1);

      try_delete_root_hint |=
          leaf_data->GetValidSize() == 0 && leaf_data->IsLevelHead() && leaf_data->GetNextPageId() == INVALID_PAGE_ID;

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
          while (cur_backfix_path.Empty()) {
            if (TryGetPathTo(backfix_key, next_level, cur_backfix_path)) {
              break;
            }
          }
          BUSTUB_ASSERT(!cur_backfix_path.Empty(), "Unexpected.");
          {
            WritePageGuard page_g{bpm_->WritePage(cur_backfix_path.Back())};
            BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
            /**
             * node1 发生 MergeToRight 并生成 `backfix_kv`,
             * 只能确定 node1 parent 的 Level 应该存在 `NEED_UNLINK` 的 kv,
             * 但更高 Level 不确定.
             */
            if (!GoSibUntilCoverWW(page_g, backfix_key)) {
              cur_backfix_path.Clear();
              continue;
              // BUSTUB_ASSERT(false, "Unexpected.");
            }
            auto page_data{page_g.As<InternalPage>()};
            BUSTUB_ASSERT(page_data->CoveredKey(backfix_key, comparator_), "Unexpected.");
            BackfixRemoveKv(page_g, backfix_kv);
            page_data = page_g.As<InternalPage>();
            try_delete_root_hint |= leaf_data->GetValidSize() == 0 && page_data->IsLevelHead() &&
                                    page_data->GetNextPageId() == INVALID_PAGE_ID;
          }
          next_level++;
          cur_backfix_path.PopBack();
        }

        leaf_g_w = bpm_->WritePage(last_leaf_id);
        /**
         * 应该一直走到 Level Head.
         */
        if (!GoSibUntilHeadWW(leaf_g_w)) {
          break;
        }
        last_leaf_id = leaf_g_w.GetPageId();
      }
    }

    // 这里应该让 TryDeleteRoot 自己寻找 root id.
    if (try_delete_root_hint) {
      while (cur_root_id != INVALID_PAGE_ID && TryShrinkLevel(cur_root_id)) {
        ;
      }
    }

    return;
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
  while (true) {
    simple_cu::qsbr::QSBRGuard qsbr_guard{qsbr_mgr_};

    page_id_t cur_root_id{GetRootPageId()};
    if (cur_root_id == INVALID_PAGE_ID) {
      return INDEXITERATOR_TYPE{std::nullopt, bpm_};
    }

    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};

    if (!GoDownUntilLeafHead(page_g)) {
      continue;
    }

    auto page_header_data{page_g.As<BPlusTreePage>()};
    auto page_data{page_g.As<LeafPage>()};
    BUSTUB_ASSERT(page_header_data->IsLevelHead(), "Unexpected.");
    BUSTUB_ASSERT(page_header_data->GetBackfixStatus() == BackfixStatus::IS_LINKED, "Protocol error.");
    BUSTUB_ASSERT(page_header_data->GetSize() - page_header_data->GetInvalidSize() > 0, "Unexpected.");

    int offset{0};
    while (true) {
      while (offset < page_data->GetSize() && page_data->ValueAt(offset) == RID{INVALID_PAGE_ID, 0}) {
        offset++;
      }
      if (offset == page_data->GetSize() && page_data->GetNextPageId() != INVALID_PAGE_ID) {
        page_id_t sib_id{page_data->GetNextPageId()};
        page_g.Drop();
        page_g = bpm_->ReadPage(sib_id);
        page_data = page_g.As<LeafPage>();
        offset = 0;
        continue;
      }
      break;
    }

    if (offset == page_data->GetSize()) {
      return INDEXITERATOR_TYPE{std::nullopt, bpm_};
    }

    return INDEXITERATOR_TYPE{std::move(page_g), offset, bpm_};
  }
}

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  while (true) {
    simple_cu::qsbr::QSBRGuard qsbr_guard{qsbr_mgr_};

    page_id_t cur_root_id{GetRootPageId()};

    if (cur_root_id == INVALID_PAGE_ID) {
      return INDEXITERATOR_TYPE{std::nullopt, bpm_};
    }

    PathContainer path{};

    ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
    if (!GoDownUntilLevel(page_g, key, LEAF_LEVEL, path)) {
      continue;
    }

    auto page_header_data{page_g.As<BPlusTreePage>()};
    BUSTUB_ASSERT(page_header_data->IsLeafPage(), "Unexpected.");
    auto page_data{page_g.As<LeafPage>()};
    int lowerbound_idx{page_data->FindIndexLowerbound(key, comparator_)};
    BUSTUB_ASSERT(lowerbound_idx < page_data->GetSize(), "Invalid args.");
    BUSTUB_ASSERT(comparator_(page_data->KeyAt(lowerbound_idx), key) == 0, "Invalid args.");
    return INDEXITERATOR_TYPE{std::move(page_g), lowerbound_idx, bpm_};
  }
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
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverRR(ReadPageGuard &page_g, const KeyType &key) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  while (true) {
    if (page_header_data->IsLeafPage()) {
      auto page_data{page_g.As<LeafPage>()};
      if (page_data->CoveredKey(key, comparator_)) {
        break;
      }
    } else {
      auto page_data{page_g.As<InternalPage>()};
      if (page_data->CoveredKey(key, comparator_)) {
        break;
      }
    }
    page_id_t next_id{page_header_data->GetNextPageId()};
    if (next_id == INVALID_PAGE_ID) {
      BUSTUB_ASSERT(page_header_data->GetBackfixStatus() == BackfixStatus::IS_UNLINKED, "Protocol error.");
      return false;
    }
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
    page_header_data = page_g.As<BPlusTreePage>();
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverWW(WritePageGuard &page_g, const KeyType &key) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  while (true) {
    if (page_header_data->IsLeafPage()) {
      auto page_data{page_g.As<LeafPage>()};
      if (page_data->CoveredKey(key, comparator_)) {
        break;
      }
    } else {
      auto page_data{page_g.As<InternalPage>()};
      if (page_data->CoveredKey(key, comparator_)) {
        break;
      }
    }
    page_id_t next_id{page_header_data->GetNextPageId()};
    if (next_id == INVALID_PAGE_ID) {
      BUSTUB_ASSERT(page_header_data->GetBackfixStatus() == BackfixStatus::IS_UNLINKED, "Protocol error.");
      return false;
    }
    page_g.Drop();
    page_g = bpm_->WritePage(next_id);
    page_header_data = page_g.As<BPlusTreePage>();
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilCoverRW(ReadPageGuard &&page_g, WritePageGuard &page_g_w, const KeyType &key) -> bool {
  if (!GoSibUntilCoverRR(page_g, key)) {
    return false;
  }
  page_id_t cur_page_id{page_g.GetPageId()};
  page_g.Drop();
  page_g_w = bpm_->WritePage(cur_page_id);
  return GoSibUntilCoverWW(page_g_w, key);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilHeadRR(ReadPageGuard &page_g) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  while (!page_header_data->IsLevelHead() && page_header_data->GetNextPageId() != INVALID_PAGE_ID) {
    page_id_t sib_id{page_header_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->ReadPage(sib_id);
    page_header_data = page_g.As<BPlusTreePage>();
  }
  return page_header_data->IsLevelHead() && page_header_data->GetBackfixStatus() == BackfixStatus::IS_LINKED;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoSibUntilHeadWW(WritePageGuard &page_g) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  while (!page_header_data->IsLevelHead() && page_header_data->GetNextPageId() != INVALID_PAGE_ID) {
    page_id_t sib_id{page_header_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->WritePage(sib_id);
    page_header_data = page_g.As<BPlusTreePage>();
  }
  return page_header_data->IsLevelHead() && page_header_data->GetBackfixStatus() == BackfixStatus::IS_LINKED;
}

/**
 * 需要能处理 `page_g` `IS_UNLINKED` 的情况.
 * 最后 `page_g` 应该到达 `to_level` Level.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoDownUntilLevel(ReadPageGuard &page_g, const KeyType &key, int to_level, PathContainer &path)
    -> bool {
  BUSTUB_ASSERT(to_level >= LEAF_LEVEL, "Invalid args.");
  BUSTUB_ASSERT(path.Empty(), "Invalid args.");
  if (!GoSibUntilCoverRR(page_g, key)) {
    path.Clear();
    return false;
  }
  auto page_header_data{page_g.As<BPlusTreePage>()};

  if (page_header_data->GetLevel() < to_level) {
    path.Clear();
    return false;
  }

  while (page_header_data->GetLevel() > to_level) {
    path.EmplaceBack(page_g.GetPageId());
    auto page_data{page_g.As<InternalPage>()};
    page_id_t next_id{page_data->Locate(key, comparator_)};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
    if (!GoSibUntilCoverRR(page_g, key)) {
      path.Clear();
      return false;
    }
    page_header_data = page_g.As<BPlusTreePage>();
  }
  BUSTUB_ASSERT(page_header_data->GetLevel() == to_level, "Unexpected.");
  path.EmplaceBack(page_g.GetPageId());
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GoDownUntilLeafHead(ReadPageGuard &page_g) -> bool {
  if (!GoSibUntilHeadRR(page_g)) {
    return false;
  }
  auto page_header_data{page_g.As<BPlusTreePage>()};
  BUSTUB_ASSERT(page_header_data->IsLevelHead(), "Unexpected.");
  while (!page_header_data->IsLeafPage()) {
    auto page_data{page_g.As<InternalPage>()};
    page_id_t next_id{page_data->GetLowerlevelHeadId()};
    page_g.Drop();
    page_g = bpm_->ReadPage(next_id);
    if (!GoSibUntilHeadRR(page_g)) {
      return false;
    }
    BUSTUB_ASSERT(page_header_data->IsLevelHead(), "Unexpected.");
    page_header_data = page_g.As<BPlusTreePage>();
  }
  BUSTUB_ASSERT(page_header_data->IsLeafPage(), "Unexpected.");
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryGetPathTo(const KeyType &key, int to_level, PathContainer &path) -> bool {
  page_id_t cur_root_id{GetRootPageId()};
  if (cur_root_id == INVALID_PAGE_ID) {
    path.Clear();
    return false;
  }
  ReadPageGuard page_g{bpm_->ReadPage(cur_root_id)};
  if (!GoDownUntilLevel(page_g, key, to_level, path)) {
    path.Clear();
    return false;
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CleanTombstone(WritePageGuard &page_g) {
  auto page_header_data{page_g.AsMut<BPlusTreePage>()};
  int size{page_header_data->GetSize()};
  int tomb_size{page_header_data->GetInvalidSize()};

  if (tomb_size == 0) {
    return;
  }

  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    for (int i = size; i > 0; i--) {
      if (page_data->ValueAt(i - 1) == RID{INVALID_PAGE_ID, 0}) {
        page_data->DeleteKvAt(i - 1);
        tomb_size--;
      }
    }
  } else {
    auto page_data{page_g.AsMut<InternalPage>()};
    for (int i = size; i > 0; i--) {
      if (page_data->ValueAt(i - 1) == INVALID_PAGE_ID) {
        page_data->DeleteKvAt(i - 1);
        tomb_size--;
      }
    }
  }
  BUSTUB_ASSERT(tomb_size == 0, "Unexpected.");
  page_header_data->SetInvalidSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TrySplitAndGo(WritePageGuard &page_g, const KeyType &key, IndexKvPack &backfix_kv) -> bool {
  auto page_header_data{page_g.As<BPlusTreePage>()};
  auto page_status{page_header_data->GetBackfixStatus()};

  BUSTUB_ASSERT(page_status == BackfixStatus::IS_LINKED || page_status == BackfixStatus::NEED_LINK, "Protocol error.");

  if (!page_header_data->IsOverflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }

  /**
   * 先清理 `page_g` 中的 tombstone.
   */
  CleanTombstone(page_g);
  if (!page_header_data->IsOverflowed()) {
    backfix_kv = std::nullopt;
    return false;
  }

  page_id_t new_sib_id{bpm_->NewPage()};
  WritePageGuard new_sib_g_w{bpm_->WritePage(new_sib_id)};
  auto new_sib_header_data{new_sib_g_w.AsMut<BPlusTreePage>()};

  if (page_header_data->IsLeafPage()) {
    auto page_data{page_g.AsMut<LeafPage>()};
    auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
    new_sib_data->Init(leaf_max_size_);
    // page_data->RedistributeWithRight(new_sib_g_w);
    page_data->SplitToRight(new_sib_g_w);
    page_data->LinkToNewSibling(new_sib_g_w);
    backfix_kv = {new_sib_data->GetLowKey(), new_sib_g_w.GetPageId(), false, INVALID_PAGE_ID};
    new_sib_header_data->SetBackfixStatus(BackfixStatus::NEED_LINK);
    if (!page_data->CoveredKey(key, comparator_)) {
      page_g = std::move(new_sib_g_w);
    }
  } else {
    auto page_data{page_g.AsMut<InternalPage>()};
    auto new_sib_data{new_sib_g_w.AsMut<InternalPage>()};
    new_sib_data->Init(internal_max_size_);
    page_data->SplitToRight(new_sib_g_w);
    page_data->LinkToNewSibling(new_sib_g_w);
    backfix_kv = {new_sib_data->GetLowKey(), new_sib_g_w.GetPageId(), false, INVALID_PAGE_ID};
    new_sib_header_data->SetBackfixStatus(BackfixStatus::NEED_LINK);
    if (!page_data->CoveredKey(key, comparator_)) {
      page_g = std::move(new_sib_g_w);
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

  /**
   * 考虑在 Optimistically 到达 `page_g` 前, 有另一个线程先到达 `page_g` 并执行操作,
   * `page_g` 可能是 `IS_UNLINKED`.
   */

  auto page_status{page_header_data->GetBackfixStatus()};

  BUSTUB_ASSERT(page_status == BackfixStatus::IS_LINKED || page_status == BackfixStatus::NEED_LINK, "Protocol error.");

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

  switch (sib_status) {
    case BackfixStatus::NEED_LINK:
      /**
       * 向左合并并撤销 Sibling 的 `NEED_LINK` 为 `IS_UNLINKED`.
       */
      if (page_header_data->IsLeafPage()) {
        auto page_data{page_g.AsMut<LeafPage>()};
        auto sib_data{sib_g_w.AsMut<LeafPage>()};
        CleanTombstone(page_g);
        CleanTombstone(sib_g_w);
        page_data->MergeFromRight(sib_g_w);
        // 手动调整 Link.
        page_header_data->SetNextPageId(sib_header_data->GetNextPageId());
        if (sib_header_data->GetNextPageId() != INVALID_PAGE_ID) {
          page_data->SetHighKey(sib_data->GetHighKey());
        }
      } else {
        auto page_data{page_g.AsMut<InternalPage>()};
        auto sib_data{sib_g_w.AsMut<InternalPage>()};
        page_data->MergeFromRight(sib_g_w);
        page_header_data->SetNextPageId(sib_header_data->GetNextPageId());
        if (sib_header_data->GetNextPageId() != INVALID_PAGE_ID) {
          page_data->SetHighKey(sib_data->GetHighKey());
        }
      }
      sib_header_data->SetNextPageId(page_g.GetPageId());

      sib_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
      qsbr_mgr_.Retire(sib_g_w.GetPageId());
      backfix_kv = std::nullopt;
      break;

    case BackfixStatus::NEED_UNLINK:
      // 在 BackfixRemoveKv 里 Retire.
    case BackfixStatus::IS_UNLINKED:
      BUSTUB_ASSERT((sib_header_data->IsLeafPage() && sib_header_data->GetSize() == 0) ||
                        (!sib_header_data->IsLeafPage() && sib_header_data->GetSize() == 0),
                    "Unexpected.");
      /**
       * Sibling 从 Sibling Link 上删除才彻底从 Tree 上消失.
       * 对于 `NEED_UNLINK`, 此处 `Retire`, 在对应线程 `BackfixRemoveKv` 流程结束后才会真正被删除.
       * 对于 `IS_UNLINKED`, 此处可以直接删除.
       */
      qsbr_mgr_.Retire(sib_g_w.GetPageId());
      page_header_data->SetNextPageId(sib_header_data->GetNextPageId());
      backfix_kv = std::nullopt;
      break;

    case BackfixStatus::IS_LINKED:

      // 向右合并.
      if (!page_header_data->IsLevelHead()) {
        backfix_kv = std::nullopt;
        return false;
      }

      /**
       * 允许向右合并.
       * 对于 Level Head, 将 Level Head 的资格转移给 Sibling, 并
       * 在 Sibling 的 parent 中删除 `lowkey_` kv 并更新 `lowerlevel_head_id_`.
       *
       * 对于非 Level Head, 直接在 parent 中删除 kv.
       * > 若在这种情况 `BackfixRemoveKv` 完成之前, parent 转变为 Level Head,
       * > 应该不影响正确性, 因为此时 parent 的 `kv[0]` 来自先前的 Level Head,
       * > 没有覆盖本该删除的 Kv.
       */

      BUSTUB_ASSERT(page_header_data->GetBackfixStatus() == BackfixStatus::IS_LINKED, "Protocol error.");

      if (page_header_data->IsLeafPage()) {
        auto page_data{page_g.AsMut<LeafPage>()};
        auto sib_data{sib_g_w.AsMut<LeafPage>()};
        CleanTombstone(page_g);
        CleanTombstone(sib_g_w);
        page_data->MergeToRight(sib_g_w);
        /**
         * 逻辑上, placeholder `key` 没有删除.
         */
        backfix_kv = {sib_data->GetLowKey(), page_g.GetPageId(), true, sib_g_w.GetPageId()};
      } else {
        auto page_data{page_g.AsMut<InternalPage>()};
        auto sib_data{sib_g_w.AsMut<InternalPage>()};

        page_id_t old_lowerlevel_head_id{page_data->GetLowerlevelHeadId()};
        BUSTUB_ASSERT(old_lowerlevel_head_id != INVALID_PAGE_ID, "Unexpected.");
        BUSTUB_ASSERT(page_data->GetSize() > 0, "Unexpected.");
        if (sib_data->GetLowerlevelHeadId() == INVALID_PAGE_ID) {
          sib_data->SetLowerlevelHeadId(old_lowerlevel_head_id);
        } else {
          qsbr_mgr_.Retire(std::move(old_lowerlevel_head_id));  // NOLINT
          page_data->ModifyKvAt(0, KeyType{}, sib_data->GetLowerlevelHeadId());
        }
        page_data->MergeToRight(sib_g_w);

        backfix_kv = {sib_data->GetLowKey(), page_g.GetPageId(), true, sib_g_w.GetPageId()};
      }

      page_header_data->SetIsLevelHead(false);
      sib_header_data->SetIsLevelHead(true);
      page_header_data->SetBackfixStatus(BackfixStatus::NEED_UNLINK);

      BUSTUB_ASSERT(
          !page_header_data->IsLevelHead() && page_header_data->GetBackfixStatus() == BackfixStatus::NEED_UNLINK,
          "Unexpected.");
      page_g = std::move(sib_g_w);
      break;

    default:
      BUSTUB_ASSERT(false, "Unexpected.");
  };

  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryExpandLevel(page_id_t &new_root_id) -> bool {
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
      // desired_root_data->SetLowLevelHeadId(INVALID_PAGE_ID);
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
        desired_root_data->SetLowerlevelHeadId(expected_root_g.GetPageId());
        desired_root_data->NewKvAt(0);
        desired_root_data->ModifyKvAt(0, KeyType{}, expected_root_g.GetPageId());
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
auto BPLUSTREE_TYPE::TryShrinkLevel(page_id_t &new_root_id) -> bool {
  page_id_t expected_root_id{GetRootPageId()};

  if (expected_root_id == INVALID_PAGE_ID) {
    new_root_id = INVALID_PAGE_ID;
    return false;
  }

  {
    WritePageGuard expected_root_g{bpm_->WritePage(expected_root_id)};
    auto expected_root_header_data{expected_root_g.AsMut<BPlusTreePage>()};
    if (expected_root_header_data->GetBackfixStatus() != BackfixStatus::IS_LINKED ||
        !expected_root_header_data->IsLevelHead() || expected_root_header_data->GetNextPageId() != INVALID_PAGE_ID) {
      expected_root_g.Drop();
      new_root_id = GetRootPageId();
      return false;
    }

    if ((expected_root_header_data->IsLeafPage() && expected_root_header_data->GetValidSize() != 0) ||
        (!expected_root_header_data->IsLeafPage() && expected_root_header_data->GetValidSize() != 1)) {
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
      if (expected_root_header_data->IsLeafPage()) {
        tree_header_data->root_page_id_ = INVALID_PAGE_ID;
        new_root_id = INVALID_PAGE_ID;
      } else {
        auto expected_root_data{expected_root_g.As<InternalPage>()};
        /**
         * 使用 `GetLowerlevelHeadId()`.
         */
        tree_header_data->root_page_id_ = expected_root_data->GetLowerlevelHeadId();
        new_root_id = expected_root_data->GetLowerlevelHeadId();
      }
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

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BackfixInsertKv(WritePageGuard &page_g, IndexKvPack &backfix_kv) -> bool {
  auto &[backfix_key, backfix_val, ign1, ign2]{backfix_kv.value()};
  BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Invalid args.");
  auto page_data{page_g.AsMut<InternalPage>()};
  BUSTUB_ASSERT(page_data->CoveredKey(backfix_key, comparator_), "Unexpected.");

  IndexKvPack new_backfix_kv{std::nullopt};

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
  auto &[backfix_key, backfix_val, update_lowerlevel_head, new_lowerlevel_head_id]{backfix_kv.value()};
  BUSTUB_ASSERT(!page_g.As<BPlusTreePage>()->IsLeafPage(), "Invalid args.");
  BUSTUB_ASSERT(update_lowerlevel_head, "Only support this operation.");

  auto page_header_data{page_g.As<BPlusTreePage>()};
  auto page_data{page_g.AsMut<InternalPage>()};

  BUSTUB_ASSERT(page_data->CoveredKey(backfix_key, comparator_), "Unexpected.");
  /**
   * 不进行常规的 `DeleteKv`, 而是覆盖 `kv[0]`.
   * 本质上是因为 Level Head 的 `lowkey_` 大小无意义, 不能进行常规查找删除.
   *
   * > 或者说, Level Head 逻辑上, 从来没有被删除, 其和其 parent 中的 Kv 只是在向右转移.
   */

  IndexKvPack new_backfix_kv{std::nullopt};
  page_header_data = page_g.AsMut<BPlusTreePage>();
  page_data = page_g.AsMut<InternalPage>();

  int lowerbound_idx{page_data->FindIndexLowerbound(backfix_key, comparator_)};
  int upperbound_idx{page_data->FindIndexUpperbound(backfix_key, comparator_)};
  if (upperbound_idx - lowerbound_idx > 1) {
    for (; lowerbound_idx < upperbound_idx; lowerbound_idx++) {
      if (page_data->ValueAt(lowerbound_idx) == new_lowerlevel_head_id) {
        break;
      }
    }
  }
  while (lowerbound_idx == upperbound_idx) {
    page_id_t sib_id{page_header_data->GetNextPageId()};
    page_g.Drop();
    page_g = bpm_->WritePage(sib_id);
    if (!GoSibUntilCoverWW(page_g, backfix_key)) {
      BUSTUB_ASSERT(false, "Unexpected.");
    }
    page_header_data = page_g.AsMut<BPlusTreePage>();
    page_data = page_g.AsMut<InternalPage>();
    lowerbound_idx = page_data->FindIndexLowerbound(backfix_key, comparator_);
    upperbound_idx = page_data->FindIndexUpperbound(backfix_key, comparator_);
    if (upperbound_idx - lowerbound_idx > 1) {
      for (; lowerbound_idx < upperbound_idx; lowerbound_idx++) {
        if (page_data->ValueAt(lowerbound_idx) == new_lowerlevel_head_id) {
          break;
        }
      }
    }
  }

  BUSTUB_ASSERT(comparator_(page_data->KeyAt(lowerbound_idx), backfix_key) == 0, "Unexpected.");
  BUSTUB_ASSERT(page_data->ValueAt(lowerbound_idx) == new_lowerlevel_head_id, "Unexpected.");

  page_data->DeleteKvAt(lowerbound_idx);

  {
    WritePageGuard check_page_g{bpm_->WritePage(new_lowerlevel_head_id)};
    auto check_page_header_data{check_page_g.AsMut<BPlusTreePage>()};
    if (check_page_header_data->IsLevelHead()) {
      page_id_t old_lowerlevel_head_id{page_data->GetLowerlevelHeadId()};
      /**
       * 允许 `page_g` 不是 Level Head,
       * 这通常发生在 node1 MergeToRight node2 且 node1, node2 不属于同一个 parent 时,
       * node2 parent 尚未成为 Level Head.
       */
      BUSTUB_ASSERT(!page_data->IsLevelHead() || old_lowerlevel_head_id != INVALID_PAGE_ID, "Unexpected.");
      if (old_lowerlevel_head_id != INVALID_PAGE_ID) {
        {
          WritePageGuard unlink_page_g{bpm_->WritePage(old_lowerlevel_head_id)};
          auto unlink_page_header_data{unlink_page_g.AsMut<BPlusTreePage>()};
          BUSTUB_ASSERT(unlink_page_header_data->GetBackfixStatus() == BackfixStatus::NEED_UNLINK, "Protocol error.");
          unlink_page_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
        }
        /**
         * 不要 Retire.
         * 所有节点被 parent 通过 Kv, 或者 leaf sibling 通过 Sibling Link 连接,
         * 其中 Sibling Link 是最晚断开的, 在将节点从 Sibling Link 中删除时进行 Retire.
         */
      }
      page_data->SetLowerlevelHeadId(new_lowerlevel_head_id);
      if (page_data->IsLevelHead()) {
        page_data->ModifyKvAt(0, KeyType{}, new_lowerlevel_head_id);
      }
    } else {
      BUSTUB_ASSERT(check_page_header_data->GetBackfixStatus() == BackfixStatus::NEED_UNLINK, "Protocol error.");
      check_page_header_data->SetBackfixStatus(BackfixStatus::IS_UNLINKED);
    }
  }

  TryMergeAndGo(page_g, new_backfix_kv);
  backfix_kv = std::move(new_backfix_kv);
  return true;

  /**
   * 如果 `backfix_key_opt` 是 `std::nullopt`,
   * 我们需要用新的 id 更新 Level Head 的 `val[0]`.
   *
   * 考虑将 node1 向右 Merge 到 node2, node1 是 Level Head,
   * 需要用 node2 id 更新 node1 parent 的 `val[0]` 和 `lowerlevel_head_id_`.
   *
   * > 如果 node1 parent 不是 Level Head,
   * > 也直接更新其 `val[0]` 和 `lowerlevel_head_id_`.
   * > 因为其 `val[0]` 应该是已过时的 Level Head.
   *
   * > Level Head 的 `lowkey_` 实际有意义, 其在 parent node 中占据一个位置.
   * > 但其 key 的大小没有意义.
   *
   * 在 node1 parent 向右 Merge 到 node2 parent 时,
   * 必须检查 node1 持有的 `lowerlevel_head_id_` node 是否仍是有效的 Level Head.
   * 若是, 则修改 node2 parent 的 `val[0]` 和 `lowerlevel_head_id_`.
   * 否则 no-op.
   */
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
