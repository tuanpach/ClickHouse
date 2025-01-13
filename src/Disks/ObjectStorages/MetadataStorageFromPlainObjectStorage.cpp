#include "MetadataStorageFromPlainObjectStorage.h"

#include <Disks/IDisk.h>
#include <Disks/ObjectStorages/IObjectStorage.h>
#include <Disks/ObjectStorages/InMemoryDirectoryPathMap.h>
#include <Disks/ObjectStorages/MetadataStorageFromPlainObjectStorageOperations.h>
#include <Disks/ObjectStorages/StaticDirectoryIterator.h>
#include <Disks/ObjectStorages/StoredObject.h>
#include <Common/ObjectStorageKey.h>
#include <Common/SipHash.h>
#include <Common/logger_useful.h>

#include <IO/ReadHelpers.h>
#include <IO/ReadSettings.h>
#include <Common/filesystemHelpers.h>

#include <filesystem>

namespace DB
{

namespace ErrorCodes
{
    extern const int FILE_DOESNT_EXIST;
    extern const int FILE_ALREADY_EXISTS;
}

namespace
{

std::filesystem::path normalizeDirectoryPath(const std::filesystem::path & path)
{
    return path / "";
}

}

MetadataStorageFromPlainObjectStorage::MetadataStorageFromPlainObjectStorage(
    ObjectStoragePtr object_storage_, String storage_path_prefix_, size_t object_metadata_cache_size)
    : object_storage(object_storage_), storage_path_prefix(std::move(storage_path_prefix_))
{
    if (object_metadata_cache_size)
        object_metadata_cache.emplace(object_metadata_cache_size);
}

MetadataTransactionPtr MetadataStorageFromPlainObjectStorage::createTransaction()
{
    return std::make_shared<MetadataStorageFromPlainObjectStorageTransaction>(*this, object_storage);
}

const std::string & MetadataStorageFromPlainObjectStorage::getPath() const
{
    return storage_path_prefix;
}

bool MetadataStorageFromPlainObjectStorage::existsFile(const std::string & path) const
{
    ObjectStorageKey object_key = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */);
    StoredObject object(object_key.serialize(), path);
    if (!object_storage->exists(object))
        return false;

    /// The path does not correspond to a directory.
    /// This check is required for a local object storage since it supports hierarchy.
    auto directory = std::filesystem::path(object_key.serialize()) / "";
    ObjectStorageKey directory_key = object_storage->generateObjectKeyForPath(directory, std::nullopt /* key_prefix */);
    return !object_storage->exists(StoredObject(directory_key.serialize(), directory));
}

bool MetadataStorageFromPlainObjectStorage::existsDirectory(const std::string & path) const
{
    auto key_prefix = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */).serialize();
    auto directory = std::filesystem::path(std::move(key_prefix)) / "";
    return object_storage->existsOrHasAnyChild(directory);
}

bool MetadataStorageFromPlainObjectStorage::existsFileOrDirectory(const std::string & path) const
{
    /// NOTE: exists() cannot be used here since it works only for existing
    /// key, and does not work for some intermediate path.
    auto key_prefix = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */).serialize();
    return object_storage->existsOrHasAnyChild(key_prefix);
}


uint64_t MetadataStorageFromPlainObjectStorage::getFileSize(const String & path) const
{
    if (auto res = getFileSizeIfExists(path))
        return *res;
    throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "File {} does not exist on {}", path, object_storage->getName());
}

std::optional<uint64_t> MetadataStorageFromPlainObjectStorage::getFileSizeIfExists(const String & path) const
{
    if (auto res = getObjectMetadataEntryWithCache(path))
        return res->file_size;
    return std::nullopt;
}

Poco::Timestamp MetadataStorageFromPlainObjectStorage::getLastModified(const std::string & path) const
{
    if (auto res = getLastModifiedIfExists(path))
        return *res;
    else
        throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "File or directory {} does not exist on {}", path, object_storage->getName());
}

std::optional<Poco::Timestamp> MetadataStorageFromPlainObjectStorage::getLastModifiedIfExists(const std::string & path) const
{
    /// Since the plain object storage is used for backups only, return the current time.
    if (existsFileOrDirectory(path))
        return Poco::Timestamp{};
    return std::nullopt;
}

