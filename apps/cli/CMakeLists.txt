add_executable(eagls_cli
    main.cpp
    pak_commands.cpp
    content_commands.cpp
    text_commands.cpp
    exe_commands.cpp
    save_commands.cpp
)
set_target_properties(eagls_cli PROPERTIES OUTPUT_NAME eagls)
target_link_libraries(eagls_cli PRIVATE eagls::core)

if(WIN32)
    # CommandLineToArgvW 与 LocalFree 来自 shell32。
    target_link_libraries(eagls_cli PRIVATE shell32)
endif()

eagls_configure_target(eagls_cli)
