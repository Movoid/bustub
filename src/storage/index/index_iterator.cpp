//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/**
 * @note you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return !page_g_opt_.has_value(); }

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  // UNIMPLEMENTED("TODO(P2): Add implementation.");
  BUSTUB_ASSERT(!IsEnd(), "Invalid instance.");
  auto page_data{page_g_opt_.value().As<LeafPage>()};
  BUSTUB_ASSERT(page_data->ValueAt(offset_).GetPageId() != INVALID_PAGE_ID, "Unexpected.");
  return {page_data->KeyAt(offset_), page_data->ValueAt(offset_)};
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  BUSTUB_ASSERT(!IsEnd(), "Invalid instance.");
  BUSTUB_ASSERT(page_g_opt_.has_value(), "Unexpected.");
  BUSTUB_ASSERT(page_g_opt_.value().As<BPlusTreePage>()->IsLeafPage(), "Unexpected.");
  offset_++;

  auto page_data{page_g_opt_.value().As<LeafPage>()};

  while (true) {
    while (offset_ < page_data->GetSize() && page_data->ValueAt(offset_) == RID{INVALID_PAGE_ID, 0}) {
      offset_++;
    }
    if (offset_ == page_data->GetSize() && page_data->GetNextPageId() != INVALID_PAGE_ID) {
      page_id_t sib_id{page_data->GetNextPageId()};
      page_g_opt_.value().Drop();
      page_g_opt_ = bpm_->ReadPage(sib_id);
      page_data = page_g_opt_.value().As<LeafPage>();
      offset_ = 0;
      continue;
    }
    break;
  }

  if (offset_ == page_data->GetSize()) {
    page_g_opt_ = std::nullopt;
    offset_ = -1;
  }

  return *this;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator==(const IndexIterator &itr) const -> bool {
  // return page_g_opt_ == itr.page_g_opt_
  if (bpm_ != itr.bpm_) {
    return false;
  }
  if (offset_ != itr.offset_) {
    return false;
  }
  if (page_g_opt_.has_value() != itr.page_g_opt_.has_value()) {
    return false;
  }
  if (page_g_opt_.has_value()) {
    if (page_g_opt_->GetPageId() != itr.page_g_opt_->GetPageId()) {
      return false;
    }
  }
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator!=(const IndexIterator &itr) const -> bool { return !(*this == itr); }

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
