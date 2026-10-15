# Touch diagnostic patch for JC8012P4A1C_I_W_Y

Scope: touch/LVGL only. No changes to SIP or camera/RTSP components.

Changes:
- `graphical_lib_init()` now returns the created LVGL display to `internship.c`, so its global `display` is no longer NULL.
- Binds the LVGL pointer input explicitly to this display.
- Keeps the existing GSL3680 GPIOs, I2C speed, 800x1280 native coordinates, and rotation flags.
- Periodically logs parsed touches (`TOUCH_DEBUG: parsed press`) and a raw GSL3680
  0x80 frame (`TOUCH_DEBUG: raw 0x80 ...`). Diagnostic I2C access runs inside
  the LVGL input callback, sequentially with the driver (no additional polling task).

How to test:
1. Replace the project's `main/` directory with this patched directory.
2. `idf.py build flash monitor` (fullclean only if stale object issues arise).
3. Touch and move a finger while watching `TOUCH_DEBUG` messages.

Interpret results:
- `parsed press` appears and coordinates change, but UI ignores taps:
  LVGL coordinate orientation or UI hit target is the likely next issue. LVGL
  already transforms native coordinates for `LV_DISPLAY_ROTATION_270`.
- `raw count > 0` but `parsed=0`: likely an issue in the mangoo1 GSL3680 driver,
  which uses a Silead software tracking algorithm.
- Raw 0x80 always has `count=0` even when touching: investigate GSL3680 firmware
  startup, power/reset, I2C or hardware.
- Raw register read fails: investigate I2C bus and address first.
- No `TOUCH_DEBUG` messages: the LVGL input callback is not running.

Important: This is a diagnostic and integration correction, not a claimed
hardware-validated fix. The existing `gsl3680 startup_chip failed read 0xb0`
warning is documented by the upstream mangoo1 driver and is not enough on its
own to prove that touch is inoperative.

When ready to disable extra logs, set `TOUCH_TRACE_ENABLED` to 0 in
`LVGL_manip.c`.
