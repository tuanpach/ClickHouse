#pragma once

#include <Core/NamesAndTypes.h>
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
    bool hasActions() const { return !haystack_actions.getActions().empty(); }

    const ActionsDAG & getHaystackActionsDAG() const { return haystack_actions.getActionsDAG(); }
    const ActionsDAG & getNeedlesActionsDAG() const { return needles_actions.getActionsDAG(); }

private:
    /// Preprocessor is applied to the source column (haystack) and to constant string (needles).
    /// Column name and type, and expression actions for haystack and needles may differ.
    NameAndTypePair haystack_column;
    ExpressionActions haystack_actions;

    NameAndTypePair needles_column;
    ExpressionActions needles_actions;
};

}
