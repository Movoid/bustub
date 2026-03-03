//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/index_scan_executor.h"
#include <typeindex>
#include "common/macros.h"
#include "execution/expressions/constant_value_expression.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 *
 * IndexScanExecutor 用于单点 key 查找, 或顺序扫描.
 * 只支持 ORDER BY ... ASC 顺序.
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor{exec_ctx}, to_plan_{plan} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

void IndexScanExecutor::Init() {
  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
  auto to_index{exec_ctx_->GetCatalog()->GetIndex(to_plan_->GetIndexOid())->index_.get()};
  to_real_index_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(to_index);

  BUSTUB_ASSERT(to_real_index_ != nullptr, "Unsupported index type.");

  /**
   * 没有定点查找则使用 Iterator 从头顺序扫描.
   */
  if (to_plan_->pred_keys_.empty()) {
    to_iter_ = std::make_unique<BPlusTreeIndexIteratorForTwoIntegerColumn>(to_real_index_->GetBeginIterator());
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  /**
   * 顺序扫描.
   */
  if (to_plan_->pred_keys_.empty()) {
    while (true) {
      BUSTUB_ASSERT(to_iter_ != nullptr, "Unexepcted.");
      if (to_iter_->IsEnd()) {
        return false;
      }

      const auto &[cur_key, cur_rid]{*(*to_iter_)};
      ++(*to_iter_);

      auto to_table{to_table_info_->table_.get()};
      auto [cur_meta, cur_tuple]{to_table->GetTuple(cur_rid)};
      if (cur_meta.is_deleted_) {
        continue;
      }

      *tuple = cur_tuple;
      *rid = cur_rid;
      return true;
    }
  }
  /**
   * 单点搜索.
   */
  while (true) {
    if (next_predkey_pos_ == static_cast<int32_t>(to_plan_->pred_keys_.size())) {
      return false;
    }

    /**
     * 填充 `rids_`.
     */
    if (next_rid_pos_ == 0) {
      BUSTUB_ASSERT(rids_.size() == 0, "Unexpected.");
      std::vector<Value> key_values{};
      key_values.reserve(1);

      BUSTUB_ASSERT(dynamic_cast<ConstantValueExpression *>(to_plan_->pred_keys_[next_predkey_pos_].get()) != nullptr,
                    "Unexpected expression.");

      key_values.emplace_back(to_plan_->pred_keys_[next_predkey_pos_]->Evaluate(nullptr, Schema{{}}));
      Tuple key_tuple{key_values, to_real_index_->GetKeySchema()};
      to_real_index_->ScanKey(key_tuple, &rids_, exec_ctx_->GetTransaction());
      BUSTUB_ASSERT(rids_.size() <= 1, "Unexpected.");
      if (rids_.size() == 0) {
        next_predkey_pos_++;
        continue;
      }
    }

    BUSTUB_ASSERT(next_predkey_pos_ < static_cast<int32_t>(to_plan_->pred_keys_.size()) &&
                      next_rid_pos_ < static_cast<int32_t>(rids_.size()),
                  "Unexpected.");
    BUSTUB_ASSERT(rids_.size() == 1, "Unexepcted.");
    BUSTUB_ASSERT(next_rid_pos_ == 0, "Unexpected.");

    auto to_table{to_table_info_->table_.get()};
    auto cur_rid{rids_[next_rid_pos_]};
    auto [cur_meta, cur_tuple]{to_table->GetTuple(cur_rid)};

    /**
     * 切换到下一个取值.
     */
    next_rid_pos_++;
    if (next_rid_pos_ == static_cast<int32_t>(rids_.size())) {
      next_predkey_pos_++;
      rids_.clear();
      next_rid_pos_ = 0;
    }

    if (cur_meta.is_deleted_) {
      continue;
    }

    *tuple = cur_tuple;
    *rid = cur_rid;
    return true;
  }
}

}  // namespace bustub
