#include <eagls/save.hpp>
#include <eagls/error.hpp>

#include "support/file_io.hpp"
#include "support/json_mini.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace eagls {
namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

Bytes read_bounded(const fs::path& path) {
    // Includes archival tails. Not an engine format field.
    if (detail::regular_file_size(path, "存档输入") > 64 * 1024 * 1024)
        throw Error(ErrorCode::resource_limit, "存档或归档文件超过工具的 64 MiB 输入上限。");
    return detail::read_binary_file(path);
}

void reject_same(const fs::path& input, const fs::path& output) {
    std::error_code ec;
    if (fs::equivalent(input, output, ec) || fs::weakly_canonical(input) == fs::weakly_canonical(output))
        throw Error(ErrorCode::output_exists, "输出不能与输入为同一文件，不允许就地改写存档或归档。");
}

std::string required_string(const detail::JsonValue& root, const char* key) {
    const auto* v = root.find(key);
    if (!v || v->type != detail::JsonValue::Type::string)
        throw Error(ErrorCode::invalid_content, "存档清单缺少字符串字段：" + std::string(key));
    return v->string;
}
} // namespace

SaveFile inspect_save_file(const fs::path& input, SaveKind hint) {
    return decode_save(read_bounded(input), hint);
}

void export_save_state_file(const fs::path& input, const fs::path& output,
                            SaveKind hint, bool overwrite) {
    reject_same(input, output);
    detail::check_target_available(output, overwrite);
    const auto report=describe_save_state(inspect_save_file(input,hint));
    detail::write_binary_file(output, Bytes(report.begin(),report.end()), overwrite);
}

SaveFile decrypt_save_file(const fs::path& input, const fs::path& output_directory,
                           SaveKind hint, bool overwrite) {
    const auto save = inspect_save_file(input, hint);
    std::vector<std::pair<std::string, Bytes>> files;
    files.push_back({"header.bin", Bytes(save.header.begin(), save.header.end())});
    files.push_back({"seed.bin", Bytes{save.seed}});
    files.push_back({"trailer.bin", save.trailer});
    for (std::size_t i = 0; i < save.blocks.size(); ++i) {
        const auto prefix = "block" + std::to_string(i);
        files.push_back({prefix + ".lzss", save.blocks[i].compressed});
        files.push_back({prefix + ".bin", save.blocks[i].state});
    }
    const std::string metadata = "{\n  \"format\": \"eagls-v2-save-v1\",\n  \"profile\": \"eagls-v2.0-pe1.5.0.0\",\n  \"kind\": \"" +
        std::string(save.kind == SaveKind::global ? "global" : "numbered") + "\"\n}\n";
    files.push_back({"manifest.json", Bytes(metadata.begin(), metadata.end())});
    // Validate every output before writing any, including hardlink aliases.
    for (const auto& file : files) {
        const auto output = output_directory / file.first;
        reject_same(input, output);
        detail::check_target_available(output, overwrite);
    }
    for (const auto& file : files) detail::write_binary_file(output_directory / file.first, file.second, overwrite);
    return save;
}

std::size_t encrypt_save_archive(const fs::path& directory, const fs::path& output,
                                 bool store, bool overwrite) {
    const auto bytes = read_bounded(directory / "manifest.json");
    const auto metadata = detail::parse_json(std::string(bytes.begin(), bytes.end()));
    if (required_string(metadata, "format") != "eagls-v2-save-v1" ||
        required_string(metadata, "profile") != "eagls-v2.0-pe1.5.0.0")
        throw Error(ErrorCode::invalid_content, "未知存档归档格式或 profile。");
    const auto kind = required_string(metadata, "kind");
    SaveFile save;
    if (kind == "global") save.kind = SaveKind::global;
    else if (kind == "numbered") save.kind = SaveKind::numbered;
    else throw Error(ErrorCode::invalid_content, "存档归档种类必须是 global 或 numbered。");
    std::vector<fs::path> inputs{directory / "manifest.json", directory / "header.bin",
                               directory / "seed.bin", directory / "trailer.bin"};
    const auto header = read_bounded(inputs[1]);
    const auto seed = read_bounded(inputs[2]);
    if (header.size() != 128 || seed.size() != 1)
        throw Error(ErrorCode::invalid_content, "归档 header.bin 必须为128字节，seed.bin 必须为1字节。");
    std::copy(header.begin(), header.end(), save.header.begin());
    save.seed = seed[0];
    save.trailer = read_bounded(inputs[3]);
    const std::size_t count = save.kind == SaveKind::global ? 1 : 2;
    for (std::size_t i = 0; i < count; ++i) {
        const auto prefix = "block" + std::to_string(i);
        inputs.push_back(directory / (prefix + ".lzss"));
        inputs.push_back(directory / (prefix + ".bin"));
        SaveBlock block;
        block.compressed = read_bounded(directory / (prefix + ".lzss"));
        block.state = read_bounded(directory / (prefix + ".bin"));
        save.blocks.push_back(std::move(block));
    }
    for (const auto& path : inputs) reject_same(path, output);
    detail::check_target_available(output, overwrite);
    const auto encoded = encode_save(save, store);
    detail::write_binary_file(output, encoded, overwrite);
    return encoded.size();
}
} // namespace eagls
