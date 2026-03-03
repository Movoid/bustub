//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.h
//
// Identification: src/include/execution/executors/delete_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/delete_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * DeletedExecutor executes a delete on a table.
 * Deleted values are always pulled from a child.
 */
class DeleteExecutor : public AbstractExecutor {
 public:
  DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                 std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the delete */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); };

 private:
  /** The delete plan node to be executed */
  const DeletePlanNode *to_plan_;

  /** The child executor from which RIDs for deleted tuples are pulled */
  std::unique_ptr<AbstractExecutor> to_child_executor_;

  const TableInfo *to_table_info_{};
  int32_t deleted_cnt_{};
  bool is_consumed_{};
};
}  // namespace bustub
