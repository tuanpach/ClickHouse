#include <Interpreters/Context.h>
#include <ICommand.h>

#include <IO/ReadBufferFromEmptyFile.h>
#include <IO/ReadBufferFromFile.h>
#include <IO/WriteBufferFromFile.h>
#include <IO/copyData.h>
#include <Common/TerminalSize.h>
#include <Common/logger_useful.h>
#include <Disks/WriteMode.h>

namespace DB
{

namespace ErrorCodes
{
extern const int BAD_ARGUMENTS;
}

class CommandWrite final : public ICommand
{
public:
    CommandWrite() : ICommand("CommandWrite")
    {
        command_name = "write";
        description = "Write a file from `path-from` to `path-to`";
        options_description.add_options()(
            "path-from",
            po::value<String>(),
            "file from which we are reading, defaults to `stdin` (input from `stdin` is finished by Ctrl+D)")(
            "path-to", po::value<String>(), "file to which we are writing (mandatory, positional)")(
            "mode", po::value<String>(), "write mode: `rewrite` (default) or `append`");
        positional_options_description.add("path-to", 1);
    }


    void executeImpl(const CommandLineOptions & options, DisksClient & client) override
    {
        auto disk = client.getCurrentDiskWithPath();

        std::optional<String> path_from = getValueFromCommandLineOptionsWithOptional<String>(options, "path-from");

        String path_to = disk.getRelativeFromRoot(getValueFromCommandLineOptionsThrow<String>(options, "path-to"));
        std::optional<String> write_mode_param = getValueFromCommandLineOptionsWithOptional<String>(options, "mode");
        WriteMode write_mode = WriteMode::Rewrite;
        if (write_mode_param.has_value())
        {
            if (*write_mode_param == "rewrite")
                write_mode = WriteMode::Rewrite;
            else if (*write_mode_param == "append")
                write_mode = WriteMode::Append;
            else
                throw Exception(
                    ErrorCodes::BAD_ARGUMENTS, "invalid `mode`, expected `rewrite` or `append`, actual '{}'", *write_mode_param);
        }

        auto in = [&]() -> std::unique_ptr<ReadBufferFromFileBase>
        {
            if (!path_from.has_value())
                return std::make_unique<ReadBufferFromFileDescriptor>(STDIN_FILENO);

            String relative_path_from = disk.getRelativeFromRoot(path_from.value());
            auto res = disk.getDisk()->readFileIfExists(relative_path_from, getReadSettings());
            if (res)
                return res;
            /// For backward compatibility.
            return std::make_unique<ReadBufferFromEmptyFile>();
        }();

        LOG_INFO(
            log,
            "Writing file from '{}' to '{}' with mode '{}' at disk '{}'",
            path_from.value_or("stdin"),
            path_to,
            write_mode,
            disk.getDisk()->getName());
        auto out = disk.getDisk()->writeFile(path_to, DBMS_DEFAULT_BUFFER_SIZE, write_mode);
        copyData(*in, *out);
        out->finalize();
    }
};

CommandPtr makeCommandWrite()
{
    return std::make_shared<DB::CommandWrite>();
}

}
