#pragma once

#include "config.h"

#if USE_MONGODB
#include <Common/JSONBuilder.h>
#include <Processors/ISource.h>
#include <Storages/StorageMongoDB.h>

#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/cursor.hpp>
#include <mongocxx/database.hpp>

namespace DB
{

/// Creates MongoDB connection and cursor, converts it to a stream of blocks
class MongoDBSource final : public ISource
{
public:
    MongoDBSource(
        const mongocxx::uri & uri,
        const std::string & collection_name,
        const bsoncxx::document::view_or_value & query,
        const mongocxx::options::find & options,
        SharedHeader sample_block_,
        const UInt64 & max_block_size_);

    ~MongoDBSource() override;

    String getName() const override { return "MongoDB"; }

private:
    MongoDBInstanceHolder & instance_holder = MongoDBInstanceHolder::instance();

    static void insertDefaultValue(IColumn & column, const IColumn & sample_column);
    void insertValue(IColumn & column, const size_t & idx, const DataTypePtr & type, const std::string & name, const bsoncxx::document::element & value);

    Chunk generate() override;

    mongocxx::client client;
    mongocxx::database database;
    mongocxx::collection collection;
    mongocxx::cursor cursor;

    Block sample_block;
    std::unordered_map<size_t, std::pair<size_t, std::pair<DataTypePtr, Field>>> arrays_info;
    const UInt64 max_block_size;
    bool all_read = false;

    const DB::FormatSettings db_json_format_settings;
    const JSONBuilder::FormatSettings json_format_settings;
};

}
#endif
