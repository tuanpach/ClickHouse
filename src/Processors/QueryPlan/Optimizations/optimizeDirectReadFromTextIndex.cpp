#include <Functions/IFunction.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/Context.h>
#include <Interpreters/ITokenExtractor.h>
#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Processors/QueryPlan/FilterStep.h>
#include <Processors/QueryPlan/Optimizations/Optimizations.h>
#include <Processors/QueryPlan/QueryPlan.h>
#include <Processors/QueryPlan/ReadFromMergeTree.h>
#include <Storages/MergeTree/MergeTreeIndexConditionText.h>
#include <Common/quoteString.h>
#include <Common/FieldVisitorToString.h>
#include <Common/logger_useful.h>
#include <Functions/FunctionFactory.h>
#include <Storages/MergeTree/MergeTreeIndexTextPreprocessor.h>
#include <Storages/MergeTree/RangesInDataPart.h>
#include <base/defines.h>

namespace DB::QueryPlanOptimizations
{

namespace
{

using NodesReplacementMap = absl::flat_hash_map<const ActionsDAG::Node *, const ActionsDAG::Node *>;

struct TextIndexReadInfo
{
    const MergeTreeIndexWithCondition * index;
    bool is_materialized;
    bool is_fully_materialied;
};

using TextIndexReadInfos = absl::flat_hash_map<String, TextIndexReadInfo>;

String getNameWithoutAliases(const ActionsDAG::Node * node)
{
    while (node->type == ActionsDAG::ActionType::ALIAS)
    {
        node = node->children[0];
    }

    if (node->type == ActionsDAG::ActionType::FUNCTION)
    {
        String result_name = node->function_base->getName() + "(";
        for (size_t i = 0; i < node->children.size(); ++i)
        {
            if (i)
                result_name += ", ";

            result_name += getNameWithoutAliases(node->children[i]);
        }

        result_name += ")";
        return result_name;
    }

    return node->result_name;
}

/// Check if a node with the given canonical name exists as a subexpression within the DAG rooted at `node`.
bool hasSubexpression(const ActionsDAG::Node * node, const String & subexpression_name)
{
    if (getNameWithoutAliases(node) == subexpression_name)
        return true;

    for (const auto * child : node->children)
    {
        if (hasSubexpression(child, subexpression_name))
            return true;
    }

    return false;
}

const ActionsDAG::Node * replaceNodes(ActionsDAG & dag, const ActionsDAG::Node * node, const NodesReplacementMap & replacements)
{
    if (auto it = replacements.find(node); it != replacements.end())
    {
        return it->second;
    }
    else if (node->type == ActionsDAG::ActionType::ALIAS)
    {
        const auto * old_child = node->children[0];
        const auto * new_child = replaceNodes(dag, old_child, replacements);

        if (old_child != new_child)
            return &dag.addAlias(*new_child, node->result_name);
    }
    else if (node->type == ActionsDAG::ActionType::FUNCTION)
    {
        auto old_children = node->children;
        std::vector<const ActionsDAG::Node *> new_children;

        for (const auto & child : old_children)
            new_children.push_back(replaceNodes(dag, child, replacements));

        if (new_children != old_children)
            return &dag.addFunction(node->function_base, new_children, "");
    }

    return node;
}

String optimizationInfoToString(const IndexReadColumns & added_columns, const Names & removed_columns)
{
    chassert(!added_columns.empty());

    String result = "Added: [";

    /// This will list the index and the new associated columns
    size_t idx = 0;
    for (const auto & [_, added_virtual_columns] : added_columns)
    {
        for (const auto & added_virtual_column : added_virtual_columns)
        {
            if (++idx > 1)
                result += ", ";
            result += added_virtual_column.name;
        }
    }
    result += "]";

    if (!removed_columns.empty())
    {
        result += ", Removed: [";
        for (size_t i = 0; i < removed_columns.size(); ++i)
        {
            if (i > 0)
                result += ", ";
            result += removed_columns[i];
        }
        result += "]";
    }
    return result;
}

/// Helper function.
/// Collects index conditions from the given ReadFromMergeTree step and stores them in text_index_read_infos.
void collectTextIndexReadInfos(const ReadFromMergeTree * read_from_merge_tree_step, TextIndexReadInfos & text_index_read_infos)
{
    const auto & indexes = read_from_merge_tree_step->getIndexes();
    if (!indexes || indexes->skip_indexes.useful_indices.empty())
        return;

    const RangesInDataParts & parts_with_ranges = read_from_merge_tree_step->getParts();
    if (parts_with_ranges.empty())
        return;

    std::unordered_set<DataPartPtr> unique_parts;
    for (const auto & part : parts_with_ranges)
        unique_parts.insert(part.data_part);

    for (const auto & index : indexes->skip_indexes.useful_indices)
    {
        if (!typeid_cast<MergeTreeIndexConditionText *>(index.condition.get()))
            continue;

        /// Index may be not materialized in some parts, e.g. after ALTER ADD INDEX query.
        size_t num_materialized_parts = std::ranges::count_if(unique_parts, [&](const auto & part)
        {
            return !!index.index->getDeserializedFormat(part->checksums, index.index->getFileName());
        });

        text_index_read_infos[index.index->index.name] =
        {
            .index = &index,
            .is_materialized = num_materialized_parts > 0,
            .is_fully_materialied = num_materialized_parts == unique_parts.size()
        };
    }
}

/// Convert ActionsDAG::Node to AST for use as default expression
ASTPtr convertNodeToAST(const ActionsDAG::Node & node)
{
    switch (node.type)
    {
        case ActionsDAG::ActionType::INPUT:
            return make_intrusive<ASTIdentifier>(node.result_name);

        case ActionsDAG::ActionType::COLUMN:
            if (node.column)
                return make_intrusive<ASTLiteral>((*node.column)[0]);
            return make_intrusive<ASTLiteral>(Field{});

        case ActionsDAG::ActionType::ALIAS:
            if (!node.children.empty())
                return convertNodeToAST(*node.children[0]);
            return nullptr;

        case ActionsDAG::ActionType::FUNCTION: {
            if (!node.function_base)
                return nullptr;

            auto function = make_intrusive<ASTFunction>();
            function->name = node.function_base->getName();
            function->arguments = make_intrusive<ASTExpressionList>();
            function->children.push_back(function->arguments);

            for (const auto * child : node.children)
            {
                if (auto arg_ast = convertNodeToAST(*child))
                    function->arguments->children.push_back(arg_ast);
            }

            return function;
        }

        default:
            return nullptr;
    }
}

}

/// This class substitutes filters with text-search functions by virtual columns which skip IO and read less data.
///
/// The substitution is performed after the index analysis and before PREWHERE optimization:
/// 1, We need the result of index analysis.
/// 2. We want to leverage the PREWHERE for virtual columns, because text index
///    is usually created with high granularity and PREWHERE with virtual columns
///    may significantly reduce the amount of data to read.
///
/// For example, for a query like:
///     SELECT count() FROM table WHERE hasToken(text_col, 'token')
/// if 1) text_col has an associated text index called text_col_idx, and 2) hasToken is an replaceable function,
/// then this class replaces some nodes in the ActionsDAG (and references to them) to generate an equivalent query:
///     SELECT count() FROM table where __text_index_text_col_idx_hasToken_0
///
/// This class is a (C++) friend of ActionsDAG and can therefore access its private members.
/// Some of the functions implemented here could be added to ActionsDAG directly, but this wrapper approach
/// simplifies the work by avoiding conflicts and minimizing coupling between this optimization and ActionsDAG.
class TextIndexDAGReplacer
{
public:
    TextIndexDAGReplacer(ActionsDAG & actions_dag_, const TextIndexReadInfos & text_index_read_infos_, bool direct_read_from_text_index_)
        : actions_dag(actions_dag_)
        , text_index_read_infos(text_index_read_infos_)
        , direct_read_from_text_index(direct_read_from_text_index_)
    {
    }

