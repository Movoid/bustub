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
#include "execution/execution_common.h"
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
  rids_.clear();
  next_predkey_pos_ = 0;
  next_rid_pos_ = 0;
  to_iter_.reset();

  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->table_oid_).get();
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

  auto to_txn_mgr{exec_ctx_->GetTransactionManager()};
  auto to_txn{exec_ctx_->GetTransaction()};

  auto to_table{to_table_info_->table_.get()};
  const auto &table_schema{to_table_info_->schema_};

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

      auto [cur_meta, cur_tuple]{to_table->GetTuple(cur_rid)};

      /**
       * MVCC.
       */
      auto undone_tuple_opt{GetTupleByTxnReadTs(to_txn_mgr, to_txn, to_table_info_, cur_rid)};
      if (!undone_tuple_opt.has_value()) {
        continue;
      }
      cur_tuple = std::move(undone_tuple_opt.value());
      auto undone_key_tuple{
          cur_tuple.KeyFromTuple(table_schema, *to_real_index_->GetKeySchema(), to_real_index_->GetKeyAttrs())};
      IntegerKeyType_BTree undone_index_key{};
      undone_index_key.SetFromKey(undone_key_tuple);
      if (GenericComparator<TWO_INTEGER_SIZE_B_TREE>{to_real_index_->GetKeySchema()}(cur_key, undone_index_key) != 0) {
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
    Tuple key_tuple{};
    if (next_rid_pos_ == 0) {
      BUSTUB_ASSERT(rids_.empty(), "Unexpected.");
      std::vector<Value> key_values{};
      key_values.reserve(1);

      BUSTUB_ASSERT(dynamic_cast<ConstantValueExpression *>(to_plan_->pred_keys_[next_predkey_pos_].get()) != nullptr,
                    "Unexpected expression.");

      key_values.emplace_back(to_plan_->pred_keys_[next_predkey_pos_]->Evaluate(nullptr, Schema{{}}));
      key_tuple = Tuple{std::move(key_values), to_real_index_->GetKeySchema()};
      to_real_index_->ScanKey(key_tuple, &rids_, exec_ctx_->GetTransaction());
      BUSTUB_ASSERT(rids_.size() <= 1, "Unexpected.");
      if (rids_.empty()) {
        next_predkey_pos_++;
        continue;
      }
    }

    BUSTUB_ASSERT(next_predkey_pos_ < static_cast<int32_t>(to_plan_->pred_keys_.size()) &&
                      next_rid_pos_ < static_cast<int32_t>(rids_.size()),
                  "Unexpected.");
    BUSTUB_ASSERT(rids_.size() == 1, "Unexepcted.");
    BUSTUB_ASSERT(next_rid_pos_ == 0, "Unexpected.");

    const auto &cur_rid{rids_[next_rid_pos_]};
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

    /**
     * MVCC.
     */
    auto undone_tuple_opt{GetTupleByTxnReadTs(to_txn_mgr, to_txn, to_table_info_, cur_rid)};
    if (!undone_tuple_opt.has_value()) {
      continue;
    }
    cur_tuple = std::move(undone_tuple_opt.value());
    auto undone_key_tuple{
        cur_tuple.KeyFromTuple(table_schema, *to_real_index_->GetKeySchema(), to_real_index_->GetKeyAttrs())};
    if (!IsTupleContentEqual(key_tuple, undone_key_tuple)) {
      continue;
    }

    *tuple = cur_tuple;
    *rid = cur_rid;
    return true;
  }
}

}  // namespace bustub
