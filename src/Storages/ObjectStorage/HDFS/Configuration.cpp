#include <Storages/ObjectStorage/HDFS/Configuration.h>

#if USE_HDFS
#include <Common/logger_useful.h>
#include <Common/RemoteHostFilter.h>
#include <Core/Settings.h>
#include <Parsers/IAST.h>
#include <Formats/FormatFactory.h>
#include <Disks/ObjectStorages/HDFS/HDFSObjectStorage.h>

#include <Interpreters/Context.h>
#include <Interpreters/evaluateConstantExpression.h>

#include <Storages/NamedCollectionsHelpers.h>
#include <Storages/checkAndGetLiteralArgument.h>
#include <Storages/ObjectStorage/HDFS/HDFSCommon.h>

#include <Parsers/ASTFunction.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>

namespace DB
{
namespace Setting
{
    extern const SettingsBool hdfs_create_new_file_on_insert;
    extern const SettingsBool hdfs_ignore_file_doesnt_exist;
    extern const SettingsUInt64 hdfs_replication;
    extern const SettingsBool hdfs_skip_empty_files;
    extern const SettingsBool hdfs_throw_on_zero_files_match;
    extern const SettingsBool hdfs_truncate_on_insert;
    extern const SettingsUInt64 remote_read_min_bytes_for_seek;
    extern const SettingsSchemaInferenceMode schema_inference_mode;
    extern const SettingsBool schema_inference_use_cache_for_hdfs;
}

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int LOGICAL_ERROR;
}

void StorageHDFSConfiguration::check(ContextPtr context) const
{
    context->getRemoteHostFilter().checkURL(Poco::URI(url));
    checkHDFSURL(fs::path(url) / path.path.substr(1));
    StorageObjectStorageConfiguration::check(context);
}

ObjectStoragePtr StorageHDFSConfiguration::createObjectStorage( /// NOLINT
    ContextPtr context,
    bool /* is_readonly */)
{
    assertInitialized();
    const auto & settings = context->getSettingsRef();
    auto hdfs_settings = std::make_unique<HDFSObjectStorageSettings>(settings[Setting::remote_read_min_bytes_for_seek], settings[Setting::hdfs_replication]);
    return std::make_shared<HDFSObjectStorage>(
        url, std::move(hdfs_settings), context->getConfigRef(), /* lazy_initialize */true);
}

StorageObjectStorageQuerySettings StorageHDFSConfiguration::getQuerySettings(const ContextPtr & context) const
{
    const auto & settings = context->getSettingsRef();
    return StorageObjectStorageQuerySettings{
        .truncate_on_insert = settings[Setting::hdfs_truncate_on_insert],
        .create_new_file_on_insert = settings[Setting::hdfs_create_new_file_on_insert],
        .schema_inference_use_cache = settings[Setting::schema_inference_use_cache_for_hdfs],
        .schema_inference_mode = settings[Setting::schema_inference_mode],
        .skip_empty_files = settings[Setting::hdfs_skip_empty_files],
        .list_object_keys_size = 0, /// HDFS does not support listing in batches.
        .throw_on_zero_files_match = settings[Setting::hdfs_throw_on_zero_files_match],
        .ignore_non_existent_file = settings[Setting::hdfs_ignore_file_doesnt_exist],
    };
}

void StorageHDFSConfiguration::fromAST(ASTs & args, ContextPtr context, bool with_structure)
{
    if (args.empty() || args.size() > getMaxNumberOfArguments(with_structure))
        throw Exception(
            ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
            "Storage HDFS requires 1 to {} arguments. All supported signatures:\n{}",
            getMaxNumberOfArguments(with_structure),
            getSignatures(with_structure));


    std::string url_str;
    url_str = checkAndGetLiteralArgument<String>(args[0], "url");

    for (auto & arg : args)
        arg = evaluateConstantExpressionOrIdentifierAsLiteral(arg, context);

    if (args.size() > 1)
    {
        format = checkAndGetLiteralArgument<String>(args[1], "format_name");
    }

    if (with_structure)
    {
        if (args.size() > 2)
        {
            structure = checkAndGetLiteralArgument<String>(args[2], "structure");
        }
        if (args.size() > 3)
        {
            compression_method = checkAndGetLiteralArgument<String>(args[3], "compression_method");
        }
    }
    else if (args.size() > 2)
    {
        compression_method = checkAndGetLiteralArgument<String>(args[2], "compression_method");
    }

    setURL(url_str);
}

