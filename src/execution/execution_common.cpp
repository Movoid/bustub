//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include "catalog/catalog.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

/** TODO(P3): Implement the comparison method */
auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
  BUSTUB_ASSERT(entry_a.first.size() == order_bys_.size(), "Unexpected.");
  BUSTUB_ASSERT(entry_b.first.size() == order_bys_.size(), "Unexpected.");

  for (size_t i = 0; i < order_bys_.size(); i++) {
    const auto &l_val = entry_a.first[i];
    const auto &r_val = entry_b.first[i];
    const auto is_less = l_val.CompareLessThan(r_val);
    const auto is_greater = l_val.CompareGreaterThan(r_val);
    BUSTUB_ASSERT(is_less != CmpBool::CmpNull && is_greater != CmpBool::CmpNull, "Unsupported.");

    switch (order_bys_[i].first) {
      case OrderByType::ASC:
      case OrderByType::DEFAULT:
        if (is_less == CmpBool::CmpTrue) {
          return true;
        }
        if (is_greater == CmpBool::CmpTrue) {
          return false;
        }
        break;
      case OrderByType::DESC:
        if (is_greater == CmpBool::CmpTrue) {
          return true;
        }
        if (is_less == CmpBool::CmpTrue) {
          return false;
        }
        break;
      case OrderByType::INVALID:
        BUSTUB_ASSERT(false, "Invalid.");
    }
  }
  return false;
}

/**
 * Generate sort key for a tuple based on the order by expressions.
 *
 * TODO(P3): Implement this method.
 */
auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  std::vector<Value> sort_keys{};
  sort_keys.reserve(order_bys.size());
  for (auto &order_by : order_bys) {
    sort_keys.emplace_back(order_by.second->Evaluate(&tuple, schema));
  }
  return sort_keys;
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 *
 * 遇到 `is_deleted_` UndoLog, 仍需要应用 `tuple_` 中的变更,
 * 才能回溯到更早的版本.
 */
auto ReconstructTuple(const Schema *to_schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  // UNIMPLEMENTED("not implemented");

  if (undo_logs.empty()) {
    if (base_meta.is_deleted_) {
      return std::nullopt;
    }
    return base_tuple;
  }

  std::vector<Value> values{};
  values.reserve(to_schema->GetColumnCount());
  for (uint32_t i = 0; i < to_schema->GetColumnCount(); i++) {
    values.emplace_back(base_tuple.GetValue(to_schema, i));
  }

  bool is_deleted{base_meta.is_deleted_};
  std::vector<uint32_t> attrs{};
  attrs.reserve(to_schema->GetColumnCount());
  for (const auto &undo_log : undo_logs) {
    for (size_t i = 0; i < undo_log.modified_fields_.size(); i++) {
      if (undo_log.modified_fields_[i]) {
        attrs.emplace_back(i);
      }
    }
    auto delta_schema{Schema::CopySchema(to_schema, attrs)};
    for (uint32_t i = 0; i < attrs.size(); i++) {
      values[attrs[i]] = undo_log.tuple_.GetValue(&delta_schema, i);
    }
    is_deleted = undo_log.is_deleted_;
    attrs.clear();
  }

  if (is_deleted) {
    return std::nullopt;
  }

  Tuple undone_tuple{std::move(values), to_schema};
  undone_tuple.SetRid(base_tuple.GetRid());
  return undone_tuple;
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 *
 * 注意对于当前事务, 此 Tuple 可能不存在.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *to_txn, TransactionManager *to_txn_mgr) -> std::optional<std::vector<UndoLog>> {
  // UNIMPLEMENTED("not implemented");
  auto read_ts{to_txn->GetReadTs()};

  if (base_meta.ts_ <= read_ts) {
    return std::vector<UndoLog>{};
  }

  /**
   * 未提交的事务会先构造 Undo Log, 再修改 TableHeap .
   * 未提交的版本有取值超大的临时 timestamp.
   *
   * 只有 TableHeap 中的 Tuple 可能有临时 timestamp.
   * Undo Log 不可能有临时 timestamp, 若允许则实际发生了脏写.
   */

  /**
   * 未提交的版本只有其对应的事务能读取.
   * 其他事务都不可读取.
   */

  if (base_meta.ts_ == to_txn->GetTransactionTempTs()) {
    return std::vector<UndoLog>{};
  }

  if (!undo_link.has_value()) {
    /**
     * Txn Manager 没有 Undo Link, 同时 `base_meta.ts_ > read_ts`,
     * 则这个 Tuple 是未来插入的.
     */
    return std::nullopt;
  }

  std::vector<UndoLog> needed_undo_logs{};

  BUSTUB_ASSERT(undo_link.has_value(), "Unexpected.");
  auto cur_undo_link{undo_link.value()};
  while (cur_undo_link.IsValid()) {
    BUSTUB_ASSERT(cur_undo_link.IsValid(), "Unexpected.");
    std::shared_ptr<Transaction> to_cur_txn{};
    {
      std::shared_lock txn_map_lk{to_txn_mgr->txn_map_mutex_};
      BUSTUB_ASSERT(to_txn_mgr->txn_map_.count(cur_undo_link.prev_txn_), "Unexpected.");
      to_cur_txn = to_txn_mgr->txn_map_[cur_undo_link.prev_txn_];
    }

    auto to_undo_log{to_cur_txn->GetUndoLog(cur_undo_link.prev_log_idx_)};
    auto next_link{to_undo_log.prev_version_};
    needed_undo_logs.emplace_back(std::move(to_undo_log));
    if (needed_undo_logs.back().ts_ <= read_ts) {
      break;
    }
    cur_undo_link = next_link;
  }

  if (!cur_undo_link.IsValid()) {
    /**
     * 无法回溯到当前 `read_ts` 之前的版本.
     * 此 Tuple 不存在.
     */
    return std::nullopt;
  }

  BUSTUB_ASSERT(needed_undo_logs.back().ts_ <= read_ts, "Unexpected.");
  return needed_undo_logs;
}

