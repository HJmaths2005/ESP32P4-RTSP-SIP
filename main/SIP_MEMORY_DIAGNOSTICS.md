# SIP intermittent memory-exhausted diagnostic build

Based on the touch-working `main_touch_debug_fixed.zip` project.

## What is changed
- Heap snapshots at boot, after camera start, before/after `media_lib_add_default_adapter`, before/after `esp_rtc_service_init`, and on allocation failure.
- Lightweight ESP-IDF failed-allocation callback records size/caps/function without logging in the allocation callback.
- The next LVGL timer tick reports unexpected ESP-IDF allocation failures (not necessarily allocations from a library-specific allocator).
- Register button can retry a stalled registration **after 15 seconds** (via `esp_rtc_service_deinit()`), avoiding a physical reboot. Only one SIP handle at a time; no auto retries.
- SIP password removed from logs.

## How to test
1. Build and flash normally (`idf.py build flash monitor`).
2. Connect to WiFi and press Register once.
3. If no registration occurs, keep the monitor going for at least 20 seconds.
4. Look for `SIP_MEM` lines, especially `internal free`, `largest`, and `IDF allocator failures`. Keep the log from `before adapter` through `immediately after RTC init`.
5. After 15 seconds, press Register again to test whether the library can recover after teardown.
6. Compare a successful boot and failing boot. Do not assume memory starvation unless the allocator failure hook confirms the request or the largest block is too small.

## Optional diagnostic: prefer PSRAM for smaller generic allocations
The attached sdkconfig (outside `main/`) currently sets `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`.
Try 1024 in `idf.py menuconfig` -> Component config -> ESP PSRAM -> Maximum malloc() size to always put in internal memory, then rebuild and compare logs. This is an experiment, not a guaranteed fix, and can impact performance.

## Important
This is instrumentation and retry handling, not a proven correction of `_sip_set_domain`. If `SIP: Memory exhausted` appears while both internal and PSRAM heaps are healthy, investigate library-specific allocation and URI/domain handling or allocator corruption instead of increasing heap reserves blindly.
