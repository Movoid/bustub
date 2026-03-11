//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"
#include "execution/executors/delete_executor.h"

namespace bustub {

/**
 * Construct a new DeleteExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The delete plan to be executed
 * @param child_executor The child executor that feeds the delete
 */
DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the delete */
void DeleteExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  deleted_cnt_ = 0;
  is_consumed_ = false;

  to_child_executor_->Init();
  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
}

/**
 * Yield the number of rows deleted from the table.
 * @param[out] tuple The integer tuple indicating the number of rows deleted from the table
 * @param[out] rid The next tuple RID produced by the delete (ignore, not used)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: DeleteExecutor::Next() does not use the `rid` out-parameter.
 * NOTE: DeleteExecutor::Next() returns true with the number of deleted rows produced only once.
 */
auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  if (is_consumed_) {
    return false;
  }

  auto to_txn_mgr{exec_ctx_->GetTransactionManager()};
  auto to_txn{exec_ctx_->GetTransaction()};

  const auto to_all_index_info{exec_ctx_->GetCatalog()->GetTableIndexes(to_table_info_->name_)};
  auto &to_table{to_table_info_->table_};

  BUSTUB_ASSERT(to_table_info_->schema_.GetColumnCount() == to_child_executor_->GetOutputSchema().GetColumnCount(),
                "Unexpected Schema.");

  Tuple cur_tuple{};
  RID cur_rid{};
  while (to_child_executor_->Next(&cur_tuple, &cur_rid)) {
    /**
     * 删除 Tuple.
     */
    auto old_meta{to_table->GetTupleMeta(cur_rid)};
    TupleMeta new_meta{to_txn->GetTransactionTempTs(), true};
    auto old_undo_link{to_txn_mgr->GetUndoLink(cur_rid)};
    if ((old_meta.ts_ & TXN_START_ID) > 0) {
      if (old_meta.ts_ != to_txn->GetTransactionTempTs()) {
        to_txn->SetTainted();
        throw ExecutionException{"Trying update a tuple which is being modified by another txn."};
      }

      if (!old_undo_link.has_value()) {
        to_table->UpdateTupleMeta(new_meta, cur_rid);
      } else {
        auto gen_undo_log{GenerateUpdatedUndoLog(&to_table_info_->schema_, &cur_tuple, nullptr,
                                                 to_txn_mgr->GetUndoLog(old_undo_link.value()))};
        BUSTUB_ASSERT(old_undo_link.value().prev_txn_ == to_txn->GetTransactionId(), "Unexpected.");
        to_txn->ModifyUndoLog(old_undo_link.value().prev_log_idx_, std::move(gen_undo_log));
        to_table->UpdateTupleMeta(new_meta, cur_rid);
      }
    } else {
      if (old_meta.ts_ > to_txn->GetReadTs()) {
        to_txn->SetTainted();
        throw ExecutionException{"Trying update a tuple which has been updated in the future."};
      }

      auto gen_undo_log{GenerateNewUndoLog(&to_table_info_->schema_, &cur_tuple, nullptr, old_meta.ts_,
                                           old_undo_link.value_or(UndoLink{}))};
      auto gen_undo_link{to_txn->AppendUndoLog(std::move(gen_undo_log))};
      auto check_ww_conflict{[&](const TupleMeta &base_meta, const Tuple &base_tuple, RID rid,
                                 std::optional<UndoLink> gen_undo_link) -> bool {
        if (base_meta.ts_ > to_txn->GetReadTs()) {
          return false;  // NOLINT
        }
        return true;
      }};

      if (!UpdateTupleAndUndoLink(to_txn_mgr, cur_rid, gen_undo_link, to_table.get(), to_txn, new_meta, cur_tuple,
                                  check_ww_conflict)) {
        to_txn->SetTainted();
        throw ExecutionException{"Check WW-Conflict failed."};
      }
    }

    to_txn->AppendWriteSet(to_table_info_->oid_, cur_rid);

    /**
     * 对所有 Index, 删除 Index Entry.
     */
    // for (auto &to_index_info : to_all_index_info) {
    //   auto &cur_index{to_index_info->index_};
    //   auto delete_key_tuple{
    //       cur_tuple.KeyFromTuple(to_table_info_->schema_, to_index_info->key_schema_, cur_index->GetKeyAttrs())};
    //   cur_index->DeleteEntry(delete_key_tuple, cur_rid, exec_ctx_->GetTransaction());
    // }
    deleted_cnt_++;
  }

  /**
   * 返回统计结果.
   */
  std::vector<Value> output_values{};
  output_values.reserve(GetOutputSchema().GetColumnCount());
  BUSTUB_ASSERT(output_values.capacity() == 1, "Unexpected plan.");
  output_values.emplace_back(TypeId::INTEGER, deleted_cnt_);
  *tuple = Tuple{std::move(output_values), &GetOutputSchema()};
  is_consumed_ = true;
  return true;
}

}  // namespace bustub
