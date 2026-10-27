cmake_minimum_required(VERSION 3.20)
# CLI 契约表驱动测试：一个测试组内部逐条执行参数组合，断言退出码与输出通道的约定。
#
# 用例格式：名称|期望退出码|必须出现的输出文本（+ 分隔，可空）|参数...
# 约定：
#   0 = 成功：标准错误为空，标准输出含约定文本；
#   1 = 运行期错误：标准输出为空，标准错误非空；
#   2 = 参数错误：标准输出为空，标准错误含 --help 提示。
#
# 由 tests/CMakeLists.txt 以 -DGROUP=top|content|pak|exe|exe_local 调用。

# 用例按“解析器错误类别”每类保留一条代表（帮助文本、缺动作、缺必填、缺值、
# 未知选项、多余参数、业务规则、退出码 1 通道），同模式的排列组合不逐条展开。
set(top_cases
    "no_args|0|用法：eagls+尚未实现|"
    "help|0|用法：eagls+尚未实现|--help"
    "version|0|eagls ${EXPECTED_VERSION}|--version"
    "unknown|2||--unknown"
)

set(save_cases
    "save_help|0|用法：eagls+EAGLS v2|save --help"
    "save_missing|2||save"
    "save_unknown|2||save unknown"
    "save_missing_out|2||save decrypt sample.sav"
    "save_info_out|2||save info sample.sav --out a"
    "save_wrong_kind|2||save info sample.sav --kind guess"
    "save_kind_encrypt|2||save encrypt a --out b --kind global"
    "save_store_info|2||save info sample.sav --store"
    "save_missing_input|1||save info __missing_sample__.sav"
    "save_state_missing_output|2||save state sample.sav"
    "save_state_store|2||save state sample.sav --out a.json --store"
    "save_state_wrong_kind|2||save state sample.sav --out a.json --kind guess"
)

set(content_cases
    "dat_help|0|用法：eagls|dat --help"
    "dat_missing|2||dat"
    "missing_out|2||dat text sample.dat"
    "missing_value|2||gr decode sample.gr --out --overwrite"
    "unknown_option|2||gr decode sample.gr --out a --unknown"
    "extra_input|2||dat decrypt a.dat b.dat --out a"
    "gr_encode_store_on_decode|2||gr decode sample.gr --out dir --store"
)

set(pak_cases
    "pak_help|0|用法：eagls|pak --help"
    "pak_repack_missing_from|2||pak repack a.idx a.pak"
)

set(exe_cases
    "exe_help|0|用法：eagls+--typography zh-cn+SimHei/SimSun|exe --help"
    "exe_check_out_rejected|2||exe check sample.exe --out a"
    "exe_check_missing_file|1||exe check __no_such_file__.exe"
    "exe_typography_missing|2||exe patch sample.exe --typography"
    "exe_typography_unknown|2||exe patch sample.exe --typography unknown"
    "exe_typography_duplicate|2||exe check sample.exe --typography zh-cn --typography zh-cn"
)

