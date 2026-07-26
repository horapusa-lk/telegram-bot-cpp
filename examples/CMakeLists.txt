# Each example is a single main.cpp linking the library exactly like a
# downstream consumer would.

function(tgbot_add_example name)
    add_executable(${name} ${name}/main.cpp)
    target_link_libraries(${name} PRIVATE tgbot::tgbot)
    target_compile_options(${name} PRIVATE ${TGBOT_WARNINGS})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    endif()
endfunction()

tgbot_add_example(echo_polling)
tgbot_add_example(echo_webhook)
tgbot_add_example(inline_keyboard_menu)
tgbot_add_example(file_sender)
tgbot_add_example(inline_query_bot)
