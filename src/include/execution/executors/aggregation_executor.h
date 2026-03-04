//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.h
//
// Identification: src/include/execution/executors/aggregation_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/macros.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/plans/aggregation_plan.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * A simplified hash table that has all the necessary functionality for aggregations.
 */
class SimpleAggregationHashTable {
 private:
  /**
   * @return The initial aggregate value for this aggregation executor
   *
   * COUNT(*) 对空表得到 0, 对 NULL 也算入计数.
   * 其他的 Value 从 NULL 开始, 需要注意.
   */
  auto GenerateInitialAggregateValue() -> AggregateValue {
    std::vector<Value> values{};
    for (const auto &agg_type : agg_types_) {
      switch (agg_type) {
        case AggregationType::CountStarAggregate:
          // Count start starts at zero.
          values.emplace_back(ValueFactory::GetIntegerValue(0));
          break;
        case AggregationType::CountAggregate:
        case AggregationType::SumAggregate:
        case AggregationType::MinAggregate:
        case AggregationType::MaxAggregate:
          // Others starts at null.
          values.emplace_back(ValueFactory::GetNullValueByType(TypeId::INTEGER));
          break;
      }
    }
    return {values};
  }

  /**
   * Combines the input into the aggregation result.
   * @param[out] result The output aggregate value
   * @param input The input value
   *
   * `input` 是来自 Executor 的, 一个 Tuple 中涉及聚合函数计算的取值.
   * `result` 是加入 `input` 进行统计后的最终结果.
   * 两者所属的 AggregateKey 取值应该是相等的.
   */
  void CombineAggregateValues(AggregateValue *result, const AggregateValue &input) {
    for (uint32_t i = 0; i < agg_exprs_.size(); i++) {
      switch (agg_types_[i]) {
        case AggregationType::CountStarAggregate:  // Start from 0.
          result->aggregates_[i] = result->aggregates_[i].Add(ValueFactory::GetIntegerValue(1));
          break;
        case AggregationType::CountAggregate:  // Start from NULL. Ignore NULL.
          if (input.aggregates_[i].IsNull()) {
            break;
          }
          if (result->aggregates_[i].IsNull()) {
            result->aggregates_[i] = ValueFactory::GetIntegerValue(1);
          } else {
            result->aggregates_[i] = result->aggregates_[i].Add(ValueFactory::GetIntegerValue(1));
          }
          break;
        case AggregationType::SumAggregate:
          if (input.aggregates_[i].IsNull()) {
            break;
          }
          if (result->aggregates_[i].IsNull()) {
            result->aggregates_[i] = input.aggregates_[i];
          } else {
            result->aggregates_[i] = result->aggregates_[i].Add(input.aggregates_[i]);
          }
          break;
        case AggregationType::MinAggregate:
          if (input.aggregates_[i].IsNull()) {
            break;
          }
          if (result->aggregates_[i].IsNull()) {
            result->aggregates_[i] = input.aggregates_[i];
          } else {
            result->aggregates_[i] = result->aggregates_[i].Min(input.aggregates_[i]);
          }
          break;
        case AggregationType::MaxAggregate:
          if (input.aggregates_[i].IsNull()) {
            break;
          }
          if (result->aggregates_[i].IsNull()) {
            result->aggregates_[i] = input.aggregates_[i];
          } else {
            result->aggregates_[i] = result->aggregates_[i].Max(input.aggregates_[i]);
          }
          break;
        default:
          BUSTUB_ASSERT(false, "Unsupported.");
      }
    }

    // UNIMPLEMENTED("TODO(P3): Add implementation.");
  }

 public:
  /**
   * Construct a new SimpleAggregationHashTable instance.
   * @param agg_exprs the aggregation expressions
   * @param agg_types the types of aggregations
   */
  SimpleAggregationHashTable(const std::vector<AbstractExpressionRef> &agg_exprs,
                             const std::vector<AggregationType> &agg_types)
      : agg_exprs_{agg_exprs}, agg_types_{agg_types} {}

