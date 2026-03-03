//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// type.h
//
// Identification: src/include/type/type.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include "type/type_id.h"

namespace bustub {

class Value;

// NOLINTNEXTLINE
enum class CmpBool { CmpFalse = 0, CmpTrue = 1, CmpNull = 2 };

/**
 * 对数据类型的抽象。
 * bustub 将一个数据的类型和实际存储拆分为 Type 和 Value .
 * Type 系列 Class 提供了对应类型的可用操作.
 */

class Type {
 public:
  explicit Type(TypeId type_id) : type_id_(type_id) {}

  virtual ~Type() = default;

  static auto GetTypeSize(TypeId type_id) -> uint64_t;

  auto IsCoercableFrom(TypeId type_id) const -> bool;

  static auto TypeIdToString(TypeId type_id) -> std::string;

  static auto GetMinValue(TypeId type_id) -> Value;
  static auto GetMaxValue(TypeId type_id) -> Value;

  static auto GetInstance(TypeId type_id) -> Type * { return k_types[type_id]; }

  auto GetTypeId() const -> TypeId { return type_id_; }

  // Comparison functions
  //
  // NOTE:
  // We could get away with only CompareLessThan() being purely virtual, since
  // the remaining comparison functions can derive their logic from
  // CompareLessThan(). For example:
  //
  //    CompareEquals(o) = !CompareLessThan(o) && !o.CompareLessThan(this)
  //    CompareNotEquals(o) = !CompareEquals(o)
  //    CompareLessThanEquals(o) = CompareLessThan(o) || CompareEquals(o)
  //    CompareGreaterThan(o) = !CompareLessThanEquals(o)
  //    ... etc. ...
  //
  // We don't do this for two reasons:
  // (1) The redundant calls to CompareLessThan() may be a performance problem,
  //     and since Value is a core component of the execution engine, we want to
  //     make it as performant as possible.
  // (2) Keep the interface consistent by making all functions purely virtual.
  virtual auto CompareEquals(const Value &left, const Value &right) const -> CmpBool;
  virtual auto CompareNotEquals(const Value &left, const Value &right) const -> CmpBool;
  virtual auto CompareLessThan(const Value &left, const Value &right) const -> CmpBool;
  virtual auto CompareLessThanEquals(const Value &left, const Value &right) const -> CmpBool;
  virtual auto CompareGreaterThan(const Value &left, const Value &right) const -> CmpBool;
  virtual auto CompareGreaterThanEquals(const Value &left, const Value &right) const -> CmpBool;

  // Other mathematical functions
  virtual auto Add(const Value &left, const Value &right) const -> Value;
  virtual auto Subtract(const Value &left, const Value &right) const -> Value;
  virtual auto Multiply(const Value &left, const Value &right) const -> Value;
  virtual auto Divide(const Value &left, const Value &right) const -> Value;
  virtual auto Modulo(const Value &left, const Value &right) const -> Value;
  virtual auto Min(const Value &left, const Value &right) const -> Value;
  virtual auto Max(const Value &left, const Value &right) const -> Value;
  virtual auto Sqrt(const Value &val) const -> Value;
  virtual auto OperateNull(const Value &val, const Value &right) const -> Value;
  virtual auto IsZero(const Value &val) const -> bool;

  // Is the data inlined into this classes storage space, or must it be accessed
  // through an indirection/pointer?
  virtual auto IsInlined(const Value &val) const -> bool;

  // Return a stringified version of this value
  virtual auto ToString(const Value &val) const -> std::string;

  // Serialize this value into the given storage space.
  virtual void SerializeTo(const Value &val, char *storage) const;

  // Deserialize a value of the given type from the given storage space.
  virtual auto DeserializeFrom(const char *storage) const -> Value;

  // Create a copy of this value
  virtual auto Copy(const Value &val) const -> Value;

  virtual auto CastAs(const Value &val, TypeId type_id) const -> Value;

  // Access the raw varlen data stored from the tuple storage
  virtual auto GetData(const Value &val) const -> const char *;

  // Get the storage size of the value.
  virtual auto GetStorageSize(const Value &val) const -> uint32_t;

 protected:
  // The actual type ID
  /**
   * 具体类型 Class 继承 Type Class 并委托构造 Type 设置自己的 `type_id_`.
   */
  TypeId type_id_;

  // Singleton instances.
  /**
   * `static` 全局共享, 进行类似多态的运行时派发.
   */
  static Type *k_types[10];
};
}  // namespace bustub