    struct ResultReplacement
    {
        IndexReadColumns added_columns;
        Names removed_columns;
        const ActionsDAG::Node * filter_node = nullptr;
    };

    /// Replaces text-search functions by virtual columns.
    /// Example: hasToken(text_col, 'token') -> __text_index_text_col_idx_hasToken_0.
    ResultReplacement replace(const ContextPtr & context, const String & filter_column_name)
    {
        ResultReplacement result;
        NodesReplacementMap replacements;
        Names original_inputs = actions_dag.getRequiredColumnsNames();
        const auto * filter_node = &actions_dag.findInOutputs(filter_column_name);
        /// Cache for added input nodes for each virtual column.
        std::unordered_map<String, const ActionsDAG::Node *> virtual_column_to_node;

        for (ActionsDAG::Node & node : actions_dag.nodes)
        {
            auto replaced = processFunctionNode(node, virtual_column_to_node, context);

            if (replaced.has_value())
            {
                replacements[&node] = replaced->node;
                ASTPtr default_expression;

                if (!replaced->is_fully_materialized)
                    default_expression = convertNodeToAST(node);

                for (const auto & [index_name, virtual_column_name] : replaced->added_virtual_columns)
                {
                    VirtualColumnDescription virtual_column(virtual_column_name, std::make_shared<DataTypeUInt8>(), nullptr, index_name, VirtualsKind::Ephemeral);
                    virtual_column.default_desc.kind = ColumnDefaultKind::Default;
                    virtual_column.default_desc.expression = default_expression;
                    result.added_columns[index_name].add(std::move(virtual_column));
                }
            }
        }

        if (result.added_columns.empty())
            return result;

        for (auto & output : actions_dag.outputs)
        {
            bool is_filter_node = output == filter_node;
            output = replaceNodes(actions_dag, output, replacements);

            if (is_filter_node)
                filter_node = output;
        }

        result.filter_node = filter_node;
        actions_dag.removeUnusedActions();

        Names replaced_columns = actions_dag.getRequiredColumnsNames();
        NameSet replaced_columns_set(replaced_columns.begin(), replaced_columns.end());

        for (const auto & column : original_inputs)
        {
            if (!replaced_columns_set.contains(column))
                result.removed_columns.push_back(column);
        }

        return result;
    }

private:
    struct NodeReplacement
    {
        const ActionsDAG::Node * node = nullptr;
        std::unordered_map<String, String> added_virtual_columns;
        bool is_fully_materialized = true;
    };

