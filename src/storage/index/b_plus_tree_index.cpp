//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_index.cpp
//
// Identification: src/storage/index/b_plus_tree_index.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree_index.h"

namespace bustub {
/**
 * Constructor
 *
 * BPlusTreeIndex 封装了 BPlusTree 的接口.
 */
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_INDEX_TYPE::BPlusTreeIndex(std::unique_ptr<IndexMetadata> &&metadata, BufferPoolManager *buffer_pool_manager)
    : Index(std::move(metadata)), comparator_(GetMetadata()->GetKeySchema()) {
  page_id_t header_page_id = buffer_pool_manager->NewPage();
  container_ = std::make_shared<BPlusTree<KeyType, ValueType, KeyComparator>>(GetMetadata()->GetName(), header_page_id,
                                                                              buffer_pool_manager, comparator_);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_INDEX_TYPE::InsertEntry(const Tuple &key, RID rid, Transaction *transaction) -> bool {
  // construct insert index key
  KeyType index_key;
  index_key.SetFromKey(key);

  return container_->Insert(index_key, rid);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_INDEX_TYPE::DeleteEntry(const Tuple &key, RID rid, Transaction *transaction) {
  // construct delete index key
  KeyType index_key;
  index_key.SetFromKey(key);

  container_->Remove(index_key);
}

/**
 * `key` 只需要直接传递作为 Index Key 的 Column 取值.
 *
 * WARNING: `BPlusTree::GetValue()` 的实现只会返回一个 value.
 * 如果有多个 value 有相同的 key, 必须使用 Iterator 扫描.
 * bustub Test Cases 承诺不存在多个 Tuple 有相同的 Index Key 取值.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_INDEX_TYPE::ScanKey(const Tuple &key, std::vector<RID> *result, Transaction *transaction) {
  // construct scan index key
  KeyType index_key;
  index_key.SetFromKey(key);

  container_->GetValue(index_key, result);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_INDEX_TYPE::GetBeginIterator() -> INDEXITERATOR_TYPE { return container_->Begin(); }

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_INDEX_TYPE::GetBeginIterator(const KeyType &key) -> INDEXITERATOR_TYPE { return container_->Begin(key); }

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_INDEX_TYPE::GetEndIterator() -> INDEXITERATOR_TYPE { return container_->End(); }

template class BPlusTreeIndex<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeIndex<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeIndex<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeIndex<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeIndex<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