void StorageHDFSConfiguration::fromNamedCollection(const NamedCollection & collection, ContextPtr)
{
    std::string url_str;

    auto filename = collection.getOrDefault<String>("filename", "");
    if (!filename.empty())
        url_str = std::filesystem::path(collection.get<String>("url")) / filename;
    else
        url_str = collection.get<String>("url");

    format = collection.getOrDefault<String>("format", "auto");
    compression_method = collection.getOrDefault<String>("compression_method",
                                                         collection.getOrDefault<String>("compression", "auto"));
    structure = collection.getOrDefault<String>("structure", "auto");

    setURL(url_str);
}

void StorageHDFSConfiguration::setURL(const std::string & url_)
{
    auto pos = url_.find("//");
    if (pos == std::string::npos)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Bad HDFS URL: {}. It should have the following structure 'hdfs://<host_name>:<port>/path'", url_);

    pos = url_.find('/', pos + 2);
    if (pos == std::string::npos)
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Bad HDFS URL: {}. It should have the following structure 'hdfs://<host_name>:<port>/path'", url_);

    path = url_.substr(pos + 1);
    if (!path.path.starts_with('/'))
        path = '/' + path.path;

    url = url_.substr(0, pos);
    paths = {path};

    LOG_TRACE(getLogger("StorageHDFSConfiguration"), "Using URL: {}, path: {}", url, path.path);
}

void StorageHDFSConfiguration::addStructureAndFormatToArgsIfNeeded(
    ASTs & args,
    const String & structure_,
    const String & format_,
    ContextPtr context,
    bool with_structure)
{
    if (auto collection = tryGetNamedCollectionWithOverrides(args, context))
    {
        /// In case of named collection, just add key-value pairs "format='...', structure='...'"
        /// at the end of arguments to override existed format and structure with "auto" values.
        if (collection->getOrDefault<String>("format", "auto") == "auto")
        {
            ASTs format_equal_func_args = {std::make_shared<ASTIdentifier>("format"), std::make_shared<ASTLiteral>(format_)};
            auto format_equal_func = makeASTFunction("equals", std::move(format_equal_func_args));
            args.push_back(format_equal_func);
        }
        if (with_structure && collection->getOrDefault<String>("structure", "auto") == "auto")
        {
            ASTs structure_equal_func_args = {std::make_shared<ASTIdentifier>("structure"), std::make_shared<ASTLiteral>(structure_)};
            auto structure_equal_func = makeASTFunction("equals", std::move(structure_equal_func_args));
            args.push_back(structure_equal_func);
        }
    }
    else
    {
        size_t count = args.size();
        if (count == 0 || count > getMaxNumberOfArguments())
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Expected 1 to {} arguments in table function hdfs, got {}", getMaxNumberOfArguments(), count);

        auto format_literal = std::make_shared<ASTLiteral>(format_);
        auto structure_literal = std::make_shared<ASTLiteral>(structure_);

        for (auto & arg : args)
            arg = evaluateConstantExpressionOrIdentifierAsLiteral(arg, context);

        /// hdfs(url)
        if (count == 1)
        {
            /// Add format=auto before structure argument.
            args.push_back(format_literal);
            if (with_structure)
                args.push_back(structure_literal);
        }
        /// hdfs(url, format)
        else if (count == 2)
        {
            if (checkAndGetLiteralArgument<String>(args[1], "format") == "auto")
                args.back() = format_literal;
            if (with_structure)
                args.push_back(structure_literal);
        }
        /// hdfs(url, format, structure)
        /// hdfs(url, format, structure, compression_method)
        /// hdfs(url, format, compression_method)
        else if (count >= 3)
        {
            if (checkAndGetLiteralArgument<String>(args[1], "format") == "auto")
                args[1] = format_literal;
            if (with_structure && checkAndGetLiteralArgument<String>(args[2], "structure") == "auto")
                args[2] = structure_literal;
        }
    }
}

}

#endif
