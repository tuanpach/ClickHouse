#pragma once

#include <Parsers/ASTQueryWithTableAndOutput.h>
#include <Common/quoteString.h>


namespace DB
{

struct ASTExistsDatabaseQueryIDAndQueryNames
{
    static constexpr auto ID = "ExistsDatabaseQuery";
    static constexpr auto Query = "EXISTS DATABASE";
    /// No temporary databases are supported, just for parsing
    static constexpr auto QueryTemporary = "";
};

struct ASTExistsTableQueryIDAndQueryNames
{
    static constexpr auto ID = "ExistsTableQuery";
    static constexpr auto Query = "EXISTS TABLE";
    static constexpr auto QueryTemporary = "EXISTS TEMPORARY TABLE";
};

struct ASTExistsViewQueryIDAndQueryNames
{
    static constexpr auto ID = "ExistsViewQuery";
    static constexpr auto Query = "EXISTS VIEW";
    /// No temporary view are supported, just for parsing
    static constexpr auto QueryTemporary = "";
};


struct ASTExistsDictionaryQueryIDAndQueryNames
{
    static constexpr auto ID = "ExistsDictionaryQuery";
    static constexpr auto Query = "EXISTS DICTIONARY";
    /// No temporary dictionaries are supported, just for parsing
    static constexpr auto QueryTemporary = "EXISTS TEMPORARY DICTIONARY";
};

struct ASTShowCreateTableQueryIDAndQueryNames
{
    static constexpr auto ID = "ShowCreateTableQuery";
    static constexpr auto Query = "SHOW CREATE TABLE";
    static constexpr auto QueryTemporary = "SHOW CREATE TEMPORARY TABLE";
};

struct ASTShowCreateViewQueryIDAndQueryNames
{
    static constexpr auto ID = "ShowCreateViewQuery";
    static constexpr auto Query = "SHOW CREATE VIEW";
    /// No temporary view are supported, just for parsing
    static constexpr auto QueryTemporary = "";
};

struct ASTShowCreateDatabaseQueryIDAndQueryNames
{
    static constexpr auto ID = "ShowCreateDatabaseQuery";
    static constexpr auto Query = "SHOW CREATE DATABASE";
    static constexpr auto QueryTemporary = "SHOW CREATE TEMPORARY DATABASE";
};

struct ASTShowCreateDictionaryQueryIDAndQueryNames
{
    static constexpr auto ID = "ShowCreateDictionaryQuery";
    static constexpr auto Query = "SHOW CREATE DICTIONARY";
    /// No temporary dictionaries are supported, just for parsing
    static constexpr auto QueryTemporary = "SHOW CREATE TEMPORARY DICTIONARY";
};

struct ASTDescribeQueryExistsQueryIDAndQueryNames
{
    static constexpr auto ID = "DescribeQuery";
    static constexpr auto Query = "DESCRIBE TABLE";
    static constexpr auto QueryTemporary = "DESCRIBE TEMPORARY TABLE";
};

using ASTExistsTableQuery = ASTQueryWithTableAndOutputImpl<ASTExistsTableQueryIDAndQueryNames>;
using ASTExistsViewQuery = ASTQueryWithTableAndOutputImpl<ASTExistsViewQueryIDAndQueryNames>;
using ASTExistsDictionaryQuery = ASTQueryWithTableAndOutputImpl<ASTExistsDictionaryQueryIDAndQueryNames>;
using ASTShowCreateTableQuery = ASTQueryWithTableAndOutputImpl<ASTShowCreateTableQueryIDAndQueryNames>;
using ASTShowCreateViewQuery = ASTQueryWithTableAndOutputImpl<ASTShowCreateViewQueryIDAndQueryNames>;
using ASTShowCreateDictionaryQuery = ASTQueryWithTableAndOutputImpl<ASTShowCreateDictionaryQueryIDAndQueryNames>;

class ASTExistsDatabaseQuery : public ASTQueryWithTableAndOutputImpl<ASTExistsDatabaseQueryIDAndQueryNames>
{
public:
    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTExistsDatabaseQuery>(*this);
        res->children.clear();
        cloneTableOptions(*res);
        return res;
    }

protected:
    void formatQueryImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
    {
        ostr << ASTExistsDatabaseQueryIDAndQueryNames::Query
                    << " ";
        database->format(ostr, settings, state, frame);
    }

    QueryKind getQueryKind() const override { return QueryKind::Exists; }
};

class ASTShowCreateDatabaseQuery : public ASTQueryWithTableAndOutputImpl<ASTShowCreateDatabaseQueryIDAndQueryNames>
{
public:
    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTShowCreateDatabaseQuery>(*this);
        res->children.clear();
        cloneTableOptions(*res);
        return res;
    }

protected:
    void formatQueryImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
    {
        ostr << ASTShowCreateDatabaseQueryIDAndQueryNames::Query
                      << " ";
        database->format(ostr, settings, state, frame);
    }
};

class ASTDescribeQuery : public ASTQueryWithOutput
{
public:
    ASTPtr table_expression;

    String getID(char) const override { return "DescribeQuery"; }

    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTDescribeQuery>(*this);
        res->children.clear();
        if (table_expression)
        {
            res->table_expression = table_expression->clone();
            res->children.push_back(res->table_expression);
        }
        cloneOutputOptions(*res);
        return res;
    }

    QueryKind getQueryKind() const override { return QueryKind::Describe; }

protected:
    void formatQueryImpl(WriteBuffer & ostr, const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
    {
        ostr
                      << "DESCRIBE TABLE";
        table_expression->format(ostr, settings, state, frame);
    }

};

}
