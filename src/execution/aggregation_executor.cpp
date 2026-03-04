//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/aggregation_executor.h"

namespace bustub {

/**
 * Construct a new AggregationExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled (may be `nullptr`)
 *
 * 注意 `SELECT COUNT(*) FROM test_2;` 这种没有 GROUP BY 的情况.
 * 以及 `SELECT colB FROM test_2 GROUP BY colB;` 没有聚合函数的情况.
 *
 * 在输入为空且没有 GROUP BY 时仍需要输出一条 Tuple.
 * 空表但有 GROUP BY 则无输出.
 */
AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx},
      to_plan_{plan},
      child_executor_{std::move(child_executor)},
      aht_{plan->GetAggregates(), plan->GetAggregateTypes()} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the aggregation */
void AggregationExecutor::Init() {
  /**
   * Clean self before init child.
   */
  aht_.Clear();  // Only clean data, not metadata.
  to_aht_iter_.reset();
  input_is_empty_ = true;
  is_consumed_ = false;

  child_executor_->Init();
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  /**
   * Build Hash Table.
   */

  Tuple cur_tuple{};
  RID cur_rid{};
  AggregateKey cur_gb_key{};
  cur_gb_key.group_bys_.reserve(to_plan_->GetGroupBys().size());
  AggregateValue cur_agg_value{};
  cur_agg_value.aggregates_.reserve(to_plan_->GetAggregates().size());

  BUSTUB_ASSERT(
      to_plan_->OutputSchema().GetColumnCount() == to_plan_->GetGroupBys().size() + to_plan_->GetAggregates().size(),
      "Unexpected.");

  while (child_executor_->Next(&cur_tuple, &cur_rid)) {
    input_is_empty_ = false;
    cur_gb_key.group_bys_.clear();
    for (auto &gb_expr : to_plan_->GetGroupBys()) {
      cur_gb_key.group_bys_.emplace_back(gb_expr->Evaluate(&cur_tuple, child_executor_->GetOutputSchema()));
    }
    cur_agg_value.aggregates_.clear();
    for (auto &agg_expr : to_plan_->GetAggregates()) {
      cur_agg_value.aggregates_.emplace_back(agg_expr->Evaluate(&cur_tuple, child_executor_->GetOutputSchema()));
    }
    aht_.InsertCombine(cur_gb_key, cur_agg_value);
  }

  to_aht_iter_ = std::make_unique<SimpleAggregationHashTable::Iterator>(aht_.Begin());
}

/**
 * Yield the next tuple from the insert.
 * @param[out] tuple The next tuple produced by the aggregation
 * @param[out] rid The next tuple RID produced by the aggregation
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  /**
   * 对于空输入, 如果没有 GROUP BY,
   * 需要输出 1 行 0/NULL.
   */
  if (input_is_empty_ && to_plan_->GetGroupBys().empty()) {
    BUSTUB_ASSERT(aht_.Begin() == aht_.End(), "Unexpected.");
    if (is_consumed_) {
      return false;
    }
    std::vector<Value> output_values{};
    output_values.reserve(to_plan_->OutputSchema().GetColumnCount());
    for (std::size_t i = 0; i < to_plan_->GetAggregates().size(); i++) {
      if (to_plan_->GetAggregateTypes()[i] == AggregationType::CountStarAggregate) {
        output_values.emplace_back(ValueFactory::GetIntegerValue(0));
      } else {
        output_values.emplace_back(ValueFactory::GetNullValueByType(to_plan_->OutputSchema().GetColumn(i).GetType()));
      }
    }
    *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
    *rid = RID{INVALID_PAGE_ID, 0};
    is_consumed_ = true;
    return true;
  }

  BUSTUB_ASSERT(to_aht_iter_ != nullptr, "Unexpected.");

  if (*to_aht_iter_ == aht_.End()) {
    return false;
  }
  /**
   * Tuple 开头 GROUP BY keys, 后面 AGG keys.
   * 最终输出顺序由 Projection Executor 确定.
   */

  std::vector<Value> output_values{};
  output_values.reserve(to_plan_->OutputSchema().GetColumnCount());
  output_values.insert(output_values.end(), to_aht_iter_->Key().group_bys_.begin(),
                       to_aht_iter_->Key().group_bys_.end());
  output_values.insert(output_values.end(), to_aht_iter_->Val().aggregates_.begin(),
                       to_aht_iter_->Val().aggregates_.end());
  ++(*to_aht_iter_);
  BUSTUB_ASSERT(output_values.size() == to_plan_->OutputSchema().GetColumnCount(), "Unexpected.");

  *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
  *rid = RID{INVALID_PAGE_ID, 0};

  return true;
}

/** Do not use or remove this function, otherwise you will get zero points. */
auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