/**
 * 两个工具, 不操作 txn 本身.
 * 事务第一次修改某 Tuple, 使用 GenerateNewUndoLog, 仅对比两个 tuple 并生成 Undo Log.
 * 事务非第一次修改, 使用 GenerateUpdatedUndoLog.
 *
 * > 注意, bustub 要求 `GenerateUpdateUndoLog()` 不允许撤销 UndoLog 中的修改记录.
 * > 就算事务撤销了自己上一次的修改, 也要记录在 UndoLog. (?)
 */

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
  // UNIMPLEMENTED("not implemented");

  if (base_tuple == nullptr) {
    /**
     * Insert 操作.
     * RID 复用?
     */
    BUSTUB_ASSERT(target_tuple != nullptr, "Unexpected.");
    return UndoLog{true, {}, {}, ts, prev_version};
  }

  if (target_tuple == nullptr) {
    /**
     * Delete 操作.
     * 把 `base_tuple` 整个保存, 才能从空白恢复到 Delete 前的版本.
     */
    BUSTUB_ASSERT(base_tuple != nullptr, "Unexpected.");
    std::vector<bool> modified_flag(schema->GetColumnCount(), true);
    return UndoLog{false, std::move(modified_flag), {*base_tuple}, ts, prev_version};
  }

  std::vector<uint32_t> attrs{};
  std::vector<Value> base_values{};
  std::vector<bool> modified_flag{};
  attrs.reserve(schema->GetColumnCount());
  base_values.reserve(schema->GetColumnCount());
  modified_flag.resize(schema->GetColumnCount());

  for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); col_idx++) {
    Value base{base_tuple->GetValue(schema, col_idx)};
    Value target{target_tuple->GetValue(schema, col_idx)};
    BUSTUB_ASSERT(base.CheckComparable(target), "Unexpected.");
    /**
     * 前后都为 NULL 则应被视为不变.
     */
    if (!base.CompareExactlyEquals(target)) {
      attrs.emplace_back(col_idx);
      base_values.emplace_back(std::move(base));
      modified_flag[col_idx] = true;
    }
  }

  auto delta_schema{Schema::CopySchema(schema, attrs)};
  return UndoLog{false, std::move(modified_flag), {std::move(base_values), &delta_schema}, ts, prev_version};
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 *
 * `base_tuple` 应该已经是当前事务修改后的结果.
 * `log` 必须存在.
 *
 * 事务多次修改同一个数据, 只应该生成 1 个 Undo Log.
 *
 * 考虑 RID 初始为 deleted tuple.
 * 事务 insert 后, RID 存在 tuple, log 指向 deleted.
 * 再 delete 后, RID 为 deleted tuple, log 指向 deleted.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  // UNIMPLEMENTED("not implemented");

  if (log.is_deleted_) {
    return log;
  }

  if (base_tuple == nullptr) {
    /**
     * Insert.
     * 当前 Tuple 不存在, 且 `log.is_deleted_ == false`,
     * 则 `log` 中应该有其 delete 前的完整版本.
     */
    BUSTUB_ASSERT(target_tuple != nullptr, "Unexpected.");
    return GenerateNewUndoLog(schema, &log.tuple_, target_tuple, log.ts_, log.prev_version_);
  }

  std::vector<uint32_t> log_attrs{};
  log_attrs.reserve(schema->GetColumnCount());
  for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); col_idx++) {
    if (log.modified_fields_[col_idx]) {
      log_attrs.emplace_back(col_idx);
    }
  }
  auto log_schema{Schema::CopySchema(schema, log_attrs)};

  std::vector<Value> before_base_values{};
  before_base_values.reserve(schema->GetColumnCount());
  uint32_t log_col_idx{};
  for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); col_idx++) {
    Value before_base{base_tuple->GetValue(schema, col_idx)};
    if (log.modified_fields_[col_idx]) {
      before_base = log.tuple_.GetValue(&log_schema, log_col_idx++);
    }
    before_base_values.emplace_back(std::move(before_base));
  }

  if (target_tuple == nullptr) {
    /**
     * Delete.
     */
    std::vector<bool> modified_flag(schema->GetColumnCount(), true);
    return UndoLog{
        false, std::move(modified_flag), {std::move(before_base_values), schema}, log.ts_, log.prev_version_};
  }

  std::vector<uint32_t> attrs{};
  std::vector<Value> new_base_values{};
  std::vector<bool> modified_flag{};
  attrs.reserve(schema->GetColumnCount());
  new_base_values.reserve(schema->GetColumnCount());
  modified_flag.resize(schema->GetColumnCount());
  for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); col_idx++) {
    if (!before_base_values[col_idx].CompareExactlyEquals(target_tuple->GetValue(schema, col_idx)) ||
        log.modified_fields_[col_idx]) {  // bustub 的要求.
      attrs.emplace_back(col_idx);
      new_base_values.emplace_back(std::move(before_base_values[col_idx]));
      modified_flag[col_idx] = true;
    }
  }

  auto delta_schema{Schema::CopySchema(schema, attrs)};

  return UndoLog{
      false, std::move(modified_flag), {std::move(new_base_values), &delta_schema}, log.ts_, log.prev_version_};
}

