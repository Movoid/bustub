//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.h
//
// Identification: src/include/execution/executors/nested_index_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/nested_index_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * NestedIndexJoinExecutor executes index join operations.
 */
class NestIndexJoinExecutor : public AbstractExecutor {
 public:
  NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                        std::unique_ptr<AbstractExecutor> &&child_executor);

  /** @return The output schema for the nested index join */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); }

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

 private:
  /** The nested index join plan node. */
  const NestedIndexJoinPlanNode *to_plan_;

  std::unique_ptr<AbstractExecutor> to_outer_executor_;

  const TableInfo *to_inner_table_info_{};
  const IndexInfo *to_index_info_{};  // Inner

  BPlusTreeIndexForTwoIntegerColumn *to_real_index_{};
  std::vector<RID> rids_;
  int32_t next_rid_pos_{};

  Tuple cur_outer_tuple_;
  RID cur_outer_rid_{INVALID_PAGE_ID, 0};
  bool cur_outer_tuple_is_joined_{};
  bool is_consumed_{};
};
}  // namespace bustub
