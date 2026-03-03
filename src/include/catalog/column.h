//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// column.h
//
// Identification: src/include/catalog/column.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "fmt/format.h"

#include "common/exception.h"
#include "common/macros.h"
#include "type/type.h"
#include "type/type_id.h"

namespace bustub {
class AbstractExpression;

/**
 * 用于描述 Column 的 metadata.
 * Column 不负责处理 inline 逻辑.
 *
 * 任何类型数据都可以成为 inline/uninline 的,
 * 由 Schema 决定.
 */

class Column {
  friend class Schema;

 public:
  /**
   * Non-variable-length constructor for creating a Column.
   * @param column_name name of the column
   * @param type type of the column
   */
  Column(std::string column_name, TypeId type)
      : column_name_(std::move(column_name)), column_type_(type), length_(TypeSize(type)) {
    BUSTUB_ASSERT(type != TypeId::VARCHAR, "Wrong constructor for VARCHAR type.");
    BUSTUB_ASSERT(type != TypeId::VECTOR, "Wrong constructor for VECTOR type.");
  }

  /**
   * Variable-length constructor for creating a Column.
   * @param column_name name of the column
   * @param type type of column
   * @param length length of the varlen
   * @param expr expression used to create this column
   *
   * VARCHAR 和 VECTOR 需要提供 `length` 信息.
   */
  Column(std::string column_name, TypeId type, uint32_t length)
      : column_name_(std::move(column_name)), column_type_(type), length_(TypeSize(type, length)) {
    BUSTUB_ASSERT(type == TypeId::VARCHAR || type == TypeId::VECTOR, "Wrong constructor for fixed-size type.");
  }

  /**
   * Replicate a Column with a different name.
   * @param column_name name of the column
   * @param column the original column
   */
  Column(std::string column_name, const Column &column)
      : column_name_(std::move(column_name)),
        column_type_(column.column_type_),
        length_(column.length_),
        column_offset_(column.column_offset_) {}

  auto WithColumnName(std::string column_name) -> Column {
    Column c = *this;
    c.column_name_ = std::move(column_name);
    return c;
  }

  /** @return column name */
  auto GetName() const -> std::string { return column_name_; }

  /** @return column length */
  auto GetStorageSize() const -> uint32_t { return length_; }

  /** @return column's offset in the tuple */
  auto GetOffset() const -> uint32_t { return column_offset_; }

  /** @return column type */
  auto GetType() const -> TypeId { return column_type_; }

  /**
   * @return true if column is inlined, false otherwise.
   *
   * 此处决定只有 VARCHAR 和 VECTOR 会被 uninline 存储.
   */
  auto IsInlined() const -> bool { return column_type_ != TypeId::VARCHAR && column_type_ != TypeId::VECTOR; }

  auto ToString(bool simplified = true) const -> std::string;

 private:
  /**
   * Return the size in bytes of the type.
   * @param type type whose size is to be determined
   * @return size in bytes
   *
   * `static` 接口.
   * 返回类型实际数据的长度.
   * 对于 VARCHAR 和 VECTOR, 会按长度计算实际 bytes.
   */
  static auto TypeSize(TypeId type, uint32_t length = 0) -> uint8_t {
    switch (type) {
      case TypeId::BOOLEAN:
      case TypeId::TINYINT:
        return 1;
      case TypeId::SMALLINT:
        return 2;
      case TypeId::INTEGER:
        return 4;
      case TypeId::BIGINT:
      case TypeId::DECIMAL:
      case TypeId::TIMESTAMP:
        return 8;
      case TypeId::VARCHAR:
        return length;
      case TypeId::VECTOR:
        return length * sizeof(double);
      default: {
        UNREACHABLE("Cannot get size of invalid type");
      }
    }
  }

  /** Column name. */
  std::string column_name_;

  /** Column value's type. */
  TypeId column_type_;

  /** The size of the column. */
  uint32_t length_;

  /** Column offset in the tuple. */
  uint32_t column_offset_{0};
};

}  // namespace bustub

template <typename T>
struct fmt::formatter<T, std::enable_if_t<std::is_base_of<bustub::Column, T>::value, char>>
    : fmt::formatter<std::string> {
  template <typename FormatCtx>
  auto format(const bustub::Column &x, FormatCtx &ctx) const {
    return fmt::formatter<std::string>::format(x.ToString(), ctx);
  }
};

template <typename T>
struct fmt::formatter<std::unique_ptr<T>, std::enable_if_t<std::is_base_of<bustub::Column, T>::value, char>>
    : fmt::formatter<std::string> {
  template <typename FormatCtx>
  auto format(const std::unique_ptr<bustub::Column> &x, FormatCtx &ctx) const {
    return fmt::formatter<std::string>::format(x->ToString(), ctx);
  }
};
