//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// projection_plan.h
//
// Identification: src/include/execution/plans/projection_plan.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "catalog/catalog.h"
#include "catalog/schema.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/plans/abstract_plan.h"

namespace bustub {

/**
 * The ProjectionPlanNode represents a project operation.
 * It computes expressions based on the input.
 */
class ProjectionPlanNode : public AbstractPlanNode {
 public:
  /**
   * Construct a new ProjectionPlanNode instance.
   * @param output The output schema of this projection node
   * @param expressions The expression to evaluate
   * @param child The child plan node
   */

  /**
   * Projection 只有一个子节点.
   */
  ProjectionPlanNode(SchemaRef output, std::vector<AbstractExpressionRef> expressions, AbstractPlanNodeRef child)
      : AbstractPlanNode(std::move(output), {std::move(child)}), expressions_(std::move(expressions)) {}

  /** @return The type of the plan node */
  auto GetType() const -> PlanType override { return PlanType::Projection; }

  /** @return The child plan node */
  auto GetChildPlan() const -> AbstractPlanNodeRef {
    BUSTUB_ASSERT(GetChildren().size() == 1, "Projection should have exactly one child plan.");
    return GetChildAt(0);
  }

  /** @return Projection expressions */
  auto GetExpressions() const -> const std::vector<AbstractExpressionRef> & { return expressions_; }

  static auto InferProjectionSchema(const std::vector<AbstractExpressionRef> &expressions) -> Schema;

  static auto RenameSchema(const Schema &schema, const std::vector<std::string> &col_names) -> Schema;

  /**
   * 生成一个 `CloneWithChildren` 接口,
   * 拷贝一个当前 ProjectionPlanNode 的副本, 并设置其新的 `children_`.
   */
  BUSTUB_PLAN_NODE_CLONE_WITH_CHILDREN(ProjectionPlanNode);

  /**
   * 描述 Projection 每次输出的一个 Tuple 的所有 Column 上的 Value,
   * 如何从输入的元组求值得到.
   * 每个 Expression 使用输入 Tuple 求值得到的 Value 就是输出元组的一个 Column 取值.
   *
   * 这里的 Expression 通过解析用户 SQL 得到.
   */
  std::vector<AbstractExpressionRef> expressions_;

 protected:
  auto PlanNodeToString() const -> std::string override;
};

}  // namespace bustub
