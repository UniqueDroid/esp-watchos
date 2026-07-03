cmake_minimum_required(VERSION 3.5)

# ESPWatchOS layout: components/system (OS core/services) and components/apps
# (independent apps) are grouped subfolders, not components themselves.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/apps"
    "${CMAKE_CURRENT_LIST_DIR}/components/system"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format -Wno-error=deprecated-variadic-comma-omission)

# See lv_assert_override.h - overrides LVGL's default LV_ASSERT_HANDLER
# (an infinite `while(1);` halt) with a trap that reboots instead. Only
# applied to C/C++ (not assembly, which can't parse a C header).
add_compile_options(
    "$<$<COMPILE_LANGUAGE:C,CXX>:-include>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:${CMAKE_CURRENT_LIST_DIR}/lv_assert_override.h>"
)

project(esp-watchos)
