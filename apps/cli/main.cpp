// EAGLS 命令行入口：参数获取、顶层分发与帮助输出。
//
// Windows 下参数必须经 CommandLineToArgvW 获取宽字符命令行再统一转成 UTF-8：
// MinGW 与 MSVC 交给 main 的 argv 编码互不相同（UTF-8 与本地代码页），
// 不能直接使用。控制台输出设为 UTF-8 代码页后按 UTF-8 字节写出；
// 重定向到文件或管道时字节流不受影响。
//
// 本文件面向 Windows（项目目标平台），依赖 shellapi.h。

#include "pak_commands.hpp"
#include "content_commands.hpp"
#include "text_commands.hpp"
#include "exe_commands.hpp"
#include "save_commands.hpp"

#include <eagls/version.hpp>

#include <cstddef>
#include <cwchar>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>

namespace {

// 顶层帮助。tests/cli_contract_test.cmake 依赖其中必须出现的「用法：eagls」与「尚未实现」字样。
void print_help() {
    std::cout
        << "EAGLS Engine Tool（重启开发中）\n\n"
        << "用法：eagls <命令> [参数]\n\n"
        << "命令：\n"
        << "  eagls save info <FILE.sav>               检查 EAGLS v2.0存档\n"
        << "  eagls save state <FILE.sav> --out FILE.json  导出变量表与部分内部状态\n"
        << "  eagls save decrypt <FILE.sav> --out DIR   解密/解压存档归档\n"
        << "  eagls save encrypt <DIR> --out FILE.sav   无损重建存档；save --help 查看选项\n"
        << "  eagls pak info    <idx> <pak>             显示归档信息\n"
        << "  eagls pak list    <idx> <pak>             列出全部条目\n"
        << "  eagls pak extract <idx> <pak> [选项]      提取条目\n"
        << "  eagls pak repack  <idx> <pak> --from DIR [--out DIR]  用替换目录重建归档\n"
        << "  eagls pak --help                          显示 pak 子命令帮助\n"
        << "  eagls dat decrypt <文件或目录> --out DIR  导出解密 DAT\n"
        << "  eagls dat text    <文件或目录> --out DIR  导出原编码正文\n"
        << "  eagls gr decode   <文件或目录> --out DIR  导出标准 BMP\n"
        << "  eagls gr encode   <文件或目录> --out DIR [--store]  把 BMP 编码为 GR\n"
        << "  eagls text extract <文件或目录> --out FILE.json  提取文本清单\n"
        << "  eagls text apply   <文件或目录> --table FILE.json --out DIR  回填重建 DAT\n"
        << "  eagls exe check   <EAGLS.exe>             检查 GBK 中文补丁位置与状态\n"
        << "  eagls exe patch   <EAGLS.exe> [--out FILE] [--overwrite]  写出补丁副本\n"
        << "  eagls dat --help / eagls gr --help / eagls text --help / eagls exe --help  显示子命令帮助\n"
        << "  eagls --help                              显示本帮助\n"
        << "  eagls --version                           显示核心版本\n\n"
        << "pak extract 选项：\n"
        << "  --out DIR     输出目录（默认：<pak 所在目录>/<pak 主文件名>_out）\n"
        << "  --entry NAME  只提取指定条目，可重复；NAME 可带目录前缀，\n"
        << "                比较时只使用最后一个路径分隔符之后的部分\n"
        << "  --overwrite   允许覆盖输出目录中已存在的同名普通文件\n\n"
        << "pak repack 选项：\n"
        << "  --from DIR    替换文件目录（必填）；目录中每个文件都必须匹配到一个条目\n"
        << "  --out DIR     输出目录（默认：<pak 所在目录>/<pak 主文件名>_repack）\n"
        << "  --overwrite   允许覆盖输出目录中已存在的同名普通文件\n\n"
        << "目前已实现 IDX/PAK 归档的信息查看、列目录、原样提取与替换回包；\n"
        << "已实现 DAT 解密/正文导出、GR 解码与编码、文本清单的提取与回填，以及 EAGLS.exe 中文显示补丁；\n"
        << "全量翻译工作流与 GUI 尚未实现。\n";
}

// 宽字符串转 UTF-8。
std::string wide_to_utf8(const wchar_t* text) {
    const int length = static_cast<int>(::wcslen(text));
    if (length == 0) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

// 取 UTF-8 形式的命令行参数（不含程序名）。
std::vector<std::string> command_line_arguments() {
    int count = 0;
    wchar_t** wide_arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    std::vector<std::string> result;
    if (wide_arguments == nullptr) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(count > 0 ? count - 1 : 0));
    for (int i = 1; i < count; ++i) {
        result.push_back(wide_to_utf8(wide_arguments[i]));
    }
    ::LocalFree(wide_arguments);
    return result;
}

// 让真实控制台按 UTF-8 解释程序输出；重定向到文件或管道时不影响字节流。
void prepare_console() {
    ::SetConsoleOutputCP(CP_UTF8);
}

}  // namespace

int main() {
    prepare_console();
    const std::vector<std::string> arguments = command_line_arguments();

    if (arguments.empty()) {
        print_help();
        return 0;
    }

    if (arguments.size() == 1) {
        const std::string_view first{arguments[0]};
        if (first == "--help" || first == "-h") {
            print_help();
            return 0;
        }
        if (first == "--version") {
            std::cout << "eagls " << eagls::version() << '\n';
            return 0;
        }
    }

    if (arguments[0] == "pak") {
        const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
        return cli::run_pak_command(rest);
    }

    if (arguments[0] == "dat" || arguments[0] == "gr") {
        const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
        return cli::run_content_command(arguments[0], rest);
    }

    if (arguments[0] == "text") {
        const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
        return cli::run_text_command(rest);
    }

    if (arguments[0] == "exe") {
        const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
        return cli::run_exe_command(rest);
    }

    if (arguments[0] == "save") {
        const std::vector<std::string> rest(arguments.begin() + 1, arguments.end());
        return cli::run_save_command(rest);
    }

    // 未实现的命令与非法参数明确失败，不返回伪成功。
    std::cerr << "错误：参数无效或命令尚未实现。请使用 eagls --help。\n";
    return 2;
}
