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
  return Tuple{std::move(values), to_schema};
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
   * 事务应该能读取自己的未提交写结果.
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
    needed_undo_logs.emplace_back(std::move(to_undo_log));
    if (needed_undo_logs.back().ts_ <= read_ts) {
      break;
    }
    cur_undo_link = to_undo_log.prev_version_;
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
  UNIMPLEMENTED("not implemented");
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
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  UNIMPLEMENTED("not implemented");
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
