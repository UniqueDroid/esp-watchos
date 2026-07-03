#pragma once

/* LVGL's default LV_ASSERT_HANDLER is `while(1);` - an intentional, permanent
 * halt with no recovery. On this device (no PSRAM, very little free heap)
 * LV_ASSERT_MALLOC fires under normal memory pressure, which was the actual
 * cause of nearly every "random freeze" investigated this session (each one
 * decoded to a backtrace ending in an LVGL draw/alloc call, never user code).
 * Trapping instead triggers ESP-IDF's panic handler, which prints
 * diagnostics and reboots (CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y) instead of
 * freezing forever. */
#ifndef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER __builtin_trap();
#endif