std::vector<std::string> MetadataStorageFromPlainObjectStorage::listDirectory(const std::string & path) const
{
    auto key_prefix = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */).serialize();

    RelativePathsWithMetadata files;
    std::string absolute_key = key_prefix;
    if (!absolute_key.ends_with('/'))
        absolute_key += '/';

    object_storage->listObjects(absolute_key, files, 0);

    std::unordered_set<std::string> result;
    for (const auto & elem : files)
    {
        const auto & p = elem->relative_path;
        chassert(p.find(absolute_key) == 0);
        const auto child_pos = absolute_key.size();
        /// string::npos is ok.
        const auto slash_pos = p.find('/', child_pos);
        if (slash_pos == std::string::npos)
            result.emplace(p.substr(child_pos));
        else
            result.emplace(p.substr(child_pos, slash_pos - child_pos));
    }
    return std::vector<std::string>(std::make_move_iterator(result.begin()), std::make_move_iterator(result.end()));
}

DirectoryIteratorPtr MetadataStorageFromPlainObjectStorage::iterateDirectory(const std::string & path) const
{
    /// Required for MergeTree
    auto paths = listDirectory(path);
    /// Prepend path, since iterateDirectory() includes path, unlike listDirectory()
    std::for_each(paths.begin(), paths.end(), [&](auto & child) { child = fs::path(path) / child; });
    std::vector<std::filesystem::path> fs_paths(paths.begin(), paths.end());
    return std::make_unique<StaticDirectoryIterator>(std::move(fs_paths));
}

StoredObjects MetadataStorageFromPlainObjectStorage::getStorageObjects(const std::string & path) const
{
    size_t object_size = getFileSize(path);
    auto object_key = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */);
    return {StoredObject(object_key.serialize(), path, object_size)};
}

std::optional<StoredObjects> MetadataStorageFromPlainObjectStorage::getStorageObjectsIfExist(const std::string & path) const
{
    if (auto object_size = getFileSizeIfExists(path))
    {
        auto object_key = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */);
        return StoredObjects{StoredObject(object_key.serialize(), path, *object_size)};
    }
    return std::nullopt;
}

MetadataStorageFromPlainObjectStorage::ObjectMetadataEntryPtr
MetadataStorageFromPlainObjectStorage::getObjectMetadataEntryWithCache(const std::string & path) const
{
    auto object_key = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */);
    auto get = [&] -> ObjectMetadataEntryPtr
    {
        if (auto metadata = object_storage->tryGetObjectMetadata(object_key.serialize()))
            return std::make_shared<ObjectMetadataEntry>(metadata->size_bytes, metadata->last_modified.epochTime());
        return nullptr;
    };

    if (object_metadata_cache)
    {
        SipHash hash;
        hash.update(object_key.serialize());
        auto hash128 = hash.get128();
        if (auto res = object_metadata_cache->get(hash128))
            return res;
        if (auto mapped = get())
            return object_metadata_cache->getOrSet(hash128, [&] { return mapped; }).first;
        return object_metadata_cache->get(hash128);
    }
    return get();
}

const IMetadataStorage & MetadataStorageFromPlainObjectStorageTransaction::getStorageForNonTransactionalReads() const
{
    return metadata_storage;
}

void MetadataStorageFromPlainObjectStorageTransaction::unlinkFile(const std::string & path)
{
    auto object_key = metadata_storage.object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */);
    auto object = StoredObject(object_key.serialize());
    metadata_storage.object_storage->removeObjectIfExists(object);
}

void MetadataStorageFromPlainObjectStorageTransaction::removeDirectory(const std::string & path)
{
    if (metadata_storage.object_storage->isWriteOnce())
    {
        for (auto it = metadata_storage.iterateDirectory(path); it->isValid(); it->next())
            metadata_storage.object_storage->removeObjectIfExists(StoredObject(it->path()));
    }
    else
    {
        addOperation(std::make_unique<MetadataStorageFromPlainObjectStorageRemoveDirectoryOperation>(
            normalizeDirectoryPath(path), *metadata_storage.getPathMap(), object_storage, metadata_storage.getMetadataKeyPrefix()));
    }
}

