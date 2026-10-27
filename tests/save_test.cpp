#include <eagls/save.hpp>
#include <eagls/error.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using Bytes = std::vector<std::uint8_t>;
namespace fs = std::filesystem;
Bytes read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("sample not readable");
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F f) {
    try { f(); } catch (const eagls::Error&) { return; }
    throw std::runtime_error("invalid input was accepted");
}
void check(const fs::path& folder, eagls::SaveKind kind) {
    const auto raw = read(folder / "example.sav");
    const auto save = eagls::decode_save(raw);
    require(save.kind == kind, "auto kind detection");
    require(save.blocks.size() == (kind == eagls::SaveKind::global ? 1u : 2u), "block count");
    require(save.seed == (kind == eagls::SaveKind::global ? 83 : 195), "seed position");
    require(save.seed != raw.back() && !save.trailer.empty(), "stale-tail fixture");
    require(eagls::encode_save(save) == raw, "byte-identical reconstruction");
    require(eagls::decode_save(raw,kind).blocks[0].state == save.blocks[0].state, "explicit kind");
    for (std::size_t i=0;i<save.blocks.size();++i) {
        const auto prefix = "block" + std::to_string(i);
        require(save.blocks[i].state == read(folder/"decrypted"/(prefix+".bin")), "independent expanded oracle");
        require(save.blocks[i].compressed == read(folder/"decrypted"/(prefix+".lzss")), "independent cipher oracle");
    }
    if (kind == eagls::SaveKind::numbered) {
        // Independently obtained with Windows CRT srand(uint32(-61))/rand.
        constexpr std::uint8_t expected[8]={0x2f,0x36,0x6f,0x6a,0x32,0x39,0x6a,0x6d};
        for(std::size_t i=0;i<8;++i)
            require((raw[136+i]^save.blocks[0].compressed[i])==expected[i], "signed seed CRT vector");
    }
    auto edited = save;
    edited.blocks[0].state[123] ^= 1;
    rejects([&]{(void)eagls::encode_save(edited);});
    const auto stored = eagls::encode_save(edited,true);
    const auto decoded = eagls::decode_save(stored);
    require(decoded.blocks[0].state == edited.blocks[0].state, "explicit edited-state encoding");
    require(decoded.trailer == save.trailer, "store keeps stale tail");
    rejects([&]{(void)eagls::decode_save(raw, kind == eagls::SaveKind::global ? eagls::SaveKind::numbered : eagls::SaveKind::global);});
    auto bad = raw; bad[0] ^= 1;
    rejects([&]{(void)eagls::decode_save(bad);});
    bad = raw;
    std::fill(bad.begin()+128,bad.begin()+132,255);
    rejects([&]{(void)eagls::decode_save(bad);});
    bad = raw;
    std::size_t seed_offset = 128 + 4 * save.blocks.size();
    for (const auto& block : save.blocks) seed_offset += block.compressed.size();
    bad[seed_offset] ^= 1;
    rejects([&]{(void)eagls::decode_save(bad,kind);});
    rejects([&]{(void)eagls::decode_save(Bytes(raw.begin(),raw.begin()+132));});
    edited=save;edited.blocks[0].compressed={0};
    rejects([&]{(void)eagls::encode_save(edited);});
    edited=save;edited.blocks[0].compressed={0,0};
    rejects([&]{(void)eagls::encode_save(edited);});
    edited=save;edited.blocks[0].state.pop_back();
    rejects([&]{(void)eagls::encode_save(edited,true);});
}

