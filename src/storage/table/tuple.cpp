//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// tuple.cpp
//
// Identification: src/storage/table/tuple.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "storage/table/tuple.h"

namespace bustub {

// TODO(Amadou): It does not look like nulls are supported. Add a null bitmap?
/**
 * constructor for creating a new tuple based on input value
 *
 * 基于提供的 Schema 来解释 `values`,
 * 并将 `values` 中的 Value object 序列化到当前 Tuple 中.
 */
Tuple::Tuple(std::vector<Value> values, const Schema *schema) {
  assert(values.size() == schema->GetColumnCount());

  // 1. Calculate the size of the tuple.

  /**
   * 先统计 inline 的数据 Size, 再逐个统计非 inline 的数据 Size.
   */
  uint32_t tuple_size = schema->GetInlinedStorageSize();
  for (auto &i : schema->GetUnlinedColumns()) {
    auto len = values[i].GetStorageSize();
    if (len == BUSTUB_VALUE_NULL) {
      len = 0;
    }
    /**
     * 对每个 uninlined column 额外预留 uint32_t,
     * 记录此 column 实际数据在 Tuple `data_` 中的起始 offset.
     */
    tuple_size += sizeof(uint32_t) + len;
  }

  // 2. Allocate memory.

  /**
   * `data_` 将用于存放 Tuple 的 byte stream.
   */
  data_.resize(tuple_size);
  std::fill(data_.begin(), data_.end(), 0);

  // 3. Serialize each attribute based on the input value.
  uint32_t column_count = schema->GetColumnCount();

  /**
   * `offset` 是 Tuple 末尾存储 uninline 实际数据的起始位置 pointer.
   */
  uint32_t offset = schema->GetInlinedStorageSize();

  for (uint32_t i = 0; i < column_count; i++) {
    const auto &col = schema->GetColumn(i);
    if (!col.IsInlined()) {
      // Serialize relative offset, where the actual varchar data is stored.
      /**
       * 对于要求 uninline 存储的 Column,
       * 仅存放 uint32_t 类型 offset, 标记 uninline 实际数据的起始 offset.
       */
      *reinterpret_cast<uint32_t *>(data_.data() + col.GetOffset()) = offset;
      // Serialize varchar value, in place (size+data).
      /**
       * 在末尾存放真正的 uninline 数据.
       * 此处序列化 Value,
       * 对于 VARCHAR 和 VECTOR, 会先将其长度序列化, 再序列化实际数据.
       */
      values[i].SerializeTo(data_.data() + offset);
      auto len = values[i].GetStorageSize();
      if (len == BUSTUB_VALUE_NULL) {
        len = 0;
      }
      /**
       * 此处 bustub 设计较耦合.
       * uint32_t 是 VARCHAR 和 VECTOR 被序列化的长度信息字段.
       * 为 NULL 时, 只有占据 uint32_t 大小的长度信息, `len` 实际为 0.
       */
      offset += sizeof(uint32_t) + len;
    } else {
      values[i].SerializeTo(data_.data() + col.GetOffset());
    }
  }
}

/**
 * constructor for creating a new tuple by copying fron existing bytes
 */
Tuple::Tuple(RID rid, const char *data, uint32_t size) {
  rid_ = rid;
  data_.resize(size);
  memcpy(data_.data(), data, size);
}

/**
 * Get the value of a specified column (const)
 * checks the schema to see how to return the Value.
 */
auto Tuple::GetValue(const Schema *schema, const uint32_t column_idx) const -> Value {
  assert(schema);
  const TypeId column_type = schema->GetColumn(column_idx).GetType();
  const char *data_ptr = GetDataPtr(schema, column_idx);
  // the third parameter "is_inlined" is unused
  return Value::DeserializeFrom(data_ptr, column_type);
}

/**
 * Generates a key tuple given schemas and attributes
 */
auto Tuple::KeyFromTuple(const Schema &schema, const Schema &key_schema, const std::vector<uint32_t> &key_attrs) const
    -> Tuple {
  std::vector<Value> values;
  values.reserve(key_attrs.size());
  for (auto idx : key_attrs) {
    values.emplace_back(this->GetValue(&schema, idx));
  }
  return {values, &key_schema};
}

/**
 * Get the starting storage address of specific column
 */
auto Tuple::GetDataPtr(const Schema *schema, const uint32_t column_idx) const -> const char * {
  assert(schema);
  const auto &col = schema->GetColumn(column_idx);
  bool is_inlined = col.IsInlined();
  // For inline type, data is stored where it is.
  if (is_inlined) {
    return (data_.data() + col.GetOffset());
  }
  // We read the relative offset from the tuple data.
  int32_t offset = *reinterpret_cast<const int32_t *>(data_.data() + col.GetOffset());
  // And return the beginning address of the real data for the VARCHAR type.
  return (data_.data() + offset);
}

auto Tuple::ToString(const Schema *schema) const -> std::string {
  std::stringstream os;

  int column_count = schema->GetColumnCount();
  bool first = true;
  os << "(";
  for (int column_itr = 0; column_itr < column_count; column_itr++) {
    if (first) {
      first = false;
    } else {
      os << ", ";
    }
    if (IsNull(schema, column_itr)) {
      os << "<NULL>";
    } else {
      Value val = (GetValue(schema, column_itr));
      os << val.ToString();
    }
  }
  os << ")";

  return os.str();
}

/**
 * serialize tuple data
 */
void Tuple::SerializeTo(char *storage) const {
  int32_t sz = data_.size();
  memcpy(storage, &sz, sizeof(int32_t));
  memcpy(storage + sizeof(int32_t), data_.data(), sz);
}

/**
 * deserialize tuple data(deep copy)
 */
void Tuple::DeserializeFrom(const char *storage) {
  uint32_t size = *reinterpret_cast<const uint32_t *>(storage);
  this->data_.resize(size);
  memcpy(this->data_.data(), storage + sizeof(int32_t), size);
}

}  // namespace bustub