void MetadataStorageFromPlainObjectStorageTransaction::createEmptyMetadataFile(const std::string & path)
{
    if (metadata_storage.object_storage->isWriteOnce())
        return;

    addOperation(
        std::make_unique<MetadataStorageFromPlainObjectStorageWriteFileOperation>(path, *metadata_storage.getPathMap(), object_storage));
}

void MetadataStorageFromPlainObjectStorageTransaction::createMetadataFile(
    const std::string & path, ObjectStorageKey /*object_key*/, uint64_t /* size_in_bytes */)
{
    createEmptyMetadataFile(path);
}

void MetadataStorageFromPlainObjectStorageTransaction::createDirectory(const std::string & path)
{
    if (metadata_storage.object_storage->isWriteOnce())
        return;

    auto normalized_path = normalizeDirectoryPath(path);
    auto op = std::make_unique<MetadataStorageFromPlainObjectStorageCreateDirectoryOperation>(
        std::move(normalized_path),
        *metadata_storage.getPathMap(),
        object_storage,
        metadata_storage.getMetadataKeyPrefix());
    addOperation(std::move(op));
}

void MetadataStorageFromPlainObjectStorageTransaction::createDirectoryRecursive(const std::string & path)
{
    createDirectory(path);
}

void MetadataStorageFromPlainObjectStorageTransaction::moveDirectory(const std::string & path_from, const std::string & path_to)
{
    if (metadata_storage.object_storage->isWriteOnce())
        throwNotImplemented();

    addOperation(std::make_unique<MetadataStorageFromPlainObjectStorageMoveDirectoryOperation>(
        normalizeDirectoryPath(path_from),
        normalizeDirectoryPath(path_to),
        *metadata_storage.getPathMap(),
        object_storage,
        metadata_storage.getMetadataKeyPrefix()));
}

void MetadataStorageFromPlainObjectStorageTransaction::moveFile(const std::string & path_from, const std::string & path_to)
{
    if (metadata_storage.object_storage->isWriteOnce())
        throwNotImplemented();

    moveFileHelper(object_storage, *metadata_storage.getPathMap(), path_from, path_to, false);
}

void MetadataStorageFromPlainObjectStorageTransaction::replaceFile(const std::string & path_from, const std::string & path_to)
{
    if (metadata_storage.object_storage->isWriteOnce())
        throwNotImplemented();
    moveFileHelper(object_storage, *metadata_storage.getPathMap(), path_from, path_to, true);
}

UnlinkMetadataFileOperationOutcomePtr MetadataStorageFromPlainObjectStorageTransaction::unlinkMetadata(const std::string & path)
{
    /// The record has become stale, remove it from cache.
    if (metadata_storage.object_metadata_cache)
    {
        auto object_key = object_storage->generateObjectKeyForPath(path, std::nullopt /* key_prefix */);
        SipHash hash;
        hash.update(object_key.serialize());
        metadata_storage.object_metadata_cache->remove(hash.get128());
    }

    auto result = std::make_shared<UnlinkMetadataFileOperationOutcome>(UnlinkMetadataFileOperationOutcome{0});
    if (!metadata_storage.object_storage->isWriteOnce())
        addOperation(std::make_unique<MetadataStorageFromPlainObjectStorageUnlinkMetadataFileOperation>(
            path, *metadata_storage.getPathMap(), object_storage));
    return result;
}

void MetadataStorageFromPlainObjectStorageTransaction::commit()
{
    MetadataOperationsHolder::commitImpl(metadata_storage.metadata_mutex);
}

