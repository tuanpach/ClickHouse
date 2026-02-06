#pragma once

#include <Interpreters/ITokenExtractor.h>
#include <Core/Field.h>
#include <Parsers/IAST_fwd.h>

#include <boost/noncopyable.hpp>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace DB
{

class TokenizerFactory final : private boost::noncopyable
{
public:
    using Creator = std::function<std::unique_ptr<ITokenExtractor>(const FieldVector &)>;
    static TokenizerFactory & instance();

    /// Create a tokenizer from an AST node.
    /// Supports ASTIdentifier (name without args), ASTLiteral with string value (name without args),
    /// and ASTFunction (name with literal args).
    std::unique_ptr<ITokenExtractor> get(const ASTPtr & ast) const;

    /// Create a tokenizer by name and arguments.
    /// If allowed is non-empty, only tokenizers of the specified types can be created.
    std::unique_ptr<ITokenExtractor> get(
        std::string_view name,
        const FieldVector & args = {},
        const std::set<ITokenExtractor::Type> & allowed = {}) const;

    void registerTokenizer(const String & name, ITokenExtractor::Type type, Creator creator);

private:
    TokenizerFactory();

    struct Entry
    {
        ITokenExtractor::Type type;
        Creator creator;
    };

    using Tokenizers = std::unordered_map<String, Entry>;
    Tokenizers tokenizers;
};

void registerTokenizers(TokenizerFactory & factory);

}
