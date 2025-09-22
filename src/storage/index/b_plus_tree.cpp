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
  header_data->root_page_id_ = bpm_->NewPage();
  // LC: Lock child.
  auto root_page_g{bpm_->WritePage(header_data->root_page_id_)};
  // LC: Unlock parent.
  header_page_g.Drop();

  root_page_g.AsMut<LeafPage>()->Init();

  // LC: Unlock child.
  root_page_g.Drop();
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
    BUSTUB_ASSERT(upperbound_idx > 0, "All keys in this node are above `key`? Invariant broken.");
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

  meow_utils::SmallVector<page_id_t, 8> path{};

  qsbr_mgr_.EnterCriticalZone();

  // Phase 1.
  while (true) {
    ReadPageGuard page_g{bpm_->ReadPage(cur_page_id)};
    auto page_header_data{page_g.As<BPlusTreePage>()};
    if (page_header_data->IsLeafPage()) {
      WritePageGuard page_g_w{GoSibUntilCoverRW(key, std::move(page_g))};
      // Now have write access to leaf.
      // Split leaf?
      while (true) {
        auto page_data{page_g_w.AsMut<LeafPage>()};
        if (page_data->IsOverflowed()) {
          // Split.
          // Create a new node as new sibling.
          page_id_t new_sib_id{bpm_->NewPage()};
          WritePageGuard new_sib_g_w{bpm_->WritePage(new_sib_id)};
          auto new_sib_data{new_sib_g_w.AsMut<LeafPage>()};
          new_sib_data->Init();  // Remember to init.
          int sum{page_data->GetSize() + new_sib_data->GetSize()};
          page_data->RedistributeWithRight(new_sib_g_w, (sum + 1) / 2, sum / 2);

          // todo

        } else {
          break;
        }
      }
      path.EmplaceBack(page_g_w.GetPageId());
      // Do some **REAL** modifications.
    }
    // Is internal node. Go down.
    page_g = GoSibUntilCoverRR(key, std::move(page_g));
    path.EmplaceBack(page_g.GetPageId());
    auto page_data{page_g.As<InternalPage>()};
    int upperbound_idx{page_data->FindIndexUpperbound(key, comparator_)};
    BUSTUB_ASSERT(upperbound_idx > 0, "All keys in this node are above `key`? Invariant broken.");
    cur_page_id = page_data->ValueAt(upperbound_idx - 1);
  }

  // Phase 2.
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
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

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
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { UNIMPLEMENTED("TODO(P2): Add implementation."); }

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

/**
 * 辅助函数.
 * 在走到最右侧 Sibling 时必须停止,
 * 最右侧的 Sibling 其 `high_key_` 应该表示 +inf.
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

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
