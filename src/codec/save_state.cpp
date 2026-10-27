#include <eagls/save.hpp>
#include <eagls/error.hpp>
#include "support/json_mini.hpp"

#include <sstream>
#include <string>
#include <optional>

namespace eagls {
namespace {
using Bytes = std::vector<std::uint8_t>;
std::uint32_t u32(const Bytes& b, std::size_t p) {
    return std::uint32_t(b.at(p)) | (std::uint32_t(b.at(p+1)) << 8) |
        (std::uint32_t(b.at(p+2)) << 16) | (std::uint32_t(b.at(p+3)) << 24);
}
std::string hex(std::size_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}
std::int64_t i32(const Bytes& b, std::size_t p) {
    const auto v=u32(b,p);
    return v<=0x7FFFFFFFu ? std::int64_t(v) : std::int64_t(v)-0x100000000LL;
}
void signed_array(std::ostream& out,const Bytes& b,std::size_t p,std::size_t count) {
    out << '[';
    for(std::size_t j=0;j<count;++j) {
        if(j) out << ',';
        out << i32(b,p+j*4);
    }
    out << ']';
}
// Identifiers in the verified profile are ASCII. For other byte sequences,
// preserve the identifier bytes as hex instead of guessing ANSI encoding.
std::string name_json(const Bytes& b, std::size_t p) {
    std::string name, bytes;
    bool ascii = true;
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i=0; i<64 && b.at(p+i); ++i) {
        auto v=b.at(p+i);
        name.push_back(static_cast<char>(v));
        ascii = ascii && v>=32 && v<127;
        bytes.push_back(digits[v>>4]); bytes.push_back(digits[v&15]);
    }
    if (ascii) return "\"name\":" + detail::escape_json_string(name);
    return "\"name_bytes_hex\":" + detail::escape_json_string(bytes);
}
} // namespace

