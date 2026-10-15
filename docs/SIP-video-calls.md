# SIP camera video calls

Based on working commit `cdcec9659f30560090dadea9bb1f8bfbe54ef664`.
The home screen's streaming card is replaced by **Video calls**. This flow sends
H.264 camera video with G.711 A-law audio to a SIP extension. It does not start an
RTSP server. The existing RTSP module remains available in source but has no UI
entry point. Remote video is not decoded or displayed on this board.

## Board workflow

1. Connect WiFi and save the existing SIP account in Settings.
2. Open **Voice calls** or **Video calls**, then press **Register**.
3. Wait for registration, enter the destination extension and press Call/Video call.
4. Use Hang up to end the call. Incoming calls have Answer and Decline controls.
5. To switch voice/video modes, end the call, open the other card and press
   Register again. This stops the idle service and registers one new handle.
   Active or incoming calls prevent replacing the service.

The dial fields remember their respective last extension in RAM. SIP settings
remain shared and still use UDP port 5060.

## RTC configuration and voice-only compatibility

In pinned `espressif/esp_media_protocols` 0.6.1 the video type is
`esp_rtc_video_info_t`, assigned to `esp_rtc_config_t.vcodec_info`.
The video configuration uses H.264, 640x480, 15 fps and a bounded 640 KiB frame
buffer. Audio retains the working G.711 A-law callbacks. Video is sent only after
`ESP_RTC_EVENT_VIDEO_SESSION_BEGIN`, and stops on video end, hangup or error.

Registration itself does not negotiate codecs; the SDP in INVITE/answer does.
A video-enabled configuration can establish an audio-only call when the peer and
server negotiate audio while declining video. That interoperability still needs
checking with the actual Asterisk/softphone combination. This SDK exposes no
public per-call video enable/disable setter, so this application uses explicit
initialization modes: Voice has a NULL video descriptor and callbacks; Video has
the descriptor and video callbacks. This guarantees that Voice does not offer
camera video, without depending on how a peer handles a video offer.

The video SDP advertises `packetization-mode=1` for fragmented H.264 NAL units
and `sendonly` for the camera. The exact prebuilt-library format is matched in
`rtsp_sdp_compat.c`. SIP 0.6.1 uses the `snprintf` wrapper; the separate
`media_lib_asprintf` wrapper handles RTSP. Audio SDP passes through unchanged. After any protocol library
upgrade, recheck this match against the generated SDP before relying on it.

## Build and Asterisk requirements

Use the same ESP-IDF 5.5.3 / ESP-ADF environment as the working firmware. Keep
ADF's bundled protocol copy disabled. The supplied sdkconfig now enables
`CONFIG_APP_USE_H264=y`; check this if retaining an older local sdkconfig.
The video page reports an error instead of advertising H.264 with an MJPEG
camera. **Camera encoding** controls the existing encoder; **SIP video calls**
controls the transmit frame rate (default 15 fps). Capture/encoding stays at
30 fps, GOP 1, so dropping intermediate frames does not break reference frames.

For a clean build:

```sh
idf.py fullclean
idf.py -D EXCLUDE_COMPONENTS=av_stream build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

The av_stream exclusion avoids an unused ADF example dependency. It is not the
camera path used for these calls.

Asterisk and the receiving softphone must both allow **alaw and h264**. For PJSIP,
review the relevant endpoint's codec allow list (for example `allow=alaw,h264`
after its existing `disallow=all`). Asterisk generally passes H.264 through;
do not assume video transcoding is installed. Ensure the existing RTP port range
is reachable for the additional video media stream. Keep normal authentication.

## Validation

```sh
ESP_RTC_INCLUDE_DIR=managed_components/espressif__esp_media_protocols/include \
  python3 -B -m unittest discover -s tests -v
```

Host tests cover audio/video initialization, callback gating, whole-frame copies,
capacity and cache failures, duplicate frames, a busy encoder, registration-mode
switches, and SDP direction/packetization. Hardware boundaries are mocked.

Required board checks:

- Verify voice-only calls still offer audio without an `m=video` line.
- For a video call, inspect Asterisk's SIP logger: check `m=video`, H264/90000,
  `packetization-mode=1`, `sendonly`, and a nonzero accepted video port.
- Confirm the softphone displays the local camera and both audio directions work.
- Verify hangup stops video RTP and repeated calls work without growing heap use.
- Check rejected/unanswered calls, incoming calls, a peer declining video, WiFi
  loss, and switching back to the Voice card.

A successful firmware build and host tests do not establish live RTP delivery,
remote decoding, board UI behavior or Asterisk compatibility.

## Diagnosing a call with no picture

The initial video build contained an SDP-hook error: it intercepted
`media_lib_asprintf`, while the SDK's SIP `_sip_get_sdp_data` uses `snprintf`.
The hook being present in the ELF did not establish that SIP called it. The
corrected build wraps the actual SIP call site and tests `snprintf` formatting,
including its length-counting and truncation behavior. Inspect the *outgoing*
INVITE to verify the added `fmtp` and `sendonly` attributes are really present.

The SDK expects `esp_rtc_call(handle, "101")`, not a complete
`"101@server:5060"` address: it appends the configured server. Passing a full
address produced a malformed request URI with the server repeated twice.

During a video call, the monitor now prints approximately one-second samples:

- `active`: the SDK emitted VIDEO_SESSION_BEGIN.
- `callbacks`: the SDK requested camera data.
- `supplied` / `bytes`: complete frames/bytes given to the RTP library. These
  counters do **not** establish that UDP packets reached Asterisk or Linphone.
- `busy`: the encoder held the publication mutex, so the timer callback skipped
  the frame instead of blocking.
- `no-new`: no newer published frame was available.
- `oversize` / `failed`: bounded-copy or other frame-copy failures.
- `SPS` / `PPS` / `IDR`: NAL types found in the first supplied Annex-B frame.

If Asterisk accepts a nonzero video port and the SDK emits VIDEO_SESSION_BEGIN,
video is accepted on the ESP-to-Asterisk SIP leg. Check supplied-frame counters,
then RTP delivery/forwarding and Linphone decoding; that acceptance alone does
not establish media delivery on the Asterisk-to-Linphone leg.

No `fmtp` line means H.264 packetization mode 0 by default. This can work for
peers using single-NAL packets; it does not describe our SDK's FU-A fragmentation,
which requires packetization mode 1. Missing `fmtp` alone does not prove the
cause of every black-picture call.
