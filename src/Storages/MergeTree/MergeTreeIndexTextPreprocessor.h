#pragma once

#include <Interpreters/ExpressionActions.h>
#include <Parsers/IAST_fwd.h>

namespace DB
{

struct ColumnWithTypeAndName;
struct IndexDescription;

class MergeTreeIndexTextPreprocessor
{
public:
    /// This function parses an expression string to create MergeTreeIndexTextPreprocessorExpression.
    /// The conversion is not direct and requires many steps and validations, but long story short
    /// ParserExpression(String) => AST; ActionsVisitor(AST) => ActionsDAG; ExpressionActions(ActionsDAG)
    static std::shared_ptr<MergeTreeIndexTextPreprocessor> create(const IndexDescription & index_description, const String & expression);

    /// Processes n_rows rows of input column, starting at start_row.
    /// The transformation is only applied in the range [start_row, start_row + n_rows)
    /// If the expression is empty this functions is just a no-op.
    /// Returns a pair with the result column and the starting position where results were written.
    /// If the expression is empty this just returns the input column and start_row.
    std::pair<ColumnPtr, size_t> processColumn(const ColumnWithTypeAndName & column, size_t start_row, size_t n_rows) const;

    /// Applies the internal expression to an input string.
    /// Kind of equivalent to 'SELECT expression(input)'.
    String process(const String & input) const;

    const ASTPtr & getAST() const { return ast; }
    const NamesAndTypesList & getSourceColumns() const { return source_columns; }
    const ExpressionActions & getExpressionActions() const { return expression_actions; }

private:
    MergeTreeIndexTextPreprocessor(ASTPtr ast_, NamesAndTypesList source_columns_, ExpressionActions expression_actions_)
        : ast(std::move(ast_))
        , source_columns(std::move(source_columns_))
        , expression_actions(std::move(expression_actions_))
    {
    }

    ASTPtr ast;
    NamesAndTypesList source_columns;
    ExpressionActions expression_actions;
};

}