  /**
   * Inserts a value into the hash table and then combines it with the current aggregation.
   * @param agg_key the key to be inserted
   * @param agg_val the value to be inserted
   *
   * `GenerateInitialAggregateValue()` 给一个 agg key 初始化其聚合函数统计结果.
   * `CombineAggregateValues()` 将 Executor 传递的新的 `agg_val` 添加上去.
   */
  void InsertCombine(const AggregateKey &agg_key, const AggregateValue &agg_val) {
    if (ht_.count(agg_key) == 0) {
      ht_.insert({agg_key, GenerateInitialAggregateValue()});
    }
    CombineAggregateValues(&ht_[agg_key], agg_val);
  }

  /**
   * Clear the hash table
   */
  void Clear() { ht_.clear(); }

  /** An iterator over the aggregation hash table */
  class Iterator {
   public:
    /** Creates an iterator for the aggregate map. */
    explicit Iterator(std::unordered_map<AggregateKey, AggregateValue>::const_iterator iter) : iter_{iter} {}

    /** @return The key of the iterator */
    auto Key() -> const AggregateKey & { return iter_->first; }

    /** @return The value of the iterator */
    auto Val() -> const AggregateValue & { return iter_->second; }

    /** @return The iterator before it is incremented */
    auto operator++() -> Iterator & {
      ++iter_;
      return *this;
    }

    /** @return `true` if both iterators are identical */
    auto operator==(const Iterator &other) -> bool { return this->iter_ == other.iter_; }

    /** @return `true` if both iterators are different */
    auto operator!=(const Iterator &other) -> bool { return this->iter_ != other.iter_; }

   private:
    /** Aggregates map */
    std::unordered_map<AggregateKey, AggregateValue>::const_iterator iter_;
  };

  /** @return Iterator to the start of the hash table */
  auto Begin() -> Iterator { return Iterator{ht_.cbegin()}; }

  /** @return Iterator to the end of the hash table */
  auto End() -> Iterator { return Iterator{ht_.cend()}; }

 private:
  /**
   * The hash table is just a map from aggregate keys to aggregate values
   *
   * 主要结构. 其中的 AggregateValue 需要 Executor 动态更新.
   */
  std::unordered_map<AggregateKey, AggregateValue> ht_;

  /** The aggregate expressions that we have */
  const std::vector<AbstractExpressionRef> &agg_exprs_;
  /** The types of aggregations that we have */
  const std::vector<AggregationType> &agg_types_;
};

/**
 * AggregationExecutor executes an aggregation operation (e.g. COUNT, SUM, MIN, MAX)
 * over the tuples produced by a child executor.
 */
class AggregationExecutor : public AbstractExecutor {
 public:
  AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                      std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the aggregation */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); };

  auto GetChildExecutor() const -> const AbstractExecutor *;

 private:
  /** @return The tuple as an AggregateKey */
  auto MakeAggregateKey(const Tuple *tuple) -> AggregateKey {
    std::vector<Value> keys;
    for (const auto &expr : to_plan_->GetGroupBys()) {
      keys.emplace_back(expr->Evaluate(tuple, child_executor_->GetOutputSchema()));
    }
    return {keys};
  }

  /** @return The tuple as an AggregateValue */
  auto MakeAggregateValue(const Tuple *tuple) -> AggregateValue {
    std::vector<Value> vals;
    for (const auto &expr : to_plan_->GetAggregates()) {
      vals.emplace_back(expr->Evaluate(tuple, child_executor_->GetOutputSchema()));
    }
    return {vals};
  }

 private:
  /** The aggregation plan node */
  const AggregationPlanNode *to_plan_;

  /** The child executor that produces tuples over which the aggregation is computed */
  std::unique_ptr<AbstractExecutor> child_executor_;

  /** Simple aggregation hash table */
  // TODO(Student): Uncomment SimpleAggregationHashTable aht_;
  SimpleAggregationHashTable aht_;

  /** Simple aggregation hash table iterator */
  // TODO(Student): Uncomment SimpleAggregationHashTable::Iterator aht_iterator_;
  std::unique_ptr<SimpleAggregationHashTable::Iterator> to_aht_iter_;

  bool input_is_empty_{true};
  bool is_consumed_{};
};
}  // namespace bustub
