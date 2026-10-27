add_library(eagls_core STATIC
    version.cpp
    archive/archive.cpp
    archive/entry_name.cpp
    archive/index_cipher.cpp
    archive/repack.cpp
    support/utf8.cpp
    support/file_io.cpp
    support/codepage.cpp
    support/json_mini.cpp
    codec/content.cpp
    codec/save.cpp
    codec/save_state.cpp
    codec/gr_encode.cpp
    codec/text_map.cpp
    codec/pe_image.cpp
    codec/exe_patch.cpp
    codec/exe_typography.cpp
    operations/content_file.cpp
    operations/save_file.cpp
    operations/text_workflow.cpp
    operations/exe_patch_file.cpp
)
add_library(eagls::core ALIAS eagls_core)

# 前端只接触公开头文件；src/ 下的私有头（archive/、support/）和生成配置不向外暴露。
target_include_directories(eagls_core
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_BINARY_DIR}/generated"
)
target_compile_features(eagls_core PUBLIC cxx_std_17)
eagls_configure_target(eagls_core)