if(GROUP STREQUAL "exe_local")
    # 真实样本补丁回归：仅在显式启用本地样本时注册；产物 SHA-256 必须与已验证版本一致。
    execute_process(COMMAND "${CLI}" exe patch "${SAMPLE}" --out "${OUTPUT}" --overwrite
        RESULT_VARIABLE actual_exit OUTPUT_VARIABLE output ERROR_VARIABLE error ENCODING UTF-8 TIMEOUT 60)
    if(NOT "${actual_exit}" STREQUAL "0")
        message(FATAL_ERROR "exe patch 失败：${actual_exit}；${output}；${error}")
    endif()
    if(NOT error STREQUAL "" OR NOT output MATCHES "已写出")
        message(FATAL_ERROR "补丁命令输出不符：${output}；${error}")
    endif()
    file(SHA256 "${OUTPUT}" actual_hash)
    if(NOT actual_hash STREQUAL "c86cceb5fdd195cad415fa22c98d5018c8935dcc6c0257d16e0f9c538e1ef4b6")
        message(FATAL_ERROR "补丁产物 SHA-256 不符：${actual_hash}")
    endif()
    # 中文排版属于同一门控测试，不增加 CTest 条目；同时覆盖 check、直出和升级。
    function(run_typography expected_exit required_text)
        execute_process(COMMAND "${CLI}" exe ${ARGN}
            RESULT_VARIABLE code OUTPUT_VARIABLE text ERROR_VARIABLE err ENCODING UTF-8 TIMEOUT 20)
        if(NOT "${code}" STREQUAL "${expected_exit}")
            message(FATAL_ERROR "中文排版命令退出码不符：${ARGN}；${code}；${text}；${err}")
        endif()
        if(expected_exit EQUAL 0)
            string(FIND "${text}" "${required_text}" position)
            if(NOT err STREQUAL "" OR position LESS 0)
                message(FATAL_ERROR "中文排版成功输出不符：${text}；${err}")
            endif()
        elseif(NOT text STREQUAL "" OR err STREQUAL "")
            message(FATAL_ERROR "中文排版失败必须只写标准错误：${text}；${err}")
        endif()
    endfunction()
    file(SHA256 "${SAMPLE}" sample_before)
    run_typography(0 "原版" check "${SAMPLE}" --typography zh-cn)
    run_typography(0 "可升级中文排版" check "${OUTPUT}" --typography zh-cn)
    run_typography(0 "已写出" patch "${SAMPLE}" --out "${OUTPUT}.layout.exe" --typography zh-cn --overwrite)
    run_typography(0 "已写出" patch "${OUTPUT}" --out "${OUTPUT}.upgrade.exe" --typography zh-cn --overwrite)
    file(SHA256 "${OUTPUT}.layout.exe" layout_hash)
    file(SHA256 "${OUTPUT}.upgrade.exe" upgrade_hash)
    if(NOT layout_hash STREQUAL "dad2d5dba3452b0e3dff1b269953adc4084575d45dab66422753ed30cf75e829"
       OR NOT layout_hash STREQUAL upgrade_hash)
        message(FATAL_ERROR "中文排版哈希或升级一致性不符：${layout_hash}；${upgrade_hash}")
    endif()
    run_typography(0 "已打补丁" check "${OUTPUT}.layout.exe" --typography zh-cn)
    file(REMOVE "${OUTPUT}.rejected.exe")
    run_typography(1 "" patch "${OUTPUT}.layout.exe" --out "${OUTPUT}.rejected.exe" --typography zh-cn)
    if(EXISTS "${OUTPUT}.rejected.exe")
        message(FATAL_ERROR "重复补丁失败后留下了输出文件")
    endif()
    file(SHA256 "${SAMPLE}" sample_after)
    if(NOT sample_before STREQUAL sample_after)
        message(FATAL_ERROR "真实 EXE 样本被修改")
    endif()

    # —— SAIMIN.exe（同一引擎的另一编译版本，5 处字节）——
    # 同一条命令自动识别版本；产物 SHA-256 必须与已验证版本一致。样本缺失时跳过。
    if(EXISTS "${SAIMIN_SAMPLE}")
        execute_process(COMMAND "${CLI}" exe check "${SAIMIN_SAMPLE}"
            RESULT_VARIABLE saimin_check_code OUTPUT_VARIABLE saimin_check_output
            ERROR_VARIABLE saimin_check_error ENCODING UTF-8 TIMEOUT 20)
        if(NOT "${saimin_check_code}" STREQUAL "0" OR NOT saimin_check_error STREQUAL "")
            message(FATAL_ERROR
                "SAIMIN exe check 失败：${saimin_check_code}；${saimin_check_output}；${saimin_check_error}")
        endif()
        if(NOT saimin_check_output MATCHES "识别版本：SAIMIN" OR NOT saimin_check_output MATCHES "原版")
            message(FATAL_ERROR "SAIMIN exe check 输出不符：${saimin_check_output}")
        endif()
        file(SHA256 "${SAIMIN_SAMPLE}" saimin_sample_before)
        execute_process(COMMAND "${CLI}" exe patch "${SAIMIN_SAMPLE}" --out "${SAIMIN_OUTPUT}" --overwrite
            RESULT_VARIABLE saimin_patch_code OUTPUT_VARIABLE saimin_patch_output
            ERROR_VARIABLE saimin_patch_error ENCODING UTF-8 TIMEOUT 60)
        if(NOT "${saimin_patch_code}" STREQUAL "0" OR NOT saimin_patch_error STREQUAL "" OR
           NOT saimin_patch_output MATCHES "已写出")
            message(FATAL_ERROR
                "SAIMIN exe patch 失败：${saimin_patch_code}；${saimin_patch_output}；${saimin_patch_error}")
        endif()
        file(SHA256 "${SAIMIN_OUTPUT}" saimin_hash)
        if(NOT saimin_hash STREQUAL "9ecd125fa90d62866563c1c3ab3ab58ec6508d1f5fd26fe5a7cf0968042b2b52")
            message(FATAL_ERROR "SAIMIN 补丁产物 SHA-256 不符：${saimin_hash}")
        endif()
        execute_process(COMMAND "${CLI}" exe check "${SAIMIN_OUTPUT}"
            RESULT_VARIABLE saimin_state_code OUTPUT_VARIABLE saimin_state_output
            ERROR_VARIABLE saimin_state_error ENCODING UTF-8 TIMEOUT 20)
        if(NOT "${saimin_state_code}" STREQUAL "0" OR NOT saimin_state_error STREQUAL "" OR
           NOT saimin_state_output MATCHES "已打补丁")
            message(FATAL_ERROR
                "SAIMIN 补丁后状态检查不符：${saimin_state_code}；${saimin_state_output}；${saimin_state_error}")
        endif()
        # 重复补丁必须拒绝且不留下输出文件；中文排版对 SAIMIN 明确报错。
        file(REMOVE "${SAIMIN_OUTPUT}.rejected.exe")
        execute_process(COMMAND "${CLI}" exe patch "${SAIMIN_OUTPUT}" --out "${SAIMIN_OUTPUT}.rejected.exe"
            RESULT_VARIABLE saimin_repeat_code OUTPUT_VARIABLE saimin_repeat_output
            ERROR_VARIABLE saimin_repeat_error ENCODING UTF-8 TIMEOUT 20)
        if(NOT "${saimin_repeat_code}" STREQUAL "1" OR NOT saimin_repeat_output STREQUAL "" OR
           saimin_repeat_error STREQUAL "")
            message(FATAL_ERROR
                "SAIMIN 重复补丁未被拒绝：${saimin_repeat_code}；${saimin_repeat_output}；${saimin_repeat_error}")
        endif()
        if(EXISTS "${SAIMIN_OUTPUT}.rejected.exe")
            message(FATAL_ERROR "SAIMIN 重复补丁失败后留下了输出文件")
        endif()
        execute_process(COMMAND "${CLI}" exe check "${SAIMIN_SAMPLE}" --typography zh-cn
            RESULT_VARIABLE saimin_typo_code OUTPUT_VARIABLE saimin_typo_output
            ERROR_VARIABLE saimin_typo_error ENCODING UTF-8 TIMEOUT 20)
        if(NOT "${saimin_typo_code}" STREQUAL "1" OR NOT saimin_typo_output STREQUAL "" OR
           saimin_typo_error STREQUAL "")
            message(FATAL_ERROR
                "SAIMIN 中文排版未被拒绝：${saimin_typo_code}；${saimin_typo_output}；${saimin_typo_error}")
        endif()
        file(SHA256 "${SAIMIN_SAMPLE}" saimin_sample_after)
        if(NOT saimin_sample_before STREQUAL saimin_sample_after)
            message(FATAL_ERROR "SAIMIN 真实样本被修改")
        endif()
    endif()
    return()
