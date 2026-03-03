//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// table_page.cpp
//
// Identification: src/storage/page/table_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/table_page.h"

#include <cassert>
#include <cstring>
#include <optional>
#include <tuple>
#include "common/config.h"
#include "common/exception.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Initialize the TablePage header.
 */
void TablePage::Init() {
  next_page_id_ = INVALID_PAGE_ID;
  num_tuples_ = 0;
  num_deleted_tuples_ = 0;
}

/**
 * Get the next offset to **insert**, return nullopt if this tuple cannot fit in this page.
 * 注意, 这用于获取下一个可插入 Tuple 的位置.
 */
auto TablePage::GetNextTupleOffset(const TupleMeta &meta, const Tuple &tuple) const -> std::optional<uint16_t> {
  size_t slot_end_offset;
  if (num_tuples_ > 0) {
    auto &[offset, size, meta] = tuple_info_[num_tuples_ - 1];
    slot_end_offset = offset;
  } else {
    slot_end_offset = BUSTUB_PAGE_SIZE;
  }

  /**
   * 注意区分,
   * `slot_end_offset` 标记区间右开边界.
   * `tuple_offset` 区间左闭边界.
   */
  auto tuple_offset = slot_end_offset - tuple.GetLength();
  /**
   * `offset_size` 得到 Page 前部分尝试插入新 `TupleInfo` 后, 已经使用了的空间的末尾 Offset.
   * 尝试在 Page 尾部插入新的 Tuple, 若超越此 Offset 则空间不足.
   */
  auto offset_size = TABLE_PAGE_HEADER_SIZE + (TUPLE_INFO_SIZE * (num_tuples_ + 1));
  if (tuple_offset < offset_size) {
    return std::nullopt;
  }
  return tuple_offset;
}

/**
 * Insert a tuple into the table.
 * @param tuple tuple to insert
 * @return true if the insert is successful (i.e. there is enough space)
 */
auto TablePage::InsertTuple(const TupleMeta &meta, const Tuple &tuple) -> std::optional<uint16_t> {
  auto tuple_offset = GetNextTupleOffset(meta, tuple);
  if (tuple_offset == std::nullopt) {
    return std::nullopt;
  }
  /**
   * `tuple_offset` 有效, 则允许分配一个新的 Slot.
   */
  auto tuple_id = num_tuples_;
  tuple_info_[tuple_id] = std::make_tuple(*tuple_offset, tuple.GetLength(), meta);
  num_tuples_++;
  /**
   * `tuple` 新 Tuple,
   * 将其数据拷贝到 Page 的对应 Slot 位置上.
   */
  memcpy(page_start_ + *tuple_offset, tuple.data_.data(), tuple.GetLength());
  return tuple_id;
}

/**
 * Update a tuple.
 * 允许通过此函数删除一个 Tuple.
 */
void TablePage::UpdateTupleMeta(const TupleMeta &meta, const RID &rid) {
  auto tuple_id = rid.GetSlotNum();
  if (tuple_id >= num_tuples_) {
    throw bustub::Exception("Tuple ID out of range");
  }
  auto &[offset, size, old_meta] = tuple_info_[tuple_id];
  if (!old_meta.is_deleted_ && meta.is_deleted_) {
    num_deleted_tuples_++;
  }
  tuple_info_[tuple_id] = std::make_tuple(offset, size, meta);
}

/**
 * Read a tuple from a table.
 */
auto TablePage::GetTuple(const RID &rid) const -> std::pair<TupleMeta, Tuple> {
  auto tuple_id = rid.GetSlotNum();
  if (tuple_id >= num_tuples_) {
    throw bustub::Exception("Tuple ID out of range");
  }
  auto &[offset, size, meta] = tuple_info_[tuple_id];
  Tuple tuple;
  tuple.data_.resize(size);
  memmove(tuple.data_.data(), page_start_ + offset, size);
  tuple.rid_ = rid;
  return std::make_pair(meta, std::move(tuple));
}

/**
 * Read a tuple meta from a table.
 */
auto TablePage::GetTupleMeta(const RID &rid) const -> TupleMeta {
  auto tuple_id = rid.GetSlotNum();
  if (tuple_id >= num_tuples_) {
    throw bustub::Exception("Tuple ID out of range");
  }
  auto &[_1, _2, meta] = tuple_info_[tuple_id];
  return meta;
}

/**
 * Update a tuple in place.
 */
void TablePage::UpdateTupleInPlaceUnsafe(const TupleMeta &meta, const Tuple &tuple, RID rid) {
  auto tuple_id = rid.GetSlotNum();
  if (tuple_id >= num_tuples_) {
    throw bustub::Exception("Tuple ID out of range");
  }
  auto &[offset, size, old_meta] = tuple_info_[tuple_id];
  if (size != tuple.GetLength()) {
    throw bustub::Exception("Tuple size mismatch");
  }
  if (!old_meta.is_deleted_ && meta.is_deleted_) {
    num_deleted_tuples_++;
  }
  tuple_info_[tuple_id] = std::make_tuple(offset, size, meta);
  memcpy(page_start_ + offset, tuple.data_.data(), tuple.GetLength());
}

}  // namespace bustub
