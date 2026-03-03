//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.h
//
// Identification: src/include/execution/executors/insert_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/insert_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * InsertExecutor executes an insert on a table.
 * Inserted values are always pulled from a child executor.
 */
class InsertExecutor : public AbstractExecutor {
 public:
  InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                 std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the insert */
  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); };

 private:
  /** The insert plan node to be executed*/
  const InsertPlanNode *to_plan_{};

  std::unique_ptr<AbstractExecutor> to_child_executor_;

  const TableInfo *to_table_info_{};
  int32_t inserted_cnt_{};
  bool is_consumed_{};
};

}  // namespace bustub
