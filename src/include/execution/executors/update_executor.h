//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.h
//
// Identification: src/include/execution/executors/update_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/update_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * UpdateExecutor executes an update on a table.
 * Updated values are always pulled from a child.
 */
class UpdateExecutor : public AbstractExecutor {
  friend class UpdatePlanNode;

 public:
  UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                 std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the update */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); }

 private:
  /** The update plan node to be executed */
  const UpdatePlanNode *to_plan_{};

  /** The child executor to obtain value from */
  std::unique_ptr<AbstractExecutor> to_child_executor_;

  /** Metadata identifying the table that should be updated */
  const TableInfo *to_table_info_{};

  int32_t updated_cnt_{};
  bool is_consumed_{};
};
}  // namespace bustub
