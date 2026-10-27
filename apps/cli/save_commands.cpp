#include "save_commands.hpp"
#include <eagls/save.hpp>

#include <filesystem>
#include <iostream>
#include <optional>

namespace cli {
namespace {
namespace fs = std::filesystem;
void usage() {
    std::cout << "用法：eagls save info <FILE.sav> [--kind auto|global|numbered]\n"
              << "      eagls save decrypt <FILE.sav> --out DIR [--kind auto|global|numbered] [--overwrite]\n"
              << "      eagls save encrypt <DIR> --out FILE.sav [--store] [--overwrite]\n\n"
              << "      eagls save state <FILE.sav> --out FILE.json [--kind auto|global|numbered] [--overwrite]\n\n"
              << "已验证 profile：EAGLS v2.0；PE版本1.5.0.0。\n"
              << "decrypt 导出解密LZSS和解压状态；encrypt 默认保留原压缩流、种子与旧尾部。\n"
              << "--store 显式全字面量重编码；日期/字体边缘alpha修改已在修复版实测读写。\n"
              << "state 导出部分状态结构，字符串正文省略；不允许就地覆盖输入。内部字段语义尚未完全解析。\n";
}
int invalid() {
    std::cerr << "错误：save 命令参数无效。请使用 eagls save --help。\n";
    return 2;
}
void print_info(const eagls::SaveFile& save) {
    std::cout << "存档 profile：EAGLS v2.0 / PE 1.5.0.0\n"
              << "种类：" << (save.kind == eagls::SaveKind::global ? "global" : "numbered")
              << "；种子：" << static_cast<unsigned>(save.seed)
              << "；旧尾部：" << save.trailer.size() << " 字节\n";
    for (std::size_t i = 0; i < save.blocks.size(); ++i)
        std::cout << "block" << i << "：压缩 " << save.blocks[i].compressed.size()
                  << "，解压 " << save.blocks[i].state.size() << " 字节\n";
}
int dispatch(const std::vector<std::string>& args) {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) { usage(); return 0; }
    if (args.empty() || (args[0] != "info" && args[0] != "decrypt" && args[0] != "encrypt" && args[0] != "state")) return invalid();
    const bool info = args[0] == "info", encrypt = args[0] == "encrypt";
    bool overwrite = false, store = false, have_kind = false;
    eagls::SaveKind kind = eagls::SaveKind::automatic;
    std::optional<fs::path> input, output;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--out") {
            if (output || ++i == args.size() || args[i].empty() || args[i][0] == '-') return invalid();
            output = fs::u8path(args[i]);
        } else if (args[i] == "--kind") {
            if (have_kind || ++i == args.size()) return invalid();
            have_kind = true;
            if (args[i] == "global") kind = eagls::SaveKind::global;
            else if (args[i] == "numbered") kind = eagls::SaveKind::numbered;
            else if (args[i] != "auto") return invalid();
        } else if (args[i] == "--overwrite") {
            if (overwrite) return invalid();
            overwrite = true;
        } else if (args[i] == "--store") {
            if (store) return invalid();
            store = true;
        } else {
            if (input || args[i].empty() || args[i][0] == '-') return invalid();
            input = fs::u8path(args[i]);
        }
    }
    if (!input || (info && (output || overwrite || store)) || (!info && !output) ||
        (encrypt && have_kind) || (!encrypt && store)) return invalid();
    if (info) print_info(eagls::inspect_save_file(*input, kind));
    else if (args[0]=="state") {
        eagls::export_save_state_file(*input,*output,kind,overwrite);
        std::cout << "已写出部分状态报告：" << output->u8string() << '\n';
    }
    else if (encrypt) {
        const auto n = eagls::encrypt_save_archive(*input, *output, store, overwrite);
        std::cout << "已写出：" << output->u8string() << "（" << n << "字节）\n";
    } else {
        const auto save = eagls::decrypt_save_file(*input, *output, kind, overwrite);
        print_info(save);
        std::cout << "已写出：" << output->u8string() << '\n';
    }
    return 0;
}
} // namespace
int run_save_command(const std::vector<std::string>& args) {
    try { return dispatch(args); }
    catch (const fs::filesystem_error& error) {
        std::cerr << "错误：文件系统操作失败（系统错误码 " << error.code().value() << "）。\n";
    } catch (const std::exception& error) { std::cerr << "错误：" << error.what() << '\n'; }
    return 1;
}
} // namespace cli
