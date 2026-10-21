# Fazbear Relay Animator — GitHub starter pack

This pack adds a separate Android relay editor and Stage ESP32 firmware alongside the existing Fazbear Control app.

## Important status
This is a starter implementation, not a verified production release. It has not been compiled on your hardware. Test with relay loads disconnected first.

## Contents
- `StageESP32/StageESP32.ino` — HTTP API, safe boot output states, manual relay test, event-sequence storage in LittleFS.
- `FazbearRelayAnimator/` — separate Android app (Java, Gradle) with Stage IP/token settings, relay toggles, and simple timeline/step sequence editor.
- `.github/workflows/relay-android-build.yml` — build workflow for the separate app.

## Stage hardware safety
- GPIO assignments default to 16,17,18,19,21,22,23,25.
- Set `RELAY_ACTIVE_LOW` after checking the relay board's trigger polarity. Default is `false` (active HIGH).
- Disconnect actual loads while testing. The ESP32 GPIO must never be connected to 12 V.
- Relays are initialized OFF at boot and emergency stop forces all channels OFF.
- Firmware API token must be changed from the example before use.
- The firmware exposes HTTP on the local network only; do not port-forward it to the internet.

## Stage API
- `GET /api/v1/status`
- `GET /api/v1/sequences`
- `PUT /api/v1/sequences/{id}` — JSON sequence payload
- `POST /api/v1/sequences/{id}/play`
- `POST /api/v1/sequences/stop`
- `POST /api/v1/relay/{1..8}` — `{"state":true}` or `{"state":false}`
- `POST /api/v1/emergency-stop`
- `POST /api/v1/show/event` — `{"event":"SHOW_START","eventId":"unique-id"}`

Sequence JSON example:
```json
{
  "id":"stage-lights-01",
  "name":"Stage Lights",
  "durationMs":5000,
  "initialStates":[false,false,false,false,false,false,false,false],
  "events":[
    {"timeMs":500,"relay":1,"state":true},
    {"timeMs":2500,"relay":1,"state":false}
  ],
  "endBehavior":"ALL_OFF"
}
```

## Install / test
1. Open `StageESP32/StageESP32.ino` in Arduino IDE.
2. Install the ESP32 board package and ArduinoJson library.
3. Change Wi-Fi credentials and API token in the sketch.
4. Upload to Stage ESP32 and open Serial Monitor at 115200.
5. Confirm `GET /api/v1/status` responds from a device on the same Wi-Fi.
6. Keep relay loads disconnected for initial tests.
7. Open `FazbearRelayAnimator` as a project in Android Studio and build the debug APK.
8. Set Stage IP and the same token in the app.

The relay app stores editable sequences locally on the phone and can upload them to Stage. The Stage stores uploaded sequences in LittleFS and runs their timelines locally. App closing does not stop a sequence already executing on Stage.
