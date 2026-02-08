#pragma once

#include <Core/NamesAndTypes.h>
#include <Interpreters/Context_fwd.h>
#include <Interpreters/ExpressionActions.h>
#include <Parsers/IAST_fwd.h>

namespace DB
{

struct ColumnWithTypeAndName;
struct IndexDescription;

class MergeTreeIndexTextPreprocessor
{
public:
    MergeTreeIndexTextPreprocessor(ASTPtr expression_ast, const IndexDescription & index_description);

    /// Processes n_rows rows of input column, starting at start_row.
    /// The transformation is only applied in the range [start_row, start_row + n_rows)
    /// If the expression is empty this functions is just a no-op.
    /// Returns a pair with the result column and the starting position where results were written.
    /// If the expression is empty this just returns the input column and start_row.
    std::pair<ColumnPtr, size_t> processColumn(const ColumnWithTypeAndName & column, size_t start_row, size_t n_rows) const;

    /// Applies the internal expression to an input string.
    /// Kind of equivalent to 'SELECT expression(const_string)'.
    String processConstant(const String & input) const;
    bool hasActions() const { return !expression_actions.getActions().empty(); }

    ASTPtr getAST() const { return ast; }
    const ActionsDAG & getActionsDAG() const { return expression_actions.getActionsDAG(); }
    const ActionsDAG & getActionsDAGForArray() const { return array_expression_actions.getActionsDAG(); }
    Names getRequiredColumns() const { return expression_actions.getRequiredColumns(); }

private:
    ASTPtr ast;
    NamesAndTypesList index_columns;
    ExpressionActions expression_actions;
    ExpressionActions array_expression_actions;
};

}
