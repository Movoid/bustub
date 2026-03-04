//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new NestedLoopJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The nested loop join plan to be executed
 * @param left_executor The child executor that produces tuple for the left side of join
 * @param right_executor The child executor that produces tuple for the right side of join
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor{exec_ctx},
      to_plan_{plan},
      left_executor_{std::move(left_executor)},
      right_executor_{std::move(right_executor)} {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {  // NOLINT
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Initialize the join
 *
 * WARNINIG: 几乎所有 Executor 的 Init 都需要可重入.
 * 这意味着必须将 Executor 恢复到刚构造的初始状态.
 *
 * 考虑 Scan 类算子, 可能需要支持多遍 Scan, 每次都需要调用 Init 恢复状态.
 * 若 Init 不可重入将导致同一个算子每次调用输出不同.
 */
void NestedLoopJoinExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  BUSTUB_ASSERT(
      to_plan_->OutputSchema().GetColumnCount() ==
          NestedLoopJoinPlanNode::InferJoinSchema(*to_plan_->GetLeftPlan().get(), *to_plan_->GetRightPlan().get())
              .GetColumnCount(),
      "Unexpected.");

  cur_l_tuple_is_joined_ = false;
  is_consumed_ = false;

  left_executor_->Init();
  right_executor_->Init();
  auto l_status{left_executor_->Next(&cur_l_tuple_, &cur_l_rid_)};
  if (!l_status) {
    is_consumed_ = true;
  }
}

/**
 * Yield the next tuple from the join.
 * @param[out] tuple The next tuple produced by the join
 * @param[out] rid The next tuple RID produced, not used by nested loop join.
 * @return `true` if a tuple was produced, `false` if there are no more tuples.
 */
auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  /**
   * NLJ 通过反复调用 `right_executor_` 的 Init 来多遍遍历.
   */

  if (is_consumed_) {
    return false;
  }

  Tuple cur_r_tuple{};
  RID cur_r_rid{};

  while (true) {
    auto r_status{right_executor_->Next(&cur_r_tuple, &cur_r_rid)};
    if (!r_status) {
      /**
       * LEFT JOIN. 保留左表 Tuple.
       */
      bool have_output{};
      if (to_plan_->GetJoinType() == JoinType::LEFT && !cur_l_tuple_is_joined_) {
        std::vector<Value> output_values{};
        output_values.reserve(to_plan_->OutputSchema().GetColumnCount());
        const auto &left_schema{to_plan_->GetLeftPlan()->OutputSchema()};
        const auto &right_schema{to_plan_->GetRightPlan()->OutputSchema()};
        for (size_t i = 0; i < left_schema.GetColumnCount(); i++) {
          output_values.emplace_back(cur_l_tuple_.GetValue(&left_schema, i));
        }
        for (size_t i = 0; i < right_schema.GetColumnCount(); i++) {
          output_values.emplace_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
        }
        *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
        *rid = RID{INVALID_PAGE_ID, 0};
        have_output = true;
      }

      auto l_status{left_executor_->Next(&cur_l_tuple_, &cur_l_rid_)};
      if (!l_status) {
        is_consumed_ = true;
        return have_output;
      }
      cur_l_tuple_is_joined_ = false;
      right_executor_->Init();
      if (have_output) {
        return true;
      }
      continue;
    }

    /**
     * 不符合 JOIN 条件?
     */
    if (to_plan_->Predicate() != nullptr) {
      auto pred_val{to_plan_->Predicate()->EvaluateJoin(&cur_l_tuple_, to_plan_->GetLeftPlan()->OutputSchema(),
                                                        &cur_r_tuple, to_plan_->GetRightPlan()->OutputSchema())};
      BUSTUB_ASSERT(pred_val.GetTypeId() == TypeId::BOOLEAN, "Unexpected.");
      if (pred_val.IsNull() || !pred_val.GetAs<bool>()) {
        continue;
      }
    }
    cur_l_tuple_is_joined_ = true;
    std::vector<Value> output_values{};
    output_values.reserve(to_plan_->OutputSchema().GetColumnCount());
    const auto &left_schema{to_plan_->GetLeftPlan()->OutputSchema()};
    const auto &right_schema{to_plan_->GetRightPlan()->OutputSchema()};
    for (size_t i = 0; i < left_schema.GetColumnCount(); i++) {
      output_values.emplace_back(cur_l_tuple_.GetValue(&left_schema, i));
    }
    for (size_t i = 0; i < right_schema.GetColumnCount(); i++) {
      output_values.emplace_back(cur_r_tuple.GetValue(&right_schema, i));
    }
    *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
    *rid = RID{INVALID_PAGE_ID, 0};
    return true;
  }
}

}  // namespace bustub
