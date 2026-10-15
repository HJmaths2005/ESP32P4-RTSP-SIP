# ESP32P4 RTSP / SIP — review fixes

This source snapshot applies review fixes to upstream commit
`e2a467a785d67f5320fec7b1fb97c4c389432bb1`.
It is source code, not a built or hardware-validated firmware image.

## Changes

- Three CSI input buffers have explicit capture/ready/processing ownership. DMA
  cannot reuse a frame while the encoder is reading it. Excess frames are dropped.
- Encoded output is written back before publication and reader caches are refreshed
  under a shared mutex. Failed encodes do not publish stale lengths.
- WiFi changes are serialized through the event loop and wait for disconnect
  acknowledgement. Explicit cancellation, retries, lost IP and driver errors are handled.
- Complete 32-byte SSIDs and valid WPA password lengths are accepted. Source
  credentials were removed; settings use RAM.
- Audio TX and RX use separate callback buffers and handle positive codec errors.
  Large received packets are processed completely. Audio setup rolls back on failure.
- Camera teardown stops DMA, waits for encoding and frees allocated resources.
  The publication mutex is retained once for the application lifetime so RTSP
  readers cannot use a deleted handle. Stop RTSP before changing camera configuration.
- JPEG copies fail with the required size instead of returning truncated images.
- RTSP advertises the actual 640 x 480 dimensions, uses the selected codec and
  returns real stop errors. The UI only enters streaming after successful start.
- SIP settings, registration, dialing, answering and hanging up are available.
  Network callbacks update atomic state; LVGL refreshes on its own timer.
- `APP_USE_H264` selects H.264 at build time; MJPEG remains the default.

## Firmware environment

Use an ESP32-P4 board with the existing LCD, OV02C10 sensor, ES8311 codec and
ESP32-C6 hosted WiFi wiring. Existing pin assignments are in the source/config.
The upstream dependency lock records ESP-IDF 5.5.3. The project also requires
ESP-ADF, including its submodules, via `ADF_PATH`; ESP-IDF must be active via
`IDF_PATH`. API checks used IDF 5.5.3 and ADF 2.8 headers, but this snapshot has
not been built against a complete installed SDK.

The inherited `dependencies.lock` contains the author's absolute local
`esp_codec_dev` path. Back up and remove that lock on a new machine, then let
the component manager regenerate it against your configured ADF installation.
Keep the supplied `sdkconfig` for the existing board and review it in menuconfig.

From an activated ESP-IDF/ADF shell in the project directory:

```text
idf.py reconfigure
idf.py menuconfig
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

The target in the supplied configuration is `esp32p4`. For H.264 select
Camera streaming > Use the hardware H.264 encoder instead of MJPEG in
menuconfig, then rebuild.

## Device workflow

1. Connect WiFi from the WiFi page. Empty passwords mean an open network.
2. Start video from the main page. The address is
   `rtsp://DEVICE_IP:8554/esp32p4cam`.
   For lower playback latency, use:
   ```sh
   ffplay -rtsp_transport tcp -fflags nobuffer -flags low_delay -framedrop -probesize 32768 -analyzeduration 0 rtsp://DEVICE_IP:8554/esp32p4cam
   ```
   MJPEG quality defaults to 60/100 (previously 20). Adjust Camera streaming >
   MJPEG image quality in menuconfig. Higher quality uses more network bandwidth;
   reduce it if playback cannot keep up. Rebuild and flash for changes to apply.
3. Enter a SIP server hostname/IPv4, account and password in settings.
   SIP currently uses UDP port 5060; settings last until restart.
4. Open voice call, register, enter an extension, then call. Answer and hang up
   are available on the same page. Audio is G.711 A-law at 8 kHz.
5. After WiFi loss, reconnect and start/register the services again.

## Local regression tests

Python 3 and Clang/GCC or Zig are sufficient; ESP-IDF is not needed for these tests.

```text
python tests/run_host_tests.py --cc clang
python tests/run_host_tests.py --cc /path/to/zig.exe
```

The tests compile current production C function bodies with fake device/RTOS
boundaries. They exercise 100,000 CSI schedules, overlapping audio callbacks,
codec read/write failures, JPEG capacity, RTSP stop propagation, SIP state,
WiFi changes/cancellation, 32-byte SSIDs, IP loss and retry limits.
Both test executables passed with Zig 0.16.0; `git diff --check` also passed.
They do not validate DMA/cache hardware effects, firmware linkage, UI layout,
actual SIP interoperability or sensor register behavior.

## Required board acceptance checks

- Build and flash with the configured SDK and confirm normal camera startup.
- Play MJPEG for an extended session, reconnect WiFi and restart streaming.
- Test the H.264 configuration separately; check decoded image/colour and memory use.
- Make and receive SIP calls, verify both audio directions and retry after server errors.
- Exercise camera teardown/reinitialization after stopping RTSP and monitor free heap.

The removed credentials may still exist in the public upstream Git history.
If they were real, rotate them. This snapshot does not rewrite GitHub history
or change the remote repository.

## H.264 streaming configuration

The active firmware uses hardware H.264 at 640 x 480 / 30 fps, with a
1,500,000 bit/s target and GOP 1 (independent frames). The I/P experiment with
GOP 15 and encoded-frame backpressure did not pass the network performance
acceptance check and was rolled back. Its source, config, binary and delivery
tests are preserved in `backups/h264-ip-experiment/`; the previously working
firmware is in `backups/h264-working/`. Do not claim a performance improvement
from that experiment without repeating hardware and network measurements.

ADF's prebuilt RTSP server uses FU-A but omits the H.264 fmtp attribute.
`main/rtsp_sdp_compat.c` wraps media_lib_asprintf at link time, matches the exact
H.264 media template and adds `a=fmtp:96 packetization-mode=1`. Other formats
pass through unchanged; allocations use the library allocator and the returned
SDP length includes the added line. After upgrading ADF, inspect DESCRIBE again:
the compatibility match is intentionally exact. SPS/PPS are sent in-band.

## Device interface

The landscape interface uses a consistent dark theme, larger touch targets and
separate WiFi, video, calls and settings cards. The home page shows live network
status and disables starting video until an IP is available. The streaming page
shows the full RTSP address and a clear stop action. Network/account forms use
labelled fields, masked passwords and a landscape keyboard positioned below
fields and actions. Text remains English with the bundled Montserrat fonts.
The UI-only change preserves the working camera/H.264/RTSP configuration.
The previous UI sources and application binary are in `backups/ui-before/`.

The display now renders widgets directly on a 1280x800 logical canvas and rotates
RGB888 strips at LCD flush time. LVGL rotates raw touch coordinates to the same
canvas. A single render buffer keeps the rotated DMA source valid until transfer
completion. This avoids per-widget transformed layers. Text fields open the keyboard on pointer taps as well as focus events, including
when reopening an already focused field after Done.
