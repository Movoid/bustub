//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new SeqScanExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The sequential scan plan to be executed
 */
SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor{exec_ctx}, to_plan_{plan} {
  // no-op
}

/** Initialize the sequential scan */
void SeqScanExecutor::Init() {
  to_iter_.reset();

  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
  to_iter_ = std::make_unique<TableIterator>(to_table_info_->table_->MakeIterator());
}

/**
 * Yield the next tuple from the sequential scan.
 * @param[out] tuple The next tuple produced by the scan
 * @param[out] rid The next tuple RID produced by the scan
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  /**
   * SeqScan 没有子 Executor, 必须直接从 Table 读取.
   * 对于其他更高层的 Executor, 需要先从子 Executor 获得输入.
   */

  while (true) {
    while (!to_iter_->IsEnd() && to_iter_->GetTuple().first.is_deleted_) {
      ++(*to_iter_);
    }

    if (to_iter_->IsEnd()) {
      return false;
    }

    const auto [cur_meta, cur_tuple]{to_iter_->GetTuple()};
    BUSTUB_ASSERT(!(cur_tuple.GetRid() == RID{INVALID_PAGE_ID, 0}), "Invalid RID.");
    ++(*to_iter_);
    /**
     * 使用 Plan 中的, 通过谓词下推获得的 `filter_predicate_` 来过滤.
     */
    if (to_plan_->filter_predicate_ != nullptr) {
      auto pred_value{to_plan_->filter_predicate_->Evaluate(&cur_tuple, to_table_info_->schema_)};
      if (pred_value.IsNull() || !pred_value.GetAs<bool>()) {
        continue;
      }
    }

    *tuple = cur_tuple;
    *rid = cur_tuple.GetRid();
    return true;
  }
}

}  // namespace bustub
