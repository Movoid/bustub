//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

#include "execution/executors/update_executor.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 *
 * Update Executor 只有一个子 Executor 作为输入.
 *
 * 关于 MVCC 支持,
 * UpdateExecutor 需要在 RID 位置上原地修改.
 * 若实现为 Delete + Insert, 若由于某 index key 改变, 使得插入新的 index entry,
 * 若 Child Executor 是 Index Scan, 会造成 Halloween problem.
 * 必须实现为先收集 RID 再修改.
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor{exec_ctx}, to_plan_{plan}, to_child_executor_{std::move(child_executor)} {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Initialize the update
 */
void UpdateExecutor::Init() {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  updated_cnt_ = 0;
  is_consumed_ = false;

  /**
   * 记得先 Init 子 Executor.
   */
  to_child_executor_->Init();
  to_table_info_ = exec_ctx_->GetCatalog()->GetTable(to_plan_->GetTableOid()).get();
}

/**
 * Yield the next tuple from the update.
 * @param[out] tuple The next tuple produced by the update
 * @param[out] rid The next tuple RID produced by the update (ignore this)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: UpdateExecutor::Next() does not use the `rid` out-parameter.
 *
 * Update 操作需要同时更新 Index.
 *
 * OutputSchema 中只有 1 Column, 1 Row, 类型 bustub::INTEGER,
 * 名字为 "__bustub_internal.update_rows".
 * Update Executor 调用 `Next` 后需要一直执行直到处理完所有输入,
 * 并输出这唯一一行计数.
 *
 * 这个计数表不需要 RID.
 */
auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // UNIMPLEMENTED("TODO(P3): Add implementation.");

  if (is_consumed_) {
    return false;
  }

  auto to_txn_mgr{exec_ctx_->GetTransactionManager()};
  auto to_txn{exec_ctx_->GetTransaction()};

  const auto to_all_index_info{exec_ctx_->GetCatalog()->GetTableIndexes(to_table_info_->name_)};
  auto &to_table{to_table_info_->table_};
  const auto &schema{to_table_info_->schema_};

  /**
   * 下层的 Output Schema 必须和 Table Schema 相同.
   * 因为 `Index::GetKeyAttrs()` 返回的始终是基于 Table Schema 的 Column 下标.
   */
  BUSTUB_ASSERT(to_table_info_->schema_.GetColumnCount() == to_child_executor_->GetOutputSchema().GetColumnCount(),
                "Unexpected Schema.");

  /**
   * pair<RID, 更新后 Tuple>.
   */
  std::vector<std::pair<RID, Tuple>> need_update_pairs{};

  Tuple input_tuple{};
  RID input_rid{};
  while (to_child_executor_->Next(&input_tuple, &input_rid)) {
    need_update_pairs.emplace_back(input_rid, std::move(input_tuple));
  }

  for (const auto &update_pair : need_update_pairs) {
    const auto &[cur_rid, cur_tuple]{update_pair};
    std::vector<Value> new_values{};
    new_values.reserve(schema.GetColumnCount());
    for (auto &to_expr : to_plan_->target_expressions_) {
      new_values.emplace_back(to_expr->Evaluate(&cur_tuple, schema));
    }
    Tuple new_tuple{std::move(new_values), &schema};

    auto old_meta{to_table->GetTupleMeta(cur_rid)};
    TupleMeta new_meta{to_txn->GetTransactionTempTs(), false};
    auto old_undo_link{to_txn_mgr->GetUndoLink(cur_rid)};
    /**
     * MVCC.
     */
    if ((old_meta.ts_ & TXN_START_ID) > 0) {
      if (old_meta.ts_ != to_txn->GetTransactionTempTs()) {
        to_txn->SetTainted();
        throw ExecutionException{"Trying update a tuple which is being modified by another txn."};
      }
      /**
       * 若 `old_meta.ts_` 是当前事务的 temp ts,
       * 则在这个 Tuple 上的修改都会被此 temp ts 阻止.
       * 当前事务可以安全修改.
       */

      BUSTUB_ASSERT(old_meta == new_meta, "Unexpected.");
      if (!old_undo_link.has_value()) {
        /**
         * 事务先 Insert 再 Update, 则这个 Tuple 没有 UndoLog.
         */
        to_table->UpdateTupleInPlace(new_meta, new_tuple, cur_rid);
      } else {
        /**
         * 此 Tuple 被当前事务修改过, 且存在修改前版本的 UndoLog.
         */
        auto gen_undo_log{GenerateUpdatedUndoLog(&to_table_info_->schema_, &cur_tuple, &new_tuple,
                                                 to_txn_mgr->GetUndoLog(old_undo_link.value()))};
        BUSTUB_ASSERT(old_undo_link.value().prev_txn_ == to_txn->GetTransactionId(), "Unexpected.");
        to_txn->ModifyUndoLog(old_undo_link.value().prev_log_idx_, std::move(gen_undo_log));
        to_table->UpdateTupleInPlace(new_meta, new_tuple, cur_rid);
      }
    } else {
      if (old_meta.ts_ > to_txn->GetReadTs()) {
        to_txn->SetTainted();
        throw ExecutionException{"Trying update a tuple which has been updated in the future."};
      }
      /**
       * 此 Tuple 可能没有 UndoLog.
       *
       * UpdateTupleAndUndoLink 需要执行 WW-Conflict 检查.
       *
       * WARNING: 注意当前事务此时没有持有任何 Latch.
       * 当前事务从 Child Executor 得到 Tuple 后,
       * 若有其他事务启动,
       * 并通过 `UpdateTupleAndUndoLink()` 成功修改了 TableHeap + UndoLog,
       * 且 **没有 Commit**,
       * 则 `old_undo_link` 得到的是被其他事务修改后的结果.
       * 这种 Race 应该被 Check WW-Conflict 检测到.
       */

      auto gen_undo_log{GenerateNewUndoLog(&to_table_info_->schema_, &cur_tuple, &new_tuple, old_meta.ts_,
                                           old_undo_link.value_or(UndoLink{}))};
      auto gen_undo_link{to_txn->AppendUndoLog(std::move(gen_undo_log))};
      auto check_ww_conflict{[&](const TupleMeta &base_meta, const Tuple &base_tuple, RID rid,
                                 std::optional<UndoLink> gen_undo_link) -> bool {
        if (base_meta.ts_ > to_txn->GetReadTs()) {
          return false;  // NOLINT
        }
        return true;
      }};

      if (!UpdateTupleAndUndoLink(to_txn_mgr, cur_rid, gen_undo_link, to_table.get(), to_txn, new_meta, new_tuple,
                                  check_ww_conflict)) {
        to_txn->SetTainted();
        throw ExecutionException{"Check WW-Conflict failed."};
      }
    }

    to_txn->AppendWriteSet(to_table_info_->oid_, cur_rid);

    /**
     * 处理 Index.
     */
    for (auto &to_index_info : to_all_index_info) {
      auto &cur_index{to_index_info->index_};
      /**
       * 注意, 需要传递 key 的 Tuple 而不是完整 Tuple.
       */
      auto old_key_tuple{
          cur_tuple.KeyFromTuple(to_table_info_->schema_, to_index_info->key_schema_, cur_index->GetKeyAttrs())};
      auto new_key_tuple{
          new_tuple.KeyFromTuple(to_table_info_->schema_, to_index_info->key_schema_, cur_index->GetKeyAttrs())};
      if (IsTupleContentEqual(old_key_tuple, new_key_tuple)) {
        continue;
      }
      auto status{cur_index->InsertEntry(new_key_tuple, cur_rid, exec_ctx_->GetTransaction())};
      BUSTUB_ASSERT(status, "Unexpected fail.");
    }
    /**
     * End.
     */
    updated_cnt_++;
  }

  /**
   * 返回统计结果.
   */
  std::vector<Value> output_values{};
  output_values.reserve(GetOutputSchema().GetColumnCount());
  BUSTUB_ASSERT(output_values.capacity() == 1, "Unexpected plan.");
  output_values.emplace_back(TypeId::INTEGER, updated_cnt_);
  *tuple = Tuple{std::move(output_values), &GetOutputSchema()};
  is_consumed_ = true;
  return true;
}

}  // namespace bustub
