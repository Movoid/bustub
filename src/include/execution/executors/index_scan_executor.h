//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.h
//
// Identification: src/include/execution/executors/index_scan_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "common/rid.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/index_scan_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * IndexScanExecutor executes an index scan over a table.
 */

class IndexScanExecutor : public AbstractExecutor {
 public:
  IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan);

  auto GetOutputSchema() const -> const Schema & override { return to_plan_->OutputSchema(); }

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

 private:
  /** The index scan plan node to be executed. */
  const IndexScanPlanNode *to_plan_;

  const TableInfo *to_table_info_{};

  BPlusTreeIndexForTwoIntegerColumn *to_real_index_{};

  /**
   * 单点查询支持.
   * 注意单点查询不要使用 BPlusTree
   */
  std::vector<RID> rids_;
  int32_t next_predkey_pos_{};
  int32_t next_rid_pos_{};

  /**
   * 顺序扫描支持.
   */
  std::unique_ptr<BPlusTreeIndexIteratorForTwoIntegerColumn> to_iter_;
};
}  // namespace bustub
