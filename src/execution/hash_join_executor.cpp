//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new HashJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The HashJoin join plan to be executed
 * @param left_child The child executor that produces tuples for the left side of join
 * @param right_child The child executor that produces tuples for the right side of join
 *
 * bustub 假设 DRAM 足够承载 HashMap.
 * 实现 Grace Hash Join 不是必要的.
 *
 * HashMap 需要存储 Tuple 而不是 RID.
 * 因为临时 Tuple 没有 RID.
 */
HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor{exec_ctx},
      to_plan_{plan},
      to_left_executor_{std::move(left_child)},
      to_right_executor_{std::move(right_child)} {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {  // NOLINT
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Initialize the join
 *
 * 貌似可以保留 `right_ht_` 的数据.
 * 没有必要重新构建.
 */
void HashJoinExecutor::Init() {
  //  UNIMPLEMENTED("TODO(P3): Add implementation.");
  cur_left_key_vals_.clear();
  cur_left_tuple_is_joined_ = false;
  next_rid_pos_ = 0;
  is_consumed_ = false;

  to_left_executor_->Init();
  to_right_executor_->Init();

  const auto &right_schema{to_plan_->GetRightPlan()->OutputSchema()};

  BUSTUB_ASSERT(!to_plan_->LeftJoinKeyExpressions().empty(), "Unexpected.");
  BUSTUB_ASSERT(!to_plan_->RightJoinKeyExpressions().empty(), "Unexpected.");
  BUSTUB_ASSERT(to_plan_->LeftJoinKeyExpressions().size() == to_plan_->RightJoinKeyExpressions().size(), "Unexpected.");

  Tuple cur_r_tuple{};
  RID cur_r_rid{};
  if (right_ht_.empty()) {
    while (to_right_executor_->Next(&cur_r_tuple, &cur_r_rid)) {
      std::vector<Value> key_values{};
      key_values.reserve(to_plan_->RightJoinKeyExpressions().size());
      for (auto &to_left_key_expr : to_plan_->RightJoinKeyExpressions()) {
        key_values.emplace_back(to_left_key_expr->Evaluate(&cur_r_tuple, right_schema));
      }
      BUSTUB_ASSERT(!key_values.empty(), "Unexpected.");
      /**
       * 没有类似 `GetKeySchema()` 的接口, 无法构造, 也没有必要构造 Tuple.
       */
      hash_t combined_hash{HashUtil::HashValue(&key_values[0])};  // NOLINT
      for (size_t i = 1; i < key_values.size(); i++) {
        combined_hash = HashUtil::CombineHashes(combined_hash, HashUtil::HashValue(&key_values[i]));
      }
      right_ht_[combined_hash].emplace_back(std::move(cur_r_tuple));
    }
  }
}

/**
 * Yield the next tuple from the join.
 * @param[out] tuple The next tuple produced by the join.
 * @param[out] rid The next tuple RID, not used by hash join.
 * @return `true` if a tuple was produced, `false` if there are no more tuples.
 */
auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  if (is_consumed_) {
    return false;
  }

  const auto &left_schema{to_plan_->GetLeftPlan()->OutputSchema()};
  const auto &right_schema{to_plan_->GetRightPlan()->OutputSchema()};

  /**
   * 逻辑和 NestedIndexJoinExecutor 类似.
   */
  while (true) {
    if (next_rid_pos_ == 0) {
      auto left_status{to_left_executor_->Next(&cur_left_tuple_, &cur_left_rid_)};
      if (!left_status) {
        is_consumed_ = true;
        return false;
      }

      cur_left_tuple_is_joined_ = false;
      std::vector<Value> key_values{};
      key_values.reserve(to_plan_->LeftJoinKeyExpressions().size());
      for (auto &to_left_expr : to_plan_->LeftJoinKeyExpressions()) {
        key_values.emplace_back(to_left_expr->Evaluate(&cur_left_tuple_, left_schema));
      }
      hash_t combined_hash{HashUtil::HashValue(&key_values[0])};  // NOLINT
      for (size_t i = 1; i < key_values.size(); i++) {
        combined_hash = HashUtil::CombineHashes(combined_hash, HashUtil::HashValue(&key_values[i]));
      }
      cur_left_key_vals_ = std::move(key_values);  // 保存.
      cur_left_key_hash_ = combined_hash;
    }

    auto to_right_bucket{right_ht_.find(cur_left_key_hash_)};

    /**
     * 找到匹配的 right tuple.
     */
    while (to_right_bucket != right_ht_.end() && next_rid_pos_ < static_cast<int32_t>(to_right_bucket->second.size())) {
      bool is_matched{true};
      for (size_t i = 0; i < to_plan_->RightJoinKeyExpressions().size(); i++) {
        const auto &left_val{cur_left_key_vals_[i]};
        const auto right_val{
            to_plan_->RightJoinKeyExpressions()[i]->Evaluate(&to_right_bucket->second[next_rid_pos_], right_schema)};
        /**
         * 同为 NULL 不被视为相等.
         */
        if (left_val.CompareEquals(right_val) != CmpBool::CmpTrue) {
          is_matched = false;
          break;
        }
      }
      if (is_matched) {
        break;
      }
      next_rid_pos_++;
    }

    if (to_right_bucket == right_ht_.end() || next_rid_pos_ == static_cast<int32_t>(to_right_bucket->second.size())) {
      bool left_join_need_ret{to_plan_->GetJoinType() == JoinType::LEFT && !cur_left_tuple_is_joined_};
      if (left_join_need_ret) {
        std::vector<Value> output_values{};
        output_values.reserve(to_plan_->OutputSchema().GetColumnCount());
        for (size_t i = 0; i < left_schema.GetColumnCount(); i++) {
          output_values.emplace_back(cur_left_tuple_.GetValue(&left_schema, i));
        }
        for (size_t i = 0; i < right_schema.GetColumnCount(); i++) {
          output_values.emplace_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
        }
        *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
        *rid = RID{INVALID_PAGE_ID, 0};
      }
      next_rid_pos_ = 0;
      if (left_join_need_ret) {
        return true;
      }
      continue;
    }

    BUSTUB_ASSERT(to_right_bucket != right_ht_.end(), "Unexpected.");
    BUSTUB_ASSERT(!to_right_bucket->second.empty(), "Unexpected.");
    BUSTUB_ASSERT(next_rid_pos_ < static_cast<int32_t>(to_right_bucket->second.size()), "Unexpected.");

    const auto &cur_right_tuple{to_right_bucket->second[next_rid_pos_]};
    next_rid_pos_++;
    cur_left_tuple_is_joined_ = true;
    std::vector<Value> output_values{};
    output_values.reserve(to_plan_->OutputSchema().GetColumnCount());

    BUSTUB_ASSERT(
        to_plan_->OutputSchema().GetColumnCount() == left_schema.GetColumnCount() + right_schema.GetColumnCount(),
        "Unexpected.");

    for (size_t i = 0; i < left_schema.GetColumnCount(); i++) {
      output_values.emplace_back(cur_left_tuple_.GetValue(&left_schema, i));
    }
    for (size_t i = 0; i < right_schema.GetColumnCount(); i++) {
      output_values.emplace_back(cur_right_tuple.GetValue(&right_schema, i));
    }
    *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
    *rid = RID{INVALID_PAGE_ID, 0};
    return true;
  }
}

}  // namespace bustub