endif()

if(GROUP STREQUAL "top")
    set(cases ${top_cases})
elseif(GROUP STREQUAL "content")
    set(cases ${content_cases})
elseif(GROUP STREQUAL "pak")
    set(cases ${pak_cases})
elseif(GROUP STREQUAL "exe")
    set(cases ${exe_cases})
elseif(GROUP STREQUAL "save")
    set(cases ${save_cases})
else()
    message(FATAL_ERROR "未知测试组：${GROUP}")
endif()

foreach(case IN LISTS cases)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 name)
    list(GET fields 1 expected_exit)
    list(GET fields 2 required_text)
    list(LENGTH fields field_count)
    if(field_count GREATER 3)
        list(GET fields 3 argument_text)
        # 参数写在同一个字段里、以空格分隔，这里按空格拆成真正的参数列表。
        separate_arguments(arguments UNIX_COMMAND "${argument_text}")
    else()
        set(arguments)
    endif()

    execute_process(COMMAND "${CLI}" ${arguments}
        RESULT_VARIABLE actual_exit OUTPUT_VARIABLE output ERROR_VARIABLE error ENCODING UTF-8 TIMEOUT 10)

    if(NOT "${actual_exit}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR
            "[${name}] 退出码不符：预期 ${expected_exit}，实际 ${actual_exit}；输出：${output}；错误：${error}")
    endif()

    if(expected_exit EQUAL 0)
        if(NOT error STREQUAL "")
            message(FATAL_ERROR "[${name}] 成功执行时不应写入标准错误：${error}")
        endif()
        if(name STREQUAL "version")
            string(STRIP "${output}" actual_version)
            if(NOT actual_version STREQUAL "eagls ${EXPECTED_VERSION}")
                message(FATAL_ERROR "[version] 版本输出不符：${actual_version}")
            endif()
        elseif(NOT required_text STREQUAL "")
            string(REPLACE "+" ";" required_parts "${required_text}")
            foreach(part IN LISTS required_parts)
                string(FIND "${output}" "${part}" position)
                if(position EQUAL -1)
                    message(FATAL_ERROR "[${name}] 输出缺少“${part}”：${output}")
                endif()
            endforeach()
        endif()
    elseif(expected_exit EQUAL 1)
        if(NOT output STREQUAL "" OR error STREQUAL "")
            message(FATAL_ERROR "[${name}] 运行期错误必须只向标准错误输出信息：${output}；${error}")
        endif()
    else()
        if(NOT output STREQUAL "" OR NOT error MATCHES "--help")
            message(FATAL_ERROR "[${name}] 参数错误必须只向标准错误输出帮助提示：${output}；${error}")
        endif()
    endif()
endforeach()