void check_state_report() {
    eagls::SaveFile save;
    save.kind=eagls::SaveKind::numbered;
    save.blocks.resize(2);
    for(auto& b:save.blocks) b.state.resize(0x157400);
    auto& b=save.blocks[0].state;
    // A live zero-valued collection marker must be retained, while an unused
    // slot containing stale bytes must not be exposed as a live variable.
    b[0x18]=1;
    const std::string name="_test";
    std::copy(name.begin(),name.end(),b.begin()+0x19);
    b[0x18+0x44]=0xFD;
    std::fill(b.begin()+0x18+0x45,b.begin()+0x18+0x48,0xFF);
    const auto marker=0x18+13*0x1D4;
    b[marker]=1;
    const std::string marker_name="@reco_test";
    std::copy(marker_name.begin(),marker_name.end(),b.begin()+marker+1);
    const std::string stale_name="@stale_not_live";
    std::copy(stale_name.begin(),stale_name.end(),b.begin()+marker+0x1D4+1);
    const auto phone=0x18+21*0x1D4;
    b[phone]=1;
    const std::string phone_name="_systemccs";
    std::copy(phone_name.begin(),phone_name.end(),b.begin()+phone+1);
    b[phone+0x44+5*4]=1; // enabled album with a missing initial index
    b[phone+0x44+31*4]=1; // photo flag alone cannot make that entry consistent
    const auto string_base=0x71EC0u;
    b[string_base]=1;
    const std::string string_name="_payload";
    std::copy(string_name.begin(),string_name.end(),b.begin()+string_base+1);
    const std::string secret="DO_NOT_EXPORT_PAYLOAD";
    std::copy(secret.begin(),secret.end(),b.begin()+string_base+0x41);
    const auto ctx=0x12DF8Cu;
    b[ctx+0x104C]=1;
    b[ctx+0x10D4]=123;
    b[0x157290]=2; // report raw engine state rather than coercing to true
    std::fill(b.begin()+0x157298,b.begin()+0x15729C,0xFF);
    std::fill(b.begin()+ctx+0x1090,b.begin()+ctx+0x1094,0xFF);
    // Corrupt call depth is reported, never used as an unbounded frame count.
    std::fill(b.begin()+ctx+0x4E94+0x104C,b.begin()+ctx+0x4E94+0x1050,0xFF);
    const auto report=eagls::describe_save_state(save);
    require(report.find("\"name\":\"_test\"")!=std::string::npos,"numeric symbol reported");
    require(report.find("\"values\":[-3,0,")!=std::string::npos,"signed numeric value");
    require(report.find("\"name\":\"@reco_test\"")!=std::string::npos,"zero-valued live unlock marker retained");
    require(report.find(stale_name)==std::string::npos,"unregistered stale symbol omitted");
    require(report.find("\"name\":\"_payload\"")!=std::string::npos,"string identifier reported");
    require(report.find(secret)==std::string::npos,"string payload must be omitted");
    require(report.find("\"partial_schema\":true")!=std::string::npos,"partial schema explicit");
    require(report.find("\"return_byte_cursor\":123")!=std::string::npos,"saved call frame exported");
    require(report.find("\"block_jump_cursors\":[-1,0,")!=std::string::npos,"signed jump sentinel retained");
    require(report.find("\"call_depth_raw\":4294967295,\"call_depth_valid\":false")!=std::string::npos,"invalid depth bounded");
    require(report.find("\"phone_albums\":[")!=std::string::npos,"phone fields resolved by symbol name");
    require(report.find("\"builtin\":\":SkipFlg\",\"offset\":\"0x157290\",\"raw_i32\":2")!=std::string::npos,"fixed built-in skip word remains raw");
    require(report.find("\"builtin\":\":AutoFlg\",\"offset\":\"0x157298\",\"raw_i32\":-1")!=std::string::npos,"built-in auto word remains signed");
    require(report.find("\"read_history\"")==std::string::npos,"global read table is not interpreted in numbered state");
    require(report.find("\"inconsistent_entry_state\":true")!=std::string::npos,"enabled album with invalid cursor identified");
    save.blocks[0].state.pop_back();
    rejects([&]{(void)eagls::describe_save_state(save);});
}

void check_read_history_report() {
    eagls::SaveFile save;
    save.kind=eagls::SaveKind::global;
    save.blocks.resize(1);
    auto& b=save.blocks[0].state;
    b.resize(0x10B008);
    constexpr std::size_t start=0xF2928, count=100000;
    // Include both boundaries, adjacent unrelated bytes, and non-boolean
    // values: the engine checks exactly 1, not arbitrary nonzero values.
    b[start-1]=1; b[start+count]=1;
    b[start]=1; b[start+count-1]=1;
    b[start+1]=2; b[start+count-2]=255;
    const auto before=b;
    const auto report=eagls::describe_save_state(save);
    require(report.find("\"read_history\":{\"offset\":\"0xf2928\",\"capacity\":100000,\"read_value\":1,\"zero_count\":99996,\"read_count\":2,\"other_value_count\":2}")!=std::string::npos,
            "read-history extent and exact read value");
    require(b==before,"read-history reporting must not mutate state");
    require(report.find("\"engine_controls\"")==std::string::npos,"numbered control offsets are not interpreted in global state");
}
} // namespace

int main(int argc,char**argv) {
    try {
        const auto root=fs::u8path(EAGLS_SAVE_FIXTURE_DIR);
        check(root/"global",eagls::SaveKind::global);
        check(root/"numbered",eagls::SaveKind::numbered);
        check_state_report();
        check_read_history_report();
        // Real samples: only opt-in, read-only, never automatically scan a game.
        if(argc==2) {
            std::size_t count=0;
            for(const auto& entry:fs::directory_iterator(fs::u8path(argv[1]))) {
                if(!entry.is_regular_file() || entry.path().extension()!=L".sav") continue;
                const auto raw=read(entry.path());
                const auto save=eagls::decode_save(raw);
                require(eagls::encode_save(save)==raw,"real sample exact reconstruction");
                ++count;
            }
            require(count>0,"local directory contained no .sav samples");
            std::cout<<"local save roundtrips: "<<count<<'\n';
        }
        std::cout<<"save codec tests passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
