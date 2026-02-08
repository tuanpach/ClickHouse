#include <Storages/MergeTree/MergeTreeIndexTextPreprocessor.h>

#include <Core/ColumnWithTypeAndName.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnString.h>
#include <Columns/IColumn_fwd.h>
#include <Columns/IColumn.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeLowCardinality.h>
#include <DataTypes/IDataType.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionsMiscellaneous.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/Context.h>
#include <Interpreters/Context_fwd.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/ExpressionActionsSettings.h>
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
    extern const int LOGICAL_ERROR;
    extern const int INCORRECT_QUERY;
    extern const int BAD_ARGUMENTS;
}


namespace
{

DataTypePtr getInnerType(DataTypePtr type)
{
    DataTypePtr inner_type = type;

    if (isArray(type))
    {
        const DataTypeArray * array_type = typeid_cast<const DataTypeArray*>(type.get());
        inner_type = array_type->getNestedType();
    }
    else if (type->lowCardinality())
    {
        const auto * low_cardinality_type = typeid_cast<const DataTypeLowCardinality *>(type.get());
        inner_type = low_cardinality_type->getDictionaryType();
    }

    if (isStringOrFixedString(inner_type))
        return inner_type;

    throw Exception(ErrorCodes::BAD_ARGUMENTS, "Text index column type {} is not supported", type->getName());
}

ActionsDAG createActionsDAGForArray(const IndexDescription & index, const ActionsDAG & actions_dag)
{
    if (actions_dag.getOutputs().empty() || !isArray(actions_dag.getOutputs().front()->result_type))
        return ActionsDAG();

    DataTypePtr index_data_type = getInnerType(index.data_types.front());
    NamesAndTypesList index_columns({{index.column_names.front(), index_data_type}});
    auto index_array_type = std::make_shared<DataTypeArray>(index_data_type);

    ActionsDAG dag;
    const auto * array_input = &dag.addInput(index.column_names.front(), index_array_type);

    /// Clone the preprocessor DAG to use as the lambda body.
    ActionsDAG lambda_dag = actions_dag.clone();
    auto result_type = lambda_dag.getOutputs().front()->result_type;
    auto result_name = lambda_dag.getOutputs().front()->result_name;
    ContextPtr context = Context::getGlobalContextInstance();

    auto function_capture = std::make_shared<FunctionCaptureOverloadResolver>(
        std::move(lambda_dag),
        ExpressionActionsSettings(context),
        /*captured_names=*/ Names{},
        index_columns,
        result_type,
        result_name,
        /*allow_constant_folding=*/ false);

    const auto * lambda_node = &dag.addFunction(function_capture, {}, "");
    auto array_map_func = FunctionFactory::instance().get("arrayMap", context);
    const auto * array_map_node = &dag.addFunction(array_map_func, {lambda_node, array_input}, "");
    dag.getOutputs() = {array_map_node};
    dag.removeUnusedActions();
    return dag;
}

/// This function parses an string to build an ExpressionActions.
/// The conversion is not direct and requires many steps and validations, but long story short
/// ParserExpression(String) => AST; ActionsVisitor(AST) => ActionsDAG; ExpressionActions(ActionsDAG)
ActionsDAG createActionsDAGForString(const IndexDescription & index, ASTPtr expression_ast)
{
    chassert(index.column_names.size() == 1);
    chassert(index.data_types.size() == 1);

    if (expression_ast == nullptr)
        return ActionsDAG();

    /// Repeat expression validation here. after the string has been parsed into an AST.
    /// We already made this check during index construction, but "don't trust, verify"
    const ASTFunction * preprocessor_function = expression_ast->as<ASTFunction>();
    if (preprocessor_function == nullptr)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Preprocessor argument must be an expression");

    /// Convert ASTPtr -> ActionsDAG
    /// We can do less checks here because in previous scope we tested that the ASTPtr is an ASTFunction.
    const String name = expression_ast->getColumnName();
    const String alias = expression_ast->getAliasOrColumnName();

    DataTypePtr column_data_type = getInnerType(index.data_types.front());
    NamesAndTypesList index_columns({{index.column_names.front(), column_data_type}});

    auto context = Context::getGlobalContextInstance();
    auto syntax_result = TreeRewriter(context).analyze(expression_ast, index_columns);
    auto actions_dag = ExpressionAnalyzer(expression_ast, syntax_result, context).getActionsDAG(false, true);
    actions_dag.project(NamesWithAliases({{name, alias}}));

    /// With the dag we can create an ExpressionActions. But before that is better to perform some validations.

    /// Lets check expression outputs
    const ActionsDAG::NodeRawConstPtrs & outputs = actions_dag.getOutputs();
    if (outputs.size() != 1)
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must return only a single value");

    if (outputs.front()->type != ActionsDAG::ActionType::FUNCTION)
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must return a function");

    if (!isStringOrFixedString(outputs.front()->result_type))
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression should return a String or a FixedString.");

    if (actions_dag.hasNonDeterministic())
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must not contain non-deterministic functions");

    if (actions_dag.hasArrayJoin())
        throw Exception(ErrorCodes::INCORRECT_QUERY, "The preprocessor expression must not contain arrayJoin");

    return actions_dag;
}

}

MergeTreeIndexTextPreprocessor::MergeTreeIndexTextPreprocessor(ASTPtr expression_ast, const IndexDescription & index_description)
    : ast(expression_ast)
    , index_columns({{index_description.column_names.front(), getInnerType(index_description.data_types.front())}})
    , expression_actions(createActionsDAGForString(index_description, expression_ast))
    , array_expression_actions(createActionsDAGForArray(index_description, expression_actions.getActionsDAG()))
{
}

std::pair<ColumnPtr, size_t> MergeTreeIndexTextPreprocessor::processColumn(const ColumnWithTypeAndName & column, size_t start_row, size_t n_rows) const
{
    ColumnPtr index_column = column.column;
    if (expression_actions.getActions().empty())
        return {index_column, start_row};

    const auto & [index_column_name, index_column_type] = index_columns.front();
    chassert(column.name == index_column_name);
    chassert(index_column->getDataType() == column.type->getTypeId());

    /// Only copy if needed
    if (start_row != 0 || n_rows != index_column->size())
        index_column = index_column->cut(start_row, n_rows);

    Block block({ColumnWithTypeAndName(index_column, column.type, index_column_name)});
    const auto & actions_to_execute = isArray(column.type) ? array_expression_actions : expression_actions;
    actions_to_execute.execute(block, n_rows);
    return {block.safeGetByPosition(0).column, 0};
}

String MergeTreeIndexTextPreprocessor::processConstant(const String & input) const
{
    if (empty())
        return input;

    const auto & [index_column_name, index_column_type] = index_columns.front();
    Field input_field(input);
    ColumnWithTypeAndName input_entry(index_column_type->createColumnConst(1, input_field), index_column_type, index_column_name);

    Block input_block;
    input_block.insert(input_entry);

    size_t nrows = 1;
    expression_actions.execute(input_block, nrows);
    return String{input_block.safeGetByPosition(0).column->getDataAt(0)};
}

}