void MetadataStorageFromPlainObjectStorageTransaction::moveFileHelper(
    ObjectStoragePtr & object_storage,
    InMemoryDirectoryPathMap & path_map,
    const std::filesystem::path & path_from,
    const std::filesystem::path & path_to,
    bool replaceable)
{
    auto logger = getLogger("MetadataStorageFromPlainObjectStorageTransaction");
    LOG_DEBUG(logger, "Moving file '{}' to '{}', replaceable {}", path_from, path_to, replaceable);
    bool target_unique_filename_inserted = false;
    {
        std::lock_guard lock(path_map.mutex);
        handleSourceFileInDirectoryMap(path_map.map, path_map.unique_filenames, path_from);
        target_unique_filename_inserted = handleTargetFileInDirectoryMap(path_map.map, path_map.unique_filenames, path_to, replaceable);
    }

    if (target_unique_filename_inserted)
    {
        auto metric = object_storage->getMetadataStorageMetrics().unique_filenames_count;
        CurrentMetrics::add(metric, 1);
    }

    std::unique_ptr<ReadBuffer> read_buf = [&path_from, &object_storage]()
    {
        auto metadata_object_key = object_storage->generateObjectKeyForPath(path_from, std::nullopt /* key_prefix */);
        auto metadata_object = StoredObject(metadata_object_key.serialize());

        bool is_metadata = path_from.string().contains(".sql");
        ReadSettings read_settings = is_metadata ? getReadSettingsForMetadata() : getReadSettings();
        return object_storage->readObject(metadata_object, read_settings);
    }();

    std::unique_ptr<WriteBuffer> write_buf = [&path_to, &object_storage, replaceable]()
    {
        auto metadata_object_key = object_storage->generateObjectKeyForPath(path_to, std::nullopt /* key_prefix */);
        auto metadata_object = StoredObject(metadata_object_key.serialize());

        if (replaceable)
            object_storage->removeObjectIfExists(metadata_object);

        bool is_metadata = path_to.string().contains(".sql");
        WriteSettings write_settings = is_metadata ? getWriteSettingsForMetadata() : getWriteSettings();
        size_t buf_size = is_metadata ? 32768UL : DBMS_DEFAULT_BUFFER_SIZE;
        return object_storage->writeObject(
            metadata_object,
            WriteMode::Rewrite,
            /* object_attributes */ std::nullopt,
            /*buf_size*/ buf_size,
            /*settings*/ write_settings);
    }();

    chassert(read_buf);
    chassert(write_buf);

    copyData(*read_buf, *write_buf);
    write_buf->finalize();
    {
        auto metadata_object_key = object_storage->generateObjectKeyForPath(path_from, std::nullopt /* key_prefix */);
        auto metadata_object = StoredObject(metadata_object_key.serialize());
        object_storage->removeObjectIfExists(metadata_object);
    }
}
void MetadataStorageFromPlainObjectStorageTransaction::handleSourceFileInDirectoryMap(
    InMemoryDirectoryPathMap::Map & map, InMemoryDirectoryPathMap::FileNames & unique_filenames, const std::filesystem::path & path_from)
{
    /// parent_path() removes the trailing '/'.
    auto it = map.find(path_from.parent_path());
    if (it == map.end())
        throw Exception(
            ErrorCodes::FILE_DOESNT_EXIST, "Metadata object for the directory of the source path '{}' does not exist", path_from);

    auto & dir_info = it->second;

    auto unique_filename_it = unique_filenames.find(path_from.filename());
    if (unique_filename_it == unique_filenames.end())
        throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "Unique filename iterator for the  path '{}' does not exist", path_from);

    if (!dir_info.filename_iterators.contains(unique_filename_it))
        throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "Filename iterator for the source path '{}' does not exist", path_from);

    dir_info.filename_iterators.erase(unique_filename_it);
}

bool MetadataStorageFromPlainObjectStorageTransaction::handleTargetFileInDirectoryMap(
    InMemoryDirectoryPathMap::Map & map,
    InMemoryDirectoryPathMap::FileNames & unique_filenames,
    const std::filesystem::path & path_to,
    bool replaceable)
{
    /// parent_path() removes the trailing '/'.
    auto it = map.find(path_to.parent_path());

    if (it == map.end())
        throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "Metadata object for the directory of the target path '{}' does not exist", path_to);

    auto & dir_info = it->second;
    auto unique_filename_it = unique_filenames.find(path_to.filename());

    bool new_unique_filename = false;
    if (unique_filename_it == unique_filenames.end())
    {
        std::tie(unique_filename_it, new_unique_filename) = unique_filenames.emplace(path_to.filename());
        chassert(new_unique_filename);
    }

    if (dir_info.filename_iterators.contains(unique_filename_it))
    {
        if (!replaceable)
            throw Exception(ErrorCodes::FILE_ALREADY_EXISTS, "Filename iterator for the destination path '{}' already exists", path_to);
    }
    else
    {
        dir_info.filename_iterators.emplace(unique_filename_it);
    }

    return new_unique_filename;
}
}
