#include <eagls/save.hpp>
#include <eagls/error.hpp>

#include "support/bytes.hpp"
#include "support/msvc_rand.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace eagls {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::string_view magic = "[E]nhanced [A]dventure [G]ame [L]anguage [S]ystem E.A.G.L.S";
constexpr std::string_view pool = "1qaz2wsx3edc4rfv5tgb6yhn7ujm8ik,9ol.0p;/-@:^[]";
static_assert(pool.size() == 46);
constexpr std::size_t global_size = 0x10b008;
constexpr std::size_t numbered_size = 0x157400;
constexpr std::size_t stream_limit = 2000000;

void cipher(Bytes& bytes, std::uint8_t seed) {
    const auto signed_seed = seed < 128 ? static_cast<std::int32_t>(seed)
                                      : static_cast<std::int32_t>(seed) - 256;
    detail::MsvcRand rng(static_cast<std::uint32_t>(signed_seed));
    for (auto& b : bytes) b ^= static_cast<std::uint8_t>(pool[static_cast<std::size_t>(rng.next()) % pool.size()]);
}

Bytes inflate(const Bytes& src, std::size_t expected) {
    std::array<std::uint8_t, 4096> ring{};
    std::size_t r = 0xfee, p = 0;
    Bytes out;
    out.reserve(expected);
    const auto emit = [&](std::uint8_t c) {
        if (out.size() >= expected) throw Error(ErrorCode::invalid_content, "存档解压数据超过 EAGLS v2.0profile 长度。");
        out.push_back(c);
        ring[r] = c;
        r = (r + 1) & 4095;
    };
    while (p < src.size()) {
        const auto flags = src[p++];
        // A standalone trailing flag has no token and is not an engine stream.
        if (p == src.size()) throw Error(ErrorCode::truncated_data, "存档 LZSS 标志之后没有数据。");
        for (unsigned bit = 0; bit < 8 && p < src.size(); ++bit) {
            if ((flags & (1u << bit)) != 0) {
                emit(src[p++]);
            } else {
                if (src.size() - p < 2) throw Error(ErrorCode::truncated_data, "存档 LZSS 匹配被截断。");
                const auto lo = src[p++];
                const auto hi = src[p++];
                const std::size_t offset = lo | (static_cast<std::size_t>(hi & 0xf0) << 4);
                const std::size_t length = (hi & 15) + 3;
                for (std::size_t k = 0; k < length; ++k) emit(ring[(offset + k) & 4095]);
            }
        }
    }
    if (out.size() != expected) throw Error(ErrorCode::invalid_content, "存档解压长度不符合已验证的 EAGLS v2.0profile。");
    return out;
}

Bytes literals(const Bytes& state) {
    Bytes out;
    out.reserve(state.size() + (state.size() + 7) / 8);
    for (std::size_t p = 0; p < state.size(); p += 8) {
        const auto n = std::min<std::size_t>(8, state.size() - p);
        out.push_back(static_cast<std::uint8_t>((1u << n) - 1));
        out.insert(out.end(), state.begin() + p, state.begin() + p + n);
    }
    return out;
}

SaveFile decode_kind(const Bytes& bytes, SaveKind kind) {
    const std::size_t count = kind == SaveKind::global ? 1 : 2;
    const std::size_t header_end = 128 + count * 4;
    if (bytes.size() <= header_end) throw Error(ErrorCode::truncated_data, "存档长度字段或种子缺失。");
    std::array<std::size_t, 2> lengths{};
    std::size_t end = header_end;
    for (std::size_t i = 0; i < count; ++i) {
        lengths[i] = detail::read_u32_le(bytes.data() + 128 + i * 4);
        if (lengths[i] == 0 || lengths[i] > stream_limit)
            throw Error(ErrorCode::invalid_content, "存档压缩流长度不符合 EAGLS v2.0profile。");
        if (lengths[i] >= bytes.size() - end)
            throw Error(ErrorCode::truncated_data, "存档声明长度越过种子或文件末尾。");
        end += lengths[i];
    }
    SaveFile result;
    result.kind = kind;
    std::copy_n(bytes.begin(), 128, result.header.begin());
    result.seed = bytes[end];
    result.trailer.assign(bytes.begin() + end + 1, bytes.end());
    Bytes decrypted(bytes.begin() + header_end, bytes.begin() + end);
    cipher(decrypted, result.seed); // One RNG sequence for both blocks.
    std::size_t p = 0;
    for (std::size_t i = 0; i < count; ++i) {
        SaveBlock block;
        block.compressed.assign(decrypted.begin() + p, decrypted.begin() + p + lengths[i]);
        block.state = inflate(block.compressed, kind == SaveKind::global ? global_size : numbered_size);
        p += lengths[i];
        result.blocks.push_back(std::move(block));
    }
    return result;
}
} // namespace

SaveFile decode_save(const std::vector<std::uint8_t>& bytes, SaveKind hint) {
    if (bytes.size() < 133 || !std::equal(magic.begin(), magic.end(), bytes.begin()))
        throw Error(ErrorCode::invalid_content, "不是支持的 EAGLS 存档头。");
    if (hint == SaveKind::global || hint == SaveKind::numbered) return decode_kind(bytes, hint);
    if (hint != SaveKind::automatic) throw Error(ErrorCode::invalid_content, "未知存档种类。");
    SaveFile result;
    unsigned matches = 0;
    for (const auto kind : {SaveKind::global, SaveKind::numbered}) {
        try {
            auto candidate = decode_kind(bytes, kind);
            result = std::move(candidate);
            ++matches;
        } catch (const Error&) {
            // Automatic detection requires valid lengths AND exact expanded sizes.
        }
    }
    if (matches != 1) throw Error(ErrorCode::invalid_content,
        "无法唯一识别 EAGLS v2.0存档种类；可显式指定 --kind global 或 numbered。");
    return result;
}

std::vector<std::uint8_t> encode_save(const SaveFile& save, bool store) {
    if (save.kind != SaveKind::global && save.kind != SaveKind::numbered)
        throw Error(ErrorCode::invalid_content, "编码必须指定 global 或 numbered 存档。");
    const std::size_t count = save.kind == SaveKind::global ? 1 : 2;
    const auto expected = save.kind == SaveKind::global ? global_size : numbered_size;
    if (save.blocks.size() != count || !std::equal(magic.begin(), magic.end(), save.header.begin()))
        throw Error(ErrorCode::invalid_content, "存档头或数据块数不正确。");
    Bytes out(save.header.begin(), save.header.end()), payload;
    for (const auto& block : save.blocks) {
        if (block.state.size() != expected) throw Error(ErrorCode::invalid_content, "存档状态块长度不能改变。");
        if (!store && block.compressed.size() > stream_limit)
            throw Error(ErrorCode::resource_limit, "存档压缩流超过 profile 上限。");
        auto stream = store ? literals(block.state) : block.compressed;
        if (!store && inflate(stream, expected) != block.state)
            throw Error(ErrorCode::invalid_content, "状态块已改变；需显式使用 --store 重新编码。");
        const auto p = out.size();
        out.resize(p + 4);
        detail::write_u32_le(out.data() + p, static_cast<std::uint32_t>(stream.size()));
        payload.insert(payload.end(), stream.begin(), stream.end());
    }
    cipher(payload, save.seed);
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(save.seed);
    out.insert(out.end(), save.trailer.begin(), save.trailer.end());
    return out;
}
} // namespace eagls