/**
 * 读取 RID 符合 ts 的版本.
 */
auto GetTupleByTxnReadTs(TransactionManager *to_txn_mgr, Transaction *to_txn, const TableInfo *to_table_info, RID rid)
    -> std::optional<Tuple> {
  auto to_table{to_table_info->table_.get()};
  auto &table_schema{to_table_info->schema_};

  auto [meta, tuple]{to_table->GetTuple(rid)};
  std::optional<UndoLink> undo_link_opt{};
  {
    std::shared_lock txn_map_lk{to_txn_mgr->txn_map_mutex_};
    undo_link_opt = to_txn_mgr->GetUndoLink(rid);
  }
  auto undo_logs_opt{CollectUndoLogs(rid, meta, tuple, undo_link_opt, to_txn, to_txn_mgr)};
  if (!undo_logs_opt.has_value()) {
    return std::nullopt;
  }
  auto undone_tuple_opt{ReconstructTuple(&table_schema, tuple, meta, undo_logs_opt.value())};
  return undone_tuple_opt;
}

/**
 * 在 RID 已经有效的情况下, 尝试进行更新.
 * `base_meta` 可以直接 `GetTupleMeta()` 读取.
 */
auto TryUpdateTupleAndUndoLinkWithCheck(TransactionManager *to_txn_mgr, Transaction *to_txn,
                                        const TableInfo *to_table_info, RID update_rid, const TupleMeta *base_meta,
                                        const Tuple *base_tuple, const Tuple *target_tuple, bool is_insert_op) -> bool {
  auto to_table{to_table_info->table_.get()};
  auto &table_schema{to_table_info->schema_};

  TupleMeta new_meta{to_txn->GetTransactionTempTs(), target_tuple == nullptr};
  auto old_undo_link{to_txn_mgr->GetUndoLink(update_rid)};
  if ((base_meta->ts_ & TXN_START_ID) > 0) {
    if (base_meta->ts_ != to_txn->GetTransactionTempTs()) {
      to_txn->SetTainted();
      throw ExecutionException{"Trying update a tuple which is being modified by another txn."};
    }
    /**
     * 不允许 Insert 操作在一个非 deleted 的 Tuple 上执行.
     */
    if (is_insert_op && !base_meta->is_deleted_) {
      to_txn->SetTainted();
      throw ExecutionException{"Expected update on a deleted tuple."};
    }
    /**
     * 当前事务先前已经修改过此 Tuple,
     * 由于 Tuple ts 被设为事务 temp ts,
     * 当前事务可以安全独占修改.
     */
    // BUSTUB_ASSERT(*base_meta == new_meta, "Unexpected.");
    if (old_undo_link.has_value()) {
      auto gen_undo_log{GenerateUpdatedUndoLog(&table_schema, base_tuple, target_tuple,
                                               to_txn_mgr->GetUndoLog(old_undo_link.value()))};
      BUSTUB_ASSERT(old_undo_link.value().prev_txn_ == to_txn->GetTransactionId(), "Unexpected.");
      to_txn->ModifyUndoLog(old_undo_link.value().prev_log_idx_, std::move(gen_undo_log));
    }
    if (target_tuple == nullptr) {
      BUSTUB_ASSERT(new_meta.is_deleted_, "Unexpected.");
      to_table->UpdateTupleMeta(new_meta, update_rid);
    } else {
      BUSTUB_ASSERT(!new_meta.is_deleted_, "Unexpected.");
      to_table->UpdateTupleInPlace(new_meta, *target_tuple, update_rid);
    }
  } else {
    if (base_meta->ts_ > to_txn->GetReadTs()) {
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
     *
     * WARNING: 或另一种情况, 同上的其他事务启动并成功修改了此 Tuple 并 Commit,
     * Check WW-Conflict 需要能发现此 Tuple 最新版本的 ts 晚于当前事务的 read ts.
     */
    auto gen_undo_log{GenerateNewUndoLog(&table_schema, base_tuple, target_tuple, base_meta->ts_,
                                         old_undo_link.value_or(UndoLink{}))};
    auto gen_undo_link{to_txn->AppendUndoLog(std::move(gen_undo_log))};

    /**
     * WARNING: 关于 check_ww_conflict,
     * 对于 InsertExecutor 对主键 Index 的 Index Entry 指向的 RID 位置的复用操作,
     * 需要额外检查, 其 TupleMeta 最新状态是否是 `is_delete_ == true`.
     */

    /**
     * 在删除时仍将 Tuple 更新为 `base_tuple`, 绕过 bustub 的 size mismatch check.
     */

    auto check_ww_conflict_when_insert{std::function{[&](const TupleMeta &base_meta, const Tuple &base_tuple, RID rid,
                                                         std::optional<UndoLink> gen_undo_link) -> bool {
      // 要求元组必须已经被删除.
      return base_meta.ts_ <= to_txn->GetReadTs() && base_meta.is_deleted_;
    }}};

    auto check_ww_conflict_when_update{std::function{[&](const TupleMeta &base_meta, const Tuple &base_tuple, RID rid,
                                                         std::optional<UndoLink> gen_undo_link) -> bool {
      // 不要求删除.
      return base_meta.ts_ <= to_txn->GetReadTs();
    }}};

    if (!UpdateTupleAndUndoLink(
            to_txn_mgr, update_rid, gen_undo_link, to_table, to_txn, new_meta,
            target_tuple == nullptr ? *base_tuple : *target_tuple,
            std::move(is_insert_op ? check_ww_conflict_when_insert : check_ww_conflict_when_update))) {
      to_txn->SetTainted();
      throw ExecutionException{"Check WW-Conflict failed."};
    }
  }

  return true;
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  // always use stderr for printing logs...
  fmt::println(stderr, "debug_hook: {}", info);

  fmt::println(
      stderr,
      "You see this line of text because you have not implemented `TxnMgrDbg`. You should do this once you have "
      "finished task 2. Implementing this helper function will save you a lot of time for debugging in later tasks.");

  // We recommend implementing this function as traversing the table heap and print the version chain. An example output
  // of our reference solution:
  //
  // debug_hook: before verify scan
  // RID=0/0 ts=txn8 tuple=(1, <NULL>, <NULL>)
  //   txn8@0 (2, _, _) ts=1
  // RID=0/1 ts=3 tuple=(3, <NULL>, <NULL>)
  //   txn5@0 <del> ts=2
  //   txn3@0 (4, <NULL>, <NULL>) ts=1
  // RID=0/2 ts=4 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
  //   txn7@0 (5, <NULL>, <NULL>) ts=3
  // RID=0/3 ts=txn6 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
  //   txn6@0 (6, <NULL>, <NULL>) ts=2
  //   txn3@1 (7, _, _) ts=1
}

}  // namespace bustub
