#pragma once

#include <Analyzer/InDepthQueryTreeVisitor.h>
#include <Analyzer/TableFunctionNode.h>
#include <TableFunctions/TableFunctionFactory.h>
#include <TableFunctions/TableFunctionFile.h>

namespace DB
{

class TableFunctionsWithClusterAlternativesVisitor : public InDepthQueryTreeVisitor<TableFunctionsWithClusterAlternativesVisitor, /*const_visitor=*/true>
{
public:
    void visitImpl(const QueryTreeNodePtr & node)
    {
        if (node->getNodeType() == QueryTreeNodeType::TABLE_FUNCTION)
            ++table_function_count;
        else if (node->getNodeType() == QueryTreeNodeType::TABLE)
            ++table_count;
        else if (node->getNodeType() == QueryTreeNodeType::JOIN)
            has_join = true;
    }

    bool needChildVisit(const QueryTreeNodePtr & parent, const QueryTreeNodePtr & child) 
    {
        if (child->getNodeType() == QueryTreeNodeType::QUERY)
        {
            auto * child_query = child->as<QueryNode>();
            if (child_query && child_query->isSubquery())
            {
                if (parent->getNodeType() == QueryTreeNodeType::QUERY)
                {
                    auto * parent_query = parent->as<QueryNode>();
                    if (parent_query && parent_query->getJoinTree().get() == child.get())
                    {
                        ++subquery_in_from_count;
                    }
                }
            }
        }
        
        return true;
    }

    bool shouldReplaceWithClusterAlternatives() const
    {
        return subquery_in_from_count <= 1 && !has_join && ((table_count + table_function_count) == 1 || (table_function_count == 0));
    }

private:
    size_t table_count = 0;
    size_t table_function_count = 0;
    size_t subquery_in_from_count = 0;

    bool has_join = false;
};

}