    ActionsDAG & actions_dag;
    TextIndexReadInfos text_index_read_infos;
    bool direct_read_from_text_index = false;

    struct SelectedCondition
    {
        TextSearchQueryPtr search_query;
        String index_name;
        String virtual_column_name;
        const TextIndexReadInfo * info = nullptr;
    };

    static bool isTextIndexFunction(const String & function_name)
    {
        return function_name == "hasAllTokens" || function_name == "hasAnyTokens";
    }

    std::vector<SelectedCondition> selectConditions(const ActionsDAG::Node & function_node)
    {
        NameSet used_index_columns;
        std::vector<SelectedCondition> selected_conditions;

        for (const auto & [index_name, info] : text_index_read_infos)
        {
            auto & text_index_condition = typeid_cast<MergeTreeIndexConditionText &>(*info.index->condition);
            const auto & index_header = text_index_condition.getHeader();

            /// Do not optimize if there are multiple text indexes set for the same expression.
            /// It is ambiguous which index to use. However, we allow to use several indexes for different expressions.
            /// for example, we can use indexes both for mapKeys(m) and mapValues(m) in one function m['key'] = 'value'.
            if (index_header.columns() != 1 || used_index_columns.contains(index_header.begin()->name))
                continue;

            auto search_query = text_index_condition.createTextSearchQuery(function_node);
            if (!search_query || search_query->direct_read_mode == TextIndexDirectReadMode::None)
                continue;

            auto virtual_column_name = text_index_condition.replaceToVirtualColumn(*search_query, index_name);
            if (!virtual_column_name)
                continue;

            selected_conditions.emplace_back(search_query, index_name, *virtual_column_name, &info);
            used_index_columns.insert(index_header.begin()->name);
        }

        return selected_conditions;
    }

