//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Creates a new nested index join executor.
 * @param exec_ctx the context that the nested index join should be performed in
 * @param plan the nested index join plan to be executed
 * @param child_executor the outer table
 *
 * 注意这里只有一个 Child.
 */
NestIndexJoinExecutor::NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_outer_executor_{std::move(child_executor)} {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {  // NOLINT
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

void NestIndexJoinExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  rids_.clear();
  next_rid_pos_ = 0;
  cur_outer_tuple_is_joined_ = false;
  is_consumed_ = false;

  to_outer_executor_->Init();

  to_inner_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetInnerTableOid()).get();
  to_index_info_ = exec_ctx_->GetCatalog()->GetIndex(to_plan_->GetIndexOid()).get();

  to_real_index_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(to_index_info_->index_.get());

  BUSTUB_ASSERT(to_real_index_ != nullptr, "Unsupported index type.");
}

auto NestIndexJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
  if (is_consumed_) {
    return false;
  }

  const auto &outer_schema{to_outer_executor_->GetOutputSchema()};
  const auto &inner_schema{to_plan_->InnerTableSchema()};

  while (true) {
    /**
     * 使用新的 Outer Tuple?
     * 加载 `rids_`.
     */
    if (next_rid_pos_ == 0) {
      BUSTUB_ASSERT(rids_.empty(), "Unexpected.");
      auto outer_status{to_outer_executor_->Next(&cur_outer_tuple_, &cur_outer_rid_)};
      if (!outer_status) {
        is_consumed_ = true;
        return false;
      }
      cur_outer_tuple_is_joined_ = false;

      std::vector<Value> key_values{};
      key_values.reserve(1);
      key_values.emplace_back(to_plan_->KeyPredicate()->Evaluate(&cur_outer_tuple_, outer_schema));
      Tuple key_tuple{std::move(key_values), to_real_index_->GetKeySchema()};
      to_real_index_->ScanKey(key_tuple, &rids_, exec_ctx_->GetTransaction());
      BUSTUB_ASSERT(rids_.size() <= 1, "Unexpected.");
    }

    /**
     * 在加载 `rids_` 后,
     * `rids_` 是否用尽?
     */
    if (rids_.empty() || next_rid_pos_ == static_cast<int32_t>(rids_.size())) {
      bool left_join_need_ret{to_plan_->GetJoinType() == JoinType::LEFT && !cur_outer_tuple_is_joined_};
      if (left_join_need_ret) {
        std::vector<Value> output_values{};
        output_values.reserve(to_plan_->OutputSchema().GetColumnCount());
        for (size_t i = 0; i < outer_schema.GetColumnCount(); i++) {
          output_values.emplace_back(cur_outer_tuple_.GetValue(&outer_schema, i));
        }
        for (size_t i = 0; i < inner_schema.GetColumnCount(); i++) {
          output_values.emplace_back(ValueFactory::GetNullValueByType(inner_schema.GetColumn(i).GetType()));
        }
        *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
        *rid = RID{INVALID_PAGE_ID, 0};
      }
      rids_.clear();
      next_rid_pos_ = 0;
      if (left_join_need_ret) {
        return true;
      }
      continue;
    }

    BUSTUB_ASSERT(!rids_.empty(), "Unexpected.");
    BUSTUB_ASSERT(next_rid_pos_ < static_cast<int32_t>(rids_.size()), "Unexpected.");

    auto to_inner_table{to_inner_table_info_->table_.get()};
    auto [cur_inner_meta, cur_inner_tuple]{to_inner_table->GetTuple(rids_[next_rid_pos_])};
    next_rid_pos_++;
    /**
     * 同 Index Scan, 需要检查是否是 tombstone.
     */
    if (cur_inner_meta.is_deleted_) {
      continue;
    }

    cur_outer_tuple_is_joined_ = true;
    std::vector<Value> output_values{};
    output_values.reserve(to_plan_->OutputSchema().GetColumnCount());

    BUSTUB_ASSERT(
        to_plan_->OutputSchema().GetColumnCount() == outer_schema.GetColumnCount() + inner_schema.GetColumnCount(),
        "Unexpected.");

    for (size_t i = 0; i < outer_schema.GetColumnCount(); i++) {
      output_values.emplace_back(cur_outer_tuple_.GetValue(&outer_schema, i));
    }
    for (size_t i = 0; i < inner_schema.GetColumnCount(); i++) {
      output_values.emplace_back(cur_inner_tuple.GetValue(&inner_schema, i));
    }
    *tuple = Tuple{std::move(output_values), &to_plan_->OutputSchema()};
    *rid = RID{INVALID_PAGE_ID, 0};
    return true;
  }
}

}  // namespace bustub
