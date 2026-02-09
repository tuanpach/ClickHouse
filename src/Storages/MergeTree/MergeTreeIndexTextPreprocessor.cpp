#include <Storages/MergeTree/MergeTreeIndexTextPreprocessor.h>

#include <Core/ColumnWithTypeAndName.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnString.h>
#include <Columns/IColumn_fwd.h>
#include <Columns/IColumn.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeLowCardinality.h>
#include <DataTypes/IDataType.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/Context.h>
#include <Interpreters/ExpressionActions.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTIndexDeclaration.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ExpressionListParsers.h>
#include <Storages/IndicesDescription.h>
#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/TreeRewriter.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int INCORRECT_QUERY;
}

namespace
{

constexpr char preprocessor_lambda_arg[] = "__text_index_x";
constexpr char preprocessor_needles_arg[] = "__text_index_needles";

/// Replaces subtrees in the AST whose canonical name matches `from` with an identifier named `to`.
/// Unlike RenameColumnVisitor which only handles plain identifiers, this also handles
/// function expressions (e.g., replacing the `lower(val)` subtree with a lambda variable).
void replaceColumnExpression(ASTPtr & ast, const String & from, const String & to)
{
    if (!ast)
        return;

    if ((ast->as<ASTIdentifier>() || ast->as<ASTFunction>()) && ast->getColumnName() == from)
    {
        ast = make_intrusive<ASTIdentifier>(to);
        return;
    }

    for (auto & child : ast->children)
        replaceColumnExpression(child, from, to);
}

/// Transforms a preprocessor AST like `lower(val)` into `arrayMap(x -> lower(x), val)`.
/// This is done at the AST level so that `ActionsVisitor` can build the DAG naturally.
ASTPtr wrapPreprocessorWithArrayMap(const ASTPtr & expression_ast, const String & column_name)
{
    ASTPtr body = expression_ast->clone();
    replaceColumnExpression(body, column_name, preprocessor_lambda_arg);

    auto lambda_args = makeASTFunction("tuple", make_intrusive<ASTIdentifier>(preprocessor_lambda_arg));
    auto lambda_ast = makeASTFunction("lambda", lambda_args, body);
    return makeASTFunction("arrayMap", lambda_ast, make_intrusive<ASTIdentifier>(column_name));
}

ASTPtr convertASTForHaystack(const IndexDescription & index, const ASTPtr & expression_ast)
{
    chassert(index.data_types.size() == 1);

    if (expression_ast == nullptr)
        return nullptr;

    if (isArray(index.data_types.front()))
        return wrapPreprocessorWithArrayMap(expression_ast, index.column_names.front());

    return expression_ast->clone();
}

ASTPtr convertASTForNeedles(const IndexDescription & index, const ASTPtr & expression_ast)
{
    chassert(index.data_types.size() == 1);

    if (expression_ast == nullptr)
        return nullptr;

    ASTPtr body = expression_ast->clone();
    replaceColumnExpression(body, index.column_names.front(), preprocessor_needles_arg);
    return body;
}

/// Creates and validates an ActionsDAG for a preprocessor expression.
ActionsDAG createActionsDAGForPreprocessor(const NameAndTypePair & source_column, ASTPtr expression_ast)
{
    if (expression_ast == nullptr)
        return ActionsDAG();

    auto context = Context::getGlobalContextInstance();
    auto syntax_result = TreeRewriter(context).analyze(expression_ast, {source_column});
    auto actions_dag = ExpressionAnalyzer(expression_ast, syntax_result, context).getActionsDAG(false, true);

    auto expression_name = expression_ast->getColumnName();
    actions_dag.project({{expression_name, expression_name}});
    actions_dag.removeUnusedActions();

    const ActionsDAG::NodeRawConstPtrs & outputs = actions_dag.getOutputs();
    if (outputs.size() != 1)
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must return a single column. Got {} output columns", outputs.size());

    if (outputs.front()->type != ActionsDAG::ActionType::FUNCTION)
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must be a function. Got '{}' action type", outputs.front()->type);

    if (outputs.front()->result_name == source_column.name)
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor must have at least one expression on top of the source column. Got '{}'", outputs.front()->result_name);

    if (!outputs.front()->result_type->equals(*source_column.type))
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression should return the same type as the source column. Got '{}', expected '{}'", outputs.front()->result_type->getName(), source_column.type->getName());

    if (actions_dag.hasNonDeterministic())
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must not contain non-deterministic functions");

    if (actions_dag.hasArrayJoin())
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must not contain arrayJoin");

    return actions_dag;
}

}

MergeTreeIndexTextPreprocessor::MergeTreeIndexTextPreprocessor(ASTPtr expression_ast, const IndexDescription & index_description)
    : haystack_column(index_description.column_names.front(), index_description.data_types.front())
    , haystack_actions(createActionsDAGForPreprocessor(haystack_column, convertASTForHaystack(index_description, expression_ast)))
    , needles_column(preprocessor_needles_arg, std::make_shared<DataTypeString>())
    , needles_actions(createActionsDAGForPreprocessor(needles_column, convertASTForNeedles(index_description, expression_ast)))
{
}

std::pair<ColumnPtr, size_t> MergeTreeIndexTextPreprocessor::processColumn(const ColumnWithTypeAndName & column, size_t start_row, size_t n_rows) const
{
    ColumnPtr index_column = column.column;
    if (haystack_actions.getActions().empty())
        return {index_column, start_row};

    chassert(column.name == haystack_column.name);
    chassert(index_column->getDataType() == column.type->getTypeId());

    /// Only copy if needed
    if (start_row != 0 || n_rows != index_column->size())
        index_column = index_column->cut(start_row, n_rows);

    Block block({ColumnWithTypeAndName(index_column, column.type, haystack_column.name)});
    haystack_actions.execute(block, n_rows);
    return {block.safeGetByPosition(0).column, 0};
}

String MergeTreeIndexTextPreprocessor::processConstant(const String & input) const
{
    if (needles_actions.getActions().empty())
        return input;

    Block block{{ColumnWithTypeAndName
    {
        needles_column.type->createColumnConst(1, Field(input)),
        needles_column.type,
        needles_column.name,
    }}};

    size_t nrows = 1;
    needles_actions.execute(block, nrows);
    return String{block.safeGetByPosition(0).column->getDataAt(0)};
}

}
