//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"
#include "concurrency/transaction_manager.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

/**
 * Construct a new InsertExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled
 */
InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the insert */
void InsertExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  inserted_cnt_ = 0;
  is_consumed_ = false;

  to_child_executor_->Init();
  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
}

/**
 * Yield the number of rows inserted into the table.
 * @param[out] tuple The integer tuple indicating the number of rows inserted into the table
 * @param[out] rid The next tuple RID produced by the insert (ignore, not used)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: InsertExecutor::Next() does not use the `rid` out-parameter.
 * NOTE: InsertExecutor::Next() returns true with number of inserted rows produced only once.
 *
 * 类似 UpdateExecutor, 一直执行,
 * 最后输出插入元组数量.
 */
auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  if (is_consumed_) {
    return false;
  }

  auto to_txn{exec_ctx_->GetTransaction()};

  const auto to_all_index_info{exec_ctx_->GetCatalog()->GetTableIndexes(to_table_info_->name_)};

  BUSTUB_ASSERT(to_table_info_->schema_.GetColumnCount() == to_child_executor_->GetOutputSchema().GetColumnCount(),
                "Unexpected Schema.");

  Tuple cur_tuple{};
  RID cur_rid{};

  while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
    /**
     * 插入新 Tuple.
     */
    auto &to_table{to_table_info_->table_};
    TupleMeta insert_meta{to_txn->GetTransactionTempTs(), false};
    auto insert_rid_opt{to_table->InsertTuple(insert_meta, cur_tuple)};
    BUSTUB_ASSERT(insert_rid_opt.has_value(), "Unexepcted fail.");
    auto insert_rid{insert_rid_opt.value()};

    /**
     * MVCC.
     * 元组从不存在变为存在. 没有 Undo Log.
     * 需要设置临时 timestamp.
     */
    to_txn->AppendWriteSet(to_plan_->GetTableOid(), insert_rid);

    /**
     * 插入新 Index Entry.
     */
    for (auto &to_index_info : to_all_index_info) {
      auto &cur_index{to_index_info->index_};
      auto insert_key_tuple{
          cur_tuple.KeyFromTuple(to_table_info_->schema_, to_index_info->key_schema_, cur_index->GetKeyAttrs())};
      auto status{cur_index->InsertEntry(insert_key_tuple, insert_rid, exec_ctx_->GetTransaction())};
      BUSTUB_ASSERT(status, "Unexpected fail.");
    }
    inserted_cnt_++;
  }

  /**
   * 返回统计结果.
   */
  std::vector<Value> output_values{};
  output_values.reserve(GetOutputSchema().GetColumnCount());
  BUSTUB_ASSERT(output_values.capacity() == 1, "Unexpected plan.");
  output_values.emplace_back(TypeId::INTEGER, inserted_cnt_);
  *tuple = Tuple{std::move(output_values), &GetOutputSchema()};
  is_consumed_ = true;
  return true;
}

}  // namespace bustub