std::string describe_save_state(const SaveFile& save) {
    const bool global=save.kind==SaveKind::global;
    const std::size_t expected=global ? 0x10B008 : 0x157400;
    if ((save.kind!=SaveKind::global && save.kind!=SaveKind::numbered) ||
        save.blocks.size()!=(global ? 1u : 2u))
        throw Error(ErrorCode::invalid_content,"状态描述需要明确的 EAGLS v2.0存档种类和块数。");
    for (const auto& block:save.blocks)
        if (block.state.size()!=expected)
            throw Error(ErrorCode::invalid_content,"状态描述拒绝非固定长度的 EAGLS v2.0状态块。");
    const auto& b=save.blocks[0].state;
    std::ostringstream out;
    out << "{\n\"format\":\"eagls-v2-state-report-v1\",\n"
        << "\"profile\":\"eagls-v2.0-pe1.5.0.0\",\n"
        << "\"kind\":\"" << (global ? "global" : "numbered") << "\",\n"
        << "\"partial_schema\":true,\n\"string_payloads_omitted\":true,\n"
        << "\"collection_rule\":\"CG/movie/scene unlocks use live symbol presence, not a nonzero numeric value\",\n";
    if (!global) {
        out << "\"local_symbol_lookup\":{\"numeric_max_probe_count\":" << u32(b,0x71EBC)
            << ",\"string_max_probe_count\":" << u32(b,0x79800) << "},\n";
        // Built-in colon identifiers resolve fixed engine words, not symbol
        // table slots. Retain raw values without treating them as booleans.
        struct Control { const char* builtin; std::size_t offset; };
        constexpr Control controls[] = {
            {":SkipFlg",0x157290},{":AutoFlg",0x157298},
            {":SuperSkipFlg",0x15729C},{":SceneFlg",0x1572A0},
            {":RightClickFlg",0x1572A8}
        };
        out << "\"engine_controls\":[";
        for(std::size_t i=0;i<5;++i) {
            if(i) out << ',';
            out << "{\"builtin\":\"" << controls[i].builtin << "\",\"offset\":\""
                << hex(controls[i].offset) << "\",\"raw_i32\":" << i32(b,controls[i].offset) << '}';
        }
        out << "],\n";
        out << "\"timestamp_systemtime\":[";
        for (std::size_t i=0;i<8;++i) {
            if(i) out << ',';
            out << (unsigned(b[i*2]) | (unsigned(b[i*2+1])<<8));
        }
        out << "],\n\"execution_contexts\":[";
        for (std::size_t i=0;i<5;++i) {
            const auto offset=0x12DF8C+i*0x4E94;
            if(i) out << ',';
            out << "{\"index\":" << i << ",\"record_offset\":\"" << hex(offset)
                << "\",\"enabled\":" << unsigned(b[offset])
                << ",\"current_byte_cursor\":" << u32(b,offset+4)
                << ",\"saved_byte_cursor\":" << u32(b,offset+0x1008);
            const auto depth=u32(b,offset+0x104C);
            out << ",\"call_depth_raw\":" << depth
                << ",\"call_depth_valid\":" << (depth<=16 ? "true" : "false")
                << ",\"block_depth_raw\":" << u32(b,offset+0x10D0)
                << ",\"block_flags\":";
            signed_array(out,b,offset+0x1050,16);
            out << ",\"block_jump_cursors\":";
            signed_array(out,b,offset+0x1090,16);
            out << ",\"call_frames\":[";
            // Do not interpret stale frames beyond the saved call depth, or
            // follow an invalid depth into adjacent objects.
            for(std::size_t frame=0;depth<=16 && frame<depth;++frame) {
                const auto p=offset+0x10D4+frame*0x3DC;
                if(frame) out << ',';
                out << "{\"index\":" << frame << ",\"record_offset\":\"" << hex(p)
                    << "\",\"return_byte_cursor\":" << u32(b,p)
                    << ",\"block_depth_raw\":" << u32(b,p+0x394)
                    << ",\"block_flags\":";
                signed_array(out,b,p+0x354,16);
                out << ",\"block_jump_cursors\":";
                signed_array(out,b,p+0x398,16);
                out << '}';
            }
            out << "]}";
        }
        out << "],\n";
    } else {
        out << "\"configuration_words\":[";
        for (std::size_t va=0xD4236C;va<=0xD423E8;va+=4) {
            if(va!=0xD4236C) out << ',';
            const auto offset=va-0xC821B0;
            out << "{\"offset\":\"" << hex(offset) << "\",\"raw_u32\":" << u32(b,offset) << '}';
        }
        out << "],\n\"performance_calibration\":[";
        struct Calibration { const char* name; std::size_t va; };
        constexpr Calibration fields[] = {
            {"FadeTime",0xD423EC},{"WipeTime",0xD423F0},{"DitherTime",0xD42410},
            {"Blind1Time",0xD423F4},{"Blind2Time",0xD42414},{"Curtain1Time",0xD42400},
            {"Curtain2Time",0xD42404},{"Scroll1Time",0xD423F8},{"Scroll2Time",0xD423FC},
            {"MozaicTime",0xD42408},{"FlashTime",0xD4240C}
        };
        for(std::size_t i=0;i<11;++i) {
            if(i) out << ',';
            const auto p=fields[i].va-0xC821B0;
            out << "{\"name\":\"" << fields[i].name << "\",\"offset\":\"" << hex(p)
                << "\",\"raw_i32\":" << i32(b,p) << '}';
        }
        out << "],\n";
        // EAGLS v2.0 stores one byte per numeric &message ID, independently of
        // collection-symbol presence. Unknown byte values remain distinct.
        constexpr std::size_t read_base=0xF2928, read_capacity=100000;
        std::size_t zero_count=0, read_count=0, other_count=0;
        for(std::size_t i=0;i<read_capacity;++i) {
            const auto value=b[read_base+i];
            if(value==0) ++zero_count;
            else if(value==1) ++read_count;
            else ++other_count;
        }
        out << "\"read_history\":{\"offset\":\"" << hex(read_base)
            << "\",\"capacity\":" << read_capacity
            << ",\"read_value\":1,\"zero_count\":" << zero_count
            << ",\"read_count\":" << read_count
            << ",\"other_value_count\":" << other_count << "},\n";
    }
    out << "\"symbols\":[\n";
    bool first=true;
    std::optional<std::size_t> phone_values;
    for (unsigned type=0;type<2;++type) {
        const std::size_t base=type ? (global ? 0x72374 : 0x71EC0) : (global ? 0x4CC : 0x18);
        const std::size_t count=type ? 97 : 997, stride=type ? 0x140 : 0x1D4;
        for(std::size_t i=0;i<count;++i) {
            const auto offset=base+i*stride;
            if(b[offset]!=1) continue;
            if(!first) out << ",\n";
            first=false;
            out << '{' << name_json(b,offset+1) << ",\"slot\":" << i
                << ",\"record_offset\":\"" << hex(offset)
                << "\",\"type\":\"" << (type ? "string" : "numeric") << '"';
            if(!type) {
                const std::string phone_name="_systemccs";
                bool is_phone=b[offset+1+phone_name.size()]==0;
                for(std::size_t n=0;n<phone_name.size();++n)
                    is_phone=is_phone && b[offset+1+n]==static_cast<unsigned char>(phone_name[n]);
                if(is_phone) phone_values=offset+0x44;
                out << ",\"values_offset\":\"" << hex(offset+0x44) << "\",\"values\":[";
                for(std::size_t j=0;j<100;++j) {
                    if(j) out << ',';
                    const auto word=u32(b,offset+0x44+j*4);
                    const auto signed_word=word<=0x7FFFFFFFu ? std::int64_t(word) : std::int64_t(word)-0x100000000LL;
                    out << signed_word;
                }
                out << ']';
            } else {
                std::size_t length=0;
                while(length<255 && b[offset+0x41+length]) ++length;
                out << ",\"payload_offset\":\"" << hex(offset+0x41)
                    << "\",\"payload_length\":" << length;
            }
            out << '}';
        }
    }
    out << "\n]";
    if(!global && phone_values) {
        out << ",\n\"phone_albums\":[";
        for(std::size_t group=0;group<4;++group) {
            const auto start=11+group*20;
            const auto p=*phone_values;
            const bool enabled=u32(b,p+(4+group)*4)!=0;
            const auto cursor=i32(b,p+(start-1)*4);
            const bool valid=cursor>=std::int64_t(start) && cursor<std::int64_t(start+12);
            const bool unlocked=valid && u32(b,p+std::size_t(cursor)*4)!=0;
            std::size_t count=0;
            for(std::size_t n=0;n<12;++n) count+=u32(b,p+(start+n)*4)!=0;
            if(group) out << ',';
            out << "{\"group\":" << group+1 << ",\"entry_flag_index\":" << 4+group
                << ",\"entry_enabled\":" << (enabled ? "true" : "false")
                << ",\"first_photo_flag_index\":" << start
                << ",\"photo_flag_count\":" << count
                << ",\"initial_photo_index_raw\":" << cursor
                << ",\"initial_photo_index_in_group\":" << (valid ? "true" : "false")
                << ",\"initial_photo_is_unlocked\":" << (unlocked ? "true" : "false")
                << ",\"inconsistent_entry_state\":" << (enabled && !valid ? "true" : "false") << '}';
        }
        out << ']';
    }
    out << "\n}\n";
    return out.str();
}
} // namespace eagls
