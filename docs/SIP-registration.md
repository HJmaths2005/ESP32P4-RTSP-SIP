# SIP registration

This application uses `espressif/esp_media_protocols` 0.6.1 from the component
registry for both SIP and RTSP. Disable **ADF Library Configuration → Enable
Media Protocol Library** (`CONFIG_MEDIA_PROTOCOL_LIB_ENABLE`) so that ADF does
not also export its bundled protocol headers and library. The checked-in
`sdkconfig` has this disabled; the main component rejects an incompatible setting.

The ADF v2.8 bundled `esp_rtc_config_t` places `p2p_mode` at offset 64 on
ESP32-P4, while the registry 0.6.1 configuration places it at offset 72.
Mixing a header from one provider with the other provider's binary can make
`.p2p_mode = false` ineffective. The observed `sip_p2p_register` log is a strong
indicator of this problem. A clean rebuild is necessary after changing providers:

```sh
idf.py fullclean
idf.py build
```

If ADF v2.8's unused `av_stream` example fails dependency resolution for
`usb_stream ~1.5.1`, use `idf.py -D EXCLUDE_COMPONENTS=av_stream build`.
The application does not call the example's API.

In Settings, enter the Asterisk server address, account, and password. Connect
WiFi, then press Register on the voice page. The URI passed to the SDK is
`udp://ACCOUNT:PASSWORD@SERVER:5060`; avoid logging the password.

`ESP_RTC_EVENT_REGISTERED` must leave the account registered. An error permits
another press of Register to stop the previous service and retry. Dialing stays
disabled until registration is reported by the SDK.

For PJSIP, run `pjsip set logger on` in the laptop's Asterisk console during one
attempt. A typical authenticated registration is REGISTER → 401 challenge →
REGISTER with authentication → 200 OK. A first 401 is expected. If no REGISTER
arrives, check the firmware's selected protocol library, server address, UDP
transport, laptop firewall, and network reachability. Redact credentials and
Authorization headers before sharing logs.

The host regression check compiles the production SIP event-state handler:

```sh
ESP_RTC_INCLUDE_DIR=managed_components/espressif__esp_media_protocols/include \
  python3 -B -m unittest discover -s tests -v
```

This checks registration and call-state transitions; it does not contact
Asterisk or exercise the board's audio and camera hardware.