    /// Attempts to add a new node with the replacement virtual column.
    /// Returns the pair of (index name, virtual column name) if the replacement is successful.
    std::optional<NodeReplacement> processFunctionNode(
        ActionsDAG::Node & function_node,
        std::unordered_map<String, const ActionsDAG::Node *> & virtual_column_to_node,
        const ContextPtr & context)
    {
        if (function_node.type != ActionsDAG::ActionType::FUNCTION || !function_node.function || !function_node.function_base)
            return std::nullopt;

        /// Skip if function is not a predicate. It doesn't make sense to analyze it.
        if (!function_node.result_type->canBeUsedInBooleanContext())
            return std::nullopt;

        bool is_text_index_function = isTextIndexFunction(function_node.function_base->getName());
        if (!is_text_index_function && !direct_read_from_text_index)
            return std::nullopt;

        auto selected_conditions = selectConditions(function_node);
        if (selected_conditions.empty())
            return std::nullopt;

        /// Sort conditions to produce stable output for EXPLAIN query.
        std::ranges::sort(selected_conditions, [](const auto & lhs, const auto & rhs)
        {
            return lhs.virtual_column_name < rhs.virtual_column_name;
        });

        if (is_text_index_function)
            preprocessTextIndexFunction(function_node, selected_conditions, context);

        if (direct_read_from_text_index && !selected_conditions.empty())
            return tryReplaceFunctionNode(function_node, selected_conditions, virtual_column_to_node, context);

        return std::nullopt;
    }

    void preprocessTextIndexFunction(
        ActionsDAG::Node & function_node,
        const std::vector<SelectedCondition> & selected_conditions,
        const ContextPtr & context)
    {
        if (selected_conditions.size() != 1 || function_node.children.size() != 2)
            return;

        auto new_children = function_node.children;
        const auto & arg_haystack = new_children[0];
        const auto & arg_needles = new_children[1];

        if (arg_needles->type != ActionsDAG::ActionType::COLUMN || !arg_needles->column)
            return;

        String needles;
        if (isStringOrFixedString(removeNullable(arg_needles->result_type))
            && !arg_needles->column->empty()
            && !arg_needles->column->isNullAt(0))
            needles = arg_needles->column->getDataAt(0);

        const auto & condition = selected_conditions.front();
        const auto & condition_text = typeid_cast<MergeTreeIndexConditionText &>(*condition.info->index->condition);

        auto preprocessor = condition_text.getPreprocessor();
        const auto * tokenizer = condition_text.getTokenExtractor();

        if (preprocessor && !preprocessor->empty())
        {
            const auto & preprocessor_dag = preprocessor->getActionsDAG();
            chassert(preprocessor_dag.getOutputs().size() == 1);
            const auto & preprocessor_output = preprocessor_dag.getOutputs().front();
            auto haystack_name = getNameWithoutAliases(arg_haystack);

            if (hasSubexpression(preprocessor_output, haystack_name))
            {
                ActionsDAG preprocessor_dag_copy = preprocessor_dag.clone();
                ActionsDAG::NodeRawConstPtrs merged_outputs;

                actions_dag.mergeNodes(std::move(preprocessor_dag_copy), &merged_outputs);
                chassert(merged_outputs.size() == 1);
                new_children[0] = merged_outputs.front();

                if (!needles.empty())
                    needles = preprocessor->process(needles);
            }
        }

        ColumnWithTypeAndName arg;
        arg.type = std::make_shared<DataTypeArray>(std::make_shared<DataTypeString>());

        if (needles.empty())
        {
            arg.column = IColumn::mutate(arg_needles->column);
            arg.name = arg_needles->result_name;
        }
        else
        {
            std::vector<String> needles_array;
            tokenizer->stringToTokens(needles.data(), needles.size(), needles_array);
            needles_array = tokenizer->compactTokens(needles_array);
            Field needles_field = Array(needles_array.begin(), needles_array.end());

            arg.column = arg.type->createColumnConst(1, needles_field);
            arg.name = applyVisitor(FieldVisitorToString(), needles_field);
        }

        new_children[1] = &actions_dag.addColumn(std::move(arg));

        auto tokenizer_description = tokenizer->getDescription();
        arg.type = std::make_shared<DataTypeString>();
        arg.column = arg.type->createColumnConst(1, Field(tokenizer_description));
        arg.name = quoteString(tokenizer_description);
        new_children.push_back(&actions_dag.addColumn(std::move(arg)));

        auto new_function_base = FunctionFactory::instance().get(function_node.function_base->getName(), context);
        const auto * new_function_node = &actions_dag.addFunction(new_function_base, new_children, "");

        if (!new_function_node->result_type->equals(*function_node.result_type))
            new_function_node = &actions_dag.addCast(*new_function_node, function_node.result_type, "", context);

        /// Convert function node to ALIAS in-place to avoid adding a duplicate node to the DAG.
        function_node.type = ActionsDAG::ActionType::ALIAS;
        function_node.children = {new_function_node};
        function_node.result_type = new_function_node->result_type;
        function_node.column.reset();
        function_node.function_base.reset();
        function_node.function.reset();
        function_node.is_function_compiled = false;
    }

