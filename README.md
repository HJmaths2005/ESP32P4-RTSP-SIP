# ESP32P4 SIP voice and camera calls

The current UI provides Voice calls and Video calls. Video sends the local
H.264 camera with duplex audio; remote video display is not implemented. See
[the SIP video guide](docs/SIP-video-calls.md) for configuration and validation.

The earlier source snapshot applies review fixes to upstream commit
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
- `APP_USE_H264` selects H.264 at build time and is enabled for SIP video calls.

## Firmware environment

Use an ESP32-P4 board with the existing LCD, OV02C10 sensor, ES8311 codec and
ESP32-C6 hosted WiFi wiring. Existing pin assignments are in the source/config.
The upstream dependency lock records ESP-IDF 5.5.3. The project also requires
ESP-ADF, including its submodules, via `ADF_PATH`; ESP-IDF must be active via
`IDF_PATH`. The SIP video changes build with IDF 5.5.3 and ADF 2.8. Live calls still require
validation on the board and Asterisk server.

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
2. The Video calls card now opens a SIP dial page; it does not start RTSP.
   Keep H.264 enabled in Camera encoding for camera calls.
3. Enter a SIP server hostname/IPv4, account and password in settings.
   SIP currently uses UDP port 5060; settings last until restart.
4. Open Voice calls or Video calls, register for that mode, enter an extension,
   then call. Incoming calls have Answer/Decline controls. Audio is G.711 A-law
   at 8 kHz; the Video card additionally sends H.264 camera video.
5. After WiFi loss, reconnect and start/register the services again.

## Local regression tests

The host tests compile production SIP and camera functions with mocked hardware
boundaries. They cover registration and video state, mode switching, bounded
frame copies, and H.264 SDP. Run after the SDK dependencies have been installed:

```sh
ESP_RTC_INCLUDE_DIR=managed_components/espressif__esp_media_protocols/include \
  python3 -B -m unittest discover -s tests -v
```

They do not validate live RTP, remote decoding, sensor hardware or UI touch input.
See [the SIP video guide](docs/SIP-video-calls.md) for acceptance checks.

## Required board acceptance checks

- Build and flash with the configured SDK and confirm normal camera startup.
- Play MJPEG for an extended session, reconnect WiFi and restart streaming.
- Test the H.264 configuration separately; check decoded image/colour and memory use.
- Make and receive SIP calls, verify both audio directions and retry after server errors.
- Exercise camera teardown/reinitialization after stopping RTSP and monitor free heap.

The removed credentials may still exist in the public upstream Git history.
If they were real, rotate them. This snapshot does not rewrite GitHub history
or change the remote repository.

## Legacy H.264 RTSP streaming configuration

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

## Earlier RTSP device interface

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
