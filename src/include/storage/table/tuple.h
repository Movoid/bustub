//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// tuple.h
//
// Identification: src/include/storage/table/tuple.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "catalog/schema.h"
#include "common/config.h"
#include "common/rid.h"
#include "type/value.h"

namespace bustub {

using timestamp_t = int64_t;
const timestamp_t INVALID_TS = -1;

static constexpr size_t TUPLE_META_SIZE = 16;

/**
 * W-Timestamp 和 Tombstone 标记.
 */
struct TupleMeta {
  /** the ts / txn_id of this tuple. In project 3, simply set it to 0. */
  timestamp_t ts_;
  /** marks whether this tuple is marked removed from table heap. */
  bool is_deleted_;

  friend auto operator==(const TupleMeta &a, const TupleMeta &b) {
    return a.ts_ == b.ts_ && a.is_deleted_ == b.is_deleted_;
  }

  friend auto operator!=(const TupleMeta &a, const TupleMeta &b) { return !(a == b); }
};

static_assert(sizeof(TupleMeta) == TUPLE_META_SIZE);

/**
 * Tuple format:
 * ---------------------------------------------------------------------
 * | FIXED-SIZE or VARIED-SIZED OFFSET | PAYLOAD OF VARIED-SIZED FIELD |
 * ---------------------------------------------------------------------
 *
 * Tuple 在 DRAM 承载了来自 Disk 的, RID 位置的一个元组的 bytes.
 * 前半部分包括定长数据, 和变长数据在 Tuple bytes 中的起始 offset.
 * 后半部分实际存储变长数据.
 *
 * Tuple 仅存储 bytes.
 * Schema 决定如何解释这段 bytes,
 * 是类似 Value 和 Type 的设计.
 */
class Tuple {
  friend class TablePage;
  friend class TableHeap;
  friend class TableIterator;

 public:
  // Default constructor (to create a dummy tuple)
  Tuple() = default;

  // constructor for table heap tuple
  explicit Tuple(RID rid) : rid_(rid) {}

  static auto Empty() -> Tuple { return Tuple{RID{INVALID_PAGE_ID, 0}}; }

  /**
   * 使用 Value 数组与匹配的 Schema 来构造 Tuple.
   * 主要的构造方法, 将 Value 按 Schema 序列化到了 Tuple 上.
   * Tuple 可以被直接序列化到 Page 上.
   */
  Tuple(std::vector<Value> values, const Schema *schema);

  /**
   * 直接使用 bytes 来构造 Tuple.
   */
  Tuple(RID rid, const char *data, uint32_t size);

  Tuple(const Tuple &other) = default;

  // move constructor
  Tuple(Tuple &&other) noexcept = default;

  // assign operator, deep copy
  auto operator=(const Tuple &other) -> Tuple & = default;

  // move assignment
  auto operator=(Tuple &&other) noexcept -> Tuple & = default;

  void SerializeTo(char *storage) const;

  void DeserializeFrom(const char *storage);

  // return RID of current tuple
  inline auto GetRid() const -> RID { return rid_; }

  // return RID of current tuple
  inline auto SetRid(RID rid) { rid_ = rid; }

  // Get the address of this tuple in the table's backing store
  inline auto GetData() const -> const char * { return data_.data(); }

  // Get length of the tuple, including varchar length
  inline auto GetLength() const -> uint32_t { return data_.size(); }

  auto GetValue(const Schema *schema, uint32_t column_idx) const -> Value;

  /**
   * 用于提取 Tuple 中部分 Column, 并得到新 Tuple 的工具.
   * `schema` 是原 Schema, `key_schema` 是目标 Schema.
   */
  auto KeyFromTuple(const Schema &schema, const Schema &key_schema, const std::vector<uint32_t> &key_attrs) const
      -> Tuple;

  // Is the column value null ?
  inline auto IsNull(const Schema *schema, uint32_t column_idx) const -> bool {
    Value value = GetValue(schema, column_idx);
    return value.IsNull();
  }

  auto ToString(const Schema *schema) const -> std::string;

  friend inline auto IsTupleContentEqual(const Tuple &a, const Tuple &b) { return a.data_ == b.data_; }

 private:
  auto GetDataPtr(const Schema *schema, uint32_t column_idx) const -> const char *;

  RID rid_{INVALID_PAGE_ID, 0};  // if pointing to the table heap, the rid is valid
  std::vector<char> data_;
};

}  // namespace bustub