    std::optional<NodeReplacement> tryReplaceFunctionNode(
        ActionsDAG::Node & function_node,
        const std::vector<SelectedCondition> & selected_conditions,
        std::unordered_map<String, const ActionsDAG::Node *> & virtual_column_to_node,
        const ContextPtr & context)
    {
        NodeReplacement replacement;
        replacement.is_fully_materialized = true;
        bool has_exact_search = false;
        bool has_materialized_index = false;

        for (const auto & condition : selected_conditions)
        {
            replacement.is_fully_materialized &= condition.info->is_fully_materialied;
            has_materialized_index |= condition.info->is_materialized;
            has_exact_search |= condition.search_query->direct_read_mode == TextIndexDirectReadMode::Exact;
        }

        auto add_condition_to_input = [&](const SelectedCondition & condition)
        {
            auto [it, inserted] = virtual_column_to_node.try_emplace(condition.virtual_column_name);

            if (inserted)
            {
                it->second = &actions_dag.addInput(condition.virtual_column_name, std::make_shared<DataTypeUInt8>());
                replacement.added_virtual_columns.emplace(condition.index_name, condition.virtual_column_name);
            }

            return it->second;
        };

        if (!has_materialized_index)
            return std::nullopt;

        /// If we have only one condition with exact search, we can use
        /// only virtual column and remove the original condition.
        if (selected_conditions.size() == 1 && has_exact_search)
        {
            replacement.node = add_condition_to_input(selected_conditions.front());
        }
        else /// Otherwise, combine all conditions with the AND function.
        {
            ActionsDAG::NodeRawConstPtrs children;
            auto function_builder = FunctionFactory::instance().get("and", context);

            for (const auto & condition : selected_conditions)
                children.push_back(add_condition_to_input(condition));

            if (!has_exact_search)
                children.push_back(&function_node);

            replacement.node = &actions_dag.addFunction(function_builder, children, "");
        }

        /// If the type of original function does not match the type of replacement,
        /// add a cast to the replacement to match the expected type.
        /// For example, it can happen when the original function returns Nullable or LowCardinality type and replacement doesn't.
        if (!function_node.result_type->equals(*replacement.node->result_type))
            replacement.node = &actions_dag.addCast(*replacement.node, function_node.result_type, "", context);

        return replacement;
    }
};

const ActionsDAG::Node * applyTextIndexDirectReadToDAG(
    ReadFromMergeTree & read_from_merge_tree_step,
    ActionsDAG & filter_dag,
    const TextIndexReadInfos & text_index_read_infos,
    const String & filter_column_name,
    bool direct_read_from_text_index)
{
    TextIndexDAGReplacer replacer(filter_dag, text_index_read_infos, direct_read_from_text_index);
    auto result = replacer.replace(read_from_merge_tree_step.getContext(), filter_column_name);

    if (result.added_columns.empty())
        return nullptr;

    auto logger = getLogger("optimizeDirectReadFromTextIndex");
    LOG_DEBUG(logger, "{}", optimizationInfoToString(result.added_columns, result.removed_columns));

    /// Log partially materialized text indexes
    for (const auto & [index_name, info] : text_index_read_infos)
    {
        if (!info.is_fully_materialied)
            LOG_DEBUG(logger, "Text index '{}' is not fully materialized.", index_name);
    }

    const auto & indexes = read_from_merge_tree_step.getIndexes();
    bool is_final = read_from_merge_tree_step.isQueryWithFinal();
    read_from_merge_tree_step.createReadTasksForTextIndex(indexes->skip_indexes, result.added_columns, result.removed_columns, is_final);
    return result.filter_node;
}

/// Replaces text-search functions in the PREWHERE clause with virtual columns for direct index reads.
bool optimizePrewhereDirectReadFromTextIndex(
    ReadFromMergeTree & read_from_merge_tree_step,
    const PrewhereInfoPtr & prewhere_info,
    const TextIndexReadInfos & text_index_read_infos,
    bool direct_read_from_text_index)
{
    read_from_merge_tree_step.updatePrewhereInfo({});
    auto cloned_prewhere_info = prewhere_info->clone();
    const auto * result_filter_node = applyTextIndexDirectReadToDAG(read_from_merge_tree_step, cloned_prewhere_info.prewhere_actions, text_index_read_infos, cloned_prewhere_info.prewhere_column_name, direct_read_from_text_index);

    if (!result_filter_node)
    {
        read_from_merge_tree_step.updatePrewhereInfo(prewhere_info);
        return false;
    }

    /// Finally, assign the corrected PrewhereInfo back to the plan node.
    cloned_prewhere_info.prewhere_column_name = result_filter_node->result_name;
    auto modified_prewhere_info = std::make_shared<PrewhereInfo>(std::move(cloned_prewhere_info));
    read_from_merge_tree_step.updatePrewhereInfo(modified_prewhere_info);
    return true;
}

/// Applies text index optimizations to the query plan.
///
/// Always preprocesses `hasAllTokens`/`hasAnyTokens` arguments with text index metadata
/// (preprocessor wrapping, string-to-array tokenization, tokenizer arguments).
///
/// When `direct_read_from_text_index` is true, also replaces text-search functions
/// with virtual columns for direct index reads (both WHERE and PREWHERE clauses).
void optimizeDirectReadFromTextIndex(const Stack & stack, QueryPlan::Nodes & /*nodes*/, bool direct_read_from_text_index)
{
    const auto & frame = stack.back();
    ReadFromMergeTree * read_from_merge_tree_step = typeid_cast<ReadFromMergeTree *>(frame.node->step.get());
    if (!read_from_merge_tree_step)
        return;

    TextIndexReadInfos text_index_read_infos;
    collectTextIndexReadInfos(read_from_merge_tree_step, text_index_read_infos);
    if (text_index_read_infos.empty())
        return;

    bool optimized = false;
    if (auto prewhere_info = read_from_merge_tree_step->getPrewhereInfo())
        optimized = optimizePrewhereDirectReadFromTextIndex(*read_from_merge_tree_step, prewhere_info, text_index_read_infos, direct_read_from_text_index);

    if (stack.size() < 2)
        return;

    QueryPlan::Node * filter_node = (stack.rbegin() + 1)->node;
    auto * filter_step = typeid_cast<FilterStep *>(filter_node->step.get());

    if (!filter_step)
        return;

    ActionsDAG & filter_dag = filter_step->getExpression();
    const auto * result_filter_node = applyTextIndexDirectReadToDAG(*read_from_merge_tree_step, filter_dag, text_index_read_infos, filter_step->getFilterColumnName(), direct_read_from_text_index && !optimized);

    if (!result_filter_node)
        return;

    bool removes_filter_column = filter_step->removesFilterColumn();
    auto new_filter_column_name = result_filter_node->result_name;
    filter_node->step = std::make_unique<FilterStep>(read_from_merge_tree_step->getOutputHeader(), filter_dag.clone(), new_filter_column_name, removes_filter_column);
}

}
