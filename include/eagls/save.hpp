#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace eagls {

// Verified EAGLS v2.0 profile: UI reports EAGLS Ver 2.0; PE version is 1.5.0.0.
// Other EAGLS releases and full state-field semantics are not inferred.
enum class SaveKind { automatic, global, numbered };

struct SaveBlock {
    std::vector<std::uint8_t> compressed; // Decrypted, original LZSS token stream.
    std::vector<std::uint8_t> state;
};

struct SaveFile {
    SaveKind kind = SaveKind::automatic;
    std::array<std::uint8_t, 128> header{};
    std::vector<SaveBlock> blocks;
    std::uint8_t seed = 0;
    std::vector<std::uint8_t> trailer; // OPEN_ALWAYS can leave a stale physical tail.
};

[[nodiscard]] SaveFile decode_save(const std::vector<std::uint8_t>& bytes,
                                   SaveKind hint = SaveKind::automatic);
// Default retains original compressed streams and rejects changed state blocks.
// store=true explicitly re-encodes all state using literal-only LZSS.
[[nodiscard]] std::vector<std::uint8_t> encode_save(const SaveFile& save, bool store = false);
// Partial EAGLS v2.0 state schema: numeric symbols, string metadata (no payload),
// timestamp and execution byte cursors. Unknown state is not inferred.
[[nodiscard]] std::string describe_save_state(const SaveFile& save);
void export_save_state_file(const std::filesystem::path& input,
                            const std::filesystem::path& output,
                            SaveKind hint = SaveKind::automatic, bool overwrite = false);

[[nodiscard]] SaveFile inspect_save_file(const std::filesystem::path& input,
                                        SaveKind hint = SaveKind::automatic);
// Archive includes header.bin, seed.bin, trailer.bin, blockN.lzss, blockN.bin,
// and manifest.json. Known filenames only; no executable or game process access.
SaveFile decrypt_save_file(const std::filesystem::path& input,
                           const std::filesystem::path& output_directory,
                           SaveKind hint = SaveKind::automatic, bool overwrite = false);
std::size_t encrypt_save_archive(const std::filesystem::path& archive_directory,
                                 const std::filesystem::path& output,
                                 bool store = false, bool overwrite = false);

} // namespace eagls
