#pragma once

#include <stdexcept>
#include <string>

namespace eagls {

// 核心操作失败的分类。前端可据此选择提示方式，具体原因见 Error::what()。
enum class ErrorCode {
    io,                  // 文件不存在、无法打开、读写失败、无法创建目录等
    invalid_index,       // 索引文件大小或整体内容不符合已验证的格式
    invalid_entry,       // 槽位记录非法，例如文件名不是合法 UTF-8 或含控制字符
    duplicate_name,      // 索引中存在（忽略 ASCII 大小写后）重名的条目
    entry_out_of_range,  // 条目的偏移或长度超出数据文件
    entry_not_found,     // 请求的条目序号不存在
    unsafe_name,         // 条目名不能安全地作为输出目录下的单个文件名
    output_exists,       // 输出位置已有内容且未允许覆盖
    truncated_data,      // 读取内容不足以完成解析或解压
    invalid_content,     // DAT/GR/存档内容不符合当前支持的格式
    resource_limit,      // 图像尺寸或文件大小超过工具的资源上限
};

// 核心库抛出的唯一异常类型。核心不打印日志、不弹窗、不终止进程，
// 错误统一交给调用方（CLI、GUI）决定如何呈现。what() 为 UTF-8 中文说明。
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

}  // namespace eagls
