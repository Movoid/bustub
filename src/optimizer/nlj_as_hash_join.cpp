//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nlj_as_hash_join.cpp
//
// Identification: src/optimizer/nlj_as_hash_join.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <stack>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

/**
 * @brief optimize nested loop join into hash join.
 * In the starter code, we will check NLJs with exactly one equal condition. You can further support optimizing joins
 * with multiple eq conditions.
 */
auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement NestedLoopJoin -> HashJoin optimizer rule
  // Note for Spring 2025: You should support join keys of any number of conjunction of equi-conditions:
  // E.g. <column expr> = <column expr> AND <column expr> = <column expr> AND ...

  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::NestedLoopJoin) {
    return optimized_plan;
  }

  const auto &nlj_plan{dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan)};
  if (nlj_plan.Predicate() == nullptr) {
    return optimized_plan;
  }

  /**
   * 提取 ColumnValueExpression .
   * 注意 Expr 属于左表还是右表.
   *
   * 可以对 `t1.colA = t2.colB AND t1.colA = t2.colB` 进行去重.
   */

  bool is_failed{};

  /**
   * 需要改写成 AbexprRef.
   * 不得不改写, 因为构造 ColumnValueExpression 需要 Column object.
   */
  struct ColValExprRefPair {
    AbstractExpressionRef to_left_col_expr_;
    AbstractExpressionRef to_right_col_expr_;

    ColValExprRefPair(const AbstractExpressionRef &l, const AbstractExpressionRef &r)
        : to_left_col_expr_{l}, to_right_col_expr_{r} {}
    ColValExprRefPair(AbstractExpressionRef &&l, AbstractExpressionRef &&r)
        : to_left_col_expr_{std::move(l)}, to_right_col_expr_{std::move(r)} {}
  };

  struct ColValExprRefHash {
    auto operator()(const ColValExprRefPair &obj) const -> std::size_t {
      BUSTUB_ASSERT(dynamic_cast<ColumnValueExpression *>(obj.to_left_col_expr_.get()), "Unexpected.");
      BUSTUB_ASSERT(dynamic_cast<ColumnValueExpression *>(obj.to_right_col_expr_.get()), "Unexpected.");
      auto to_left_col_expr{static_cast<ColumnValueExpression *>(obj.to_left_col_expr_.get())};
      auto to_right_col_expr{static_cast<ColumnValueExpression *>(obj.to_right_col_expr_.get())};
      return std::hash<uint64_t>{}((static_cast<uint64_t>(to_left_col_expr->GetColIdx()) << 32) |
                                   to_right_col_expr->GetColIdx());
    }
  };

  struct ColValExprRefEqualTo {
    auto operator()(const ColValExprRefPair &a, const ColValExprRefPair &b) const -> bool {
      BUSTUB_ASSERT(dynamic_cast<ColumnValueExpression *>(a.to_left_col_expr_.get()), "Unexpected.");
      BUSTUB_ASSERT(dynamic_cast<ColumnValueExpression *>(a.to_right_col_expr_.get()), "Unexpected.");
      BUSTUB_ASSERT(dynamic_cast<ColumnValueExpression *>(b.to_left_col_expr_.get()), "Unexpected.");
      BUSTUB_ASSERT(dynamic_cast<ColumnValueExpression *>(b.to_right_col_expr_.get()), "Unexpected.");
      return static_cast<ColumnValueExpression *>(a.to_left_col_expr_.get())->GetColIdx() ==
                 static_cast<ColumnValueExpression *>(b.to_left_col_expr_.get())->GetColIdx() &&
             static_cast<ColumnValueExpression *>(a.to_right_col_expr_.get())->GetColIdx() ==
                 static_cast<ColumnValueExpression *>(b.to_right_col_expr_.get())->GetColIdx();
    }
  };

  std::unordered_set<ColValExprRefPair, ColValExprRefHash, ColValExprRefEqualTo> equ_pair{};

  struct ArgPack {
    AbstractExpressionRef expr_;
    bool backward_{};

    ArgPack(const AbstractExpressionRef &expr, bool backward) : expr_{expr}, backward_{backward} {};
    ArgPack(AbstractExpressionRef &&expr, bool backward) : expr_{std::move(expr)}, backward_{backward} {};
  };

  std::stack<ArgPack, std::vector<ArgPack>> callstack{};
  callstack.emplace(nlj_plan.Predicate(), false);

  while (!callstack.empty()) {
    auto &[cur_expr, cur_backward]{callstack.top()};
    if (cur_backward) {
      callstack.pop();
      continue;
    }

    if (auto to_cur_logic_expr{dynamic_cast<LogicExpression *>(cur_expr.get())}; to_cur_logic_expr != nullptr) {
      cur_backward = true;
      if (to_cur_logic_expr->logic_type_ != LogicType::And) {
        is_failed = true;
        break;
      }
      BUSTUB_ASSERT(!to_cur_logic_expr->GetChildren().empty(), "Unexpected.");
      callstack.emplace(to_cur_logic_expr->GetChildAt(0), false);
      callstack.emplace(to_cur_logic_expr->GetChildAt(1), false);
    } else if (auto to_cur_cmp_expr{dynamic_cast<ComparisonExpression *>(cur_expr.get())}; to_cur_cmp_expr != nullptr) {
      cur_backward = true;
      if (to_cur_cmp_expr->comp_type_ != ComparisonType::Equal) {
        is_failed = true;
        break;
      }
      BUSTUB_ASSERT(to_cur_cmp_expr->GetChildren().size() == 2, "Unexpected.");
      auto to_l_expr{to_cur_cmp_expr->GetChildAt(0)};
      auto to_r_expr{to_cur_cmp_expr->GetChildAt(1)};
      auto to_l_col_expr{dynamic_cast<ColumnValueExpression *>(to_l_expr.get())};
      auto to_r_col_expr{dynamic_cast<ColumnValueExpression *>(to_r_expr.get())};

      if (to_l_col_expr == nullptr || to_r_col_expr == nullptr) {
        is_failed = true;
        break;
      }
      if (to_l_col_expr->GetTupleIdx() != 0) {
        std::swap(to_l_expr, to_r_expr);
        std::swap(to_l_col_expr, to_r_col_expr);
      }
      BUSTUB_ASSERT(to_l_col_expr->GetTupleIdx() == 0, "Unexpected.");
      BUSTUB_ASSERT(to_r_col_expr->GetTupleIdx() == 1, "Unexpected.");
      equ_pair.emplace(std::move(to_l_expr), std::move(to_r_expr));
    } else {
      is_failed = true;
      break;
    }
  }

  if (is_failed) {
    return optimized_plan;
  }

  BUSTUB_ASSERT(!equ_pair.empty(), "Unexpected.");

  std::vector<AbstractExpressionRef> left_key_expr{};
  std::vector<AbstractExpressionRef> right_key_expr{};
  left_key_expr.reserve(equ_pair.size());
  right_key_expr.reserve(equ_pair.size());
  auto equ_pair_iter{equ_pair.begin()};
  while (equ_pair_iter != equ_pair.end()) {
    auto next_iter{std::next(equ_pair_iter)};
    auto node{equ_pair.extract(equ_pair_iter)};
    left_key_expr.emplace_back(std::move(node.value().to_left_col_expr_));
    right_key_expr.emplace_back(std::move(node.value().to_right_col_expr_));
    equ_pair_iter = next_iter;
  }

  BUSTUB_ASSERT(equ_pair.empty(), "Unexpected.");

  auto hash_join_plan{std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(),
                                                         nlj_plan.GetRightPlan(), std::move(left_key_expr),
                                                         std::move(right_key_expr), nlj_plan.GetJoinType())};
  return hash_join_plan;
}

}  // namespace bustub
