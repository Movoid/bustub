//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seqscan_as_indexscan.cpp
//
// Identification: src/optimizer/seqscan_as_indexscan.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <stack>
#include "common/util/hash_util.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief Optimizes seq scan as index scan if there's an index on a table
 *
 * 将带有谓词的 SeqScan 转换为 IndexScan.
 *
 * 需要处理 `where colB = 1 OR colB = 1` 谓词中的重复常量.
 * 需要对谓词 Expression 进行解析并提取 ConstantValueExpression.
 */
auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(P3): implement seq scan with predicate -> index scan optimizer rule
  // The Filter Predicate Pushdown has been enabled for you in optimizer.cpp when forcing starter rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  /**
   * 不是 SeqScan?
   */
  if (optimized_plan->GetType() != PlanType::SeqScan) {
    return optimized_plan;
  }

  /**
   * 没有谓词?
   */
  const auto &seq_scan_plan{dynamic_cast<const SeqScanPlanNode &>(*optimized_plan)};
  if (seq_scan_plan.filter_predicate_ == nullptr) {
    return optimized_plan;
  }

  /**
   * 找出存在 Index 的 Column.
   */
  std::unordered_map<uint32_t, index_oid_t> index_oid_of_col{};
  auto all_index{catalog_.GetTableIndexes(seq_scan_plan.table_name_)};
  for (auto &to_index_info : all_index) {
    BUSTUB_ASSERT(to_index_info->index_->GetMetadata()->GetKeyAttrs().size() == 1, "Unsupported index key format.");
    index_oid_of_col.emplace(to_index_info->index_->GetMetadata()->GetKeyAttrs()[0], to_index_info->index_oid_);
  }

  /**
   * 对 `where colB = 1 OR colB = 2` 模式的谓词进行优化.
   */
  bool is_failed{};
  uint32_t use_col_idx{UINT32_MAX};

  /**
   * IndexScanExecutor 单点查询不要求有序.
   * 用 `std::unordered_set` 足够.
   */
  struct ConstantValueExpressionHash {
    auto operator()(const AbstractExpressionRef &expr) const -> std::size_t {
      BUSTUB_ASSERT(dynamic_cast<ConstantValueExpression *>(expr.get()) != nullptr, "Invalid args.");
      auto to_const_expr{static_cast<ConstantValueExpression *>(expr.get())};
      /**
       * 需要对 NULL 得到稳定的 hash.
       * bustub HashUtil 逻辑不完善,
       * 由于 bustub 在 Value `size_.len_` 上标记 NULL, 其 `value_` 未定义,
       * HashUtil 对 `value_` 取 Hash, 会让 NULL hash 值不同.
       */
      if (to_const_expr->val_.IsNull()) {
        return UINT64_MAX;
      }
      return HashUtil::HashValue(&to_const_expr->val_);
    }
  };
  struct ConstantValueExpressionEqualTo {
    auto operator()(const AbstractExpressionRef &a, const AbstractExpressionRef &b) const -> bool {
      BUSTUB_ASSERT(dynamic_cast<ConstantValueExpression *>(a.get()) != nullptr, "Invalid args.");
      BUSTUB_ASSERT(dynamic_cast<ConstantValueExpression *>(b.get()) != nullptr, "Invalid args.");
      auto to_const_expr_a{static_cast<ConstantValueExpression *>(a.get())};
      auto to_const_expr_b{static_cast<ConstantValueExpression *>(b.get())};
      /**
       * 对 NULL 需要去重.
       */
      return (to_const_expr_a->val_.CompareEquals(to_const_expr_b->val_) == CmpBool::CmpTrue) ||
             (to_const_expr_a->val_.IsNull() && to_const_expr_b->val_.IsNull());
    }
  };
  std::unordered_set<AbstractExpressionRef, ConstantValueExpressionHash, ConstantValueExpressionEqualTo>
      const_key_set{};

  struct ArgPack {
    AbstractExpressionRef expr_;
    bool backward_{};

    ArgPack(const AbstractExpressionRef &expr, bool backward) : expr_{expr}, backward_{backward} {};
    ArgPack(AbstractExpressionRef &&expr, bool backward) : expr_{std::move(expr)}, backward_{backward} {};
  };
  std::stack<ArgPack, std::vector<ArgPack>> callstack{};
  callstack.emplace(seq_scan_plan.filter_predicate_, false);

  while (!callstack.empty()) {
    auto &[cur_expr, cur_backward_]{callstack.top()};
    if (cur_backward_) {
      callstack.pop();
      continue;
    }
    if (auto to_cur_logic_expr{dynamic_cast<LogicExpression *>(cur_expr.get())}; to_cur_logic_expr != nullptr) {
      cur_backward_ = true;
      if (to_cur_logic_expr->logic_type_ != LogicType::Or) {
        is_failed = true;
        break;
      }
      BUSTUB_ASSERT(to_cur_logic_expr->GetChildren().size() == 2, "Unexpected.");
      callstack.emplace(to_cur_logic_expr->GetChildAt(0), false);
      callstack.emplace(to_cur_logic_expr->GetChildAt(1), false);
    } else if (auto to_cur_cmp_expr{dynamic_cast<ComparisonExpression *>(cur_expr.get())}; to_cur_cmp_expr != nullptr) {
      cur_backward_ = true;
      /**
       * 如果是 ColumnValueExpression 和 ConstantValueExpression,
       * 若对应 Column 存在索引则允许优化.
       */
      if (to_cur_cmp_expr->comp_type_ != ComparisonType::Equal) {
        is_failed = true;
        break;
      }
      BUSTUB_ASSERT(to_cur_cmp_expr->GetChildren().size() == 2, "Unexpected.");
      uint32_t const_expr_child_idx{UINT32_MAX};
      auto to_col_expr{dynamic_cast<ColumnValueExpression *>(to_cur_cmp_expr->GetChildAt(0).get())};
      if (to_col_expr == nullptr) {
        to_col_expr = dynamic_cast<ColumnValueExpression *>(to_cur_cmp_expr->GetChildAt(1).get());
      }
      auto to_const_expr{dynamic_cast<ConstantValueExpression *>(to_cur_cmp_expr->GetChildAt(0).get())};
      const_expr_child_idx = 0;
      if (to_const_expr == nullptr) {
        to_const_expr = dynamic_cast<ConstantValueExpression *>(to_cur_cmp_expr->GetChildAt(1).get());
        const_expr_child_idx = 1;
      }
      if (to_col_expr == nullptr || to_const_expr == nullptr) {
        is_failed = true;
        break;
      }

      if (to_col_expr->GetTupleIdx() != 0) {
        is_failed = true;
        break;
      }

      /**
       * Column 是否存在可用的索引?
       */
      if (index_oid_of_col.count(to_col_expr->GetColIdx()) == 0) {
        is_failed = true;
        break;
      }
      /**
       * 是否是谓词中唯一出现过的 Column?
       */
      if (use_col_idx != UINT32_MAX && to_col_expr->GetColIdx() != use_col_idx) {
        is_failed = true;
        break;
      }
      use_col_idx = to_col_expr->GetColIdx();
      const_key_set.emplace(to_cur_cmp_expr->GetChildAt(const_expr_child_idx));
    } else {
      is_failed = true;
      break;
    }
  }

  if (is_failed) {
    return optimized_plan;
  }

  /**
   * 允许优化.
   */
  BUSTUB_ASSERT(!const_key_set.empty(), "Unexpected.");
  BUSTUB_ASSERT(use_col_idx != UINT32_MAX, "Unexpected.");
  std::vector<AbstractExpressionRef> const_keys{};
  const_keys.resize(const_key_set.size());
  std::move(const_key_set.begin(), const_key_set.end(), const_keys.begin());
  auto index_scan_plan{std::make_shared<IndexScanPlanNode>(seq_scan_plan.output_schema_, seq_scan_plan.GetTableOid(),
                                                           index_oid_of_col[use_col_idx], nullptr,
                                                           std::move(const_keys))};
  return index_scan_plan;
}

}  // namespace bustub
