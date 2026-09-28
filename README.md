# Mochi-C3 🍡

A DasaiMochi-style companion firmware for the **ESP32-C3 Super Mini**, with
original round "kawaii" eyes (no rectangular DasaiMochi eyes) and a BLE data
link to the **Chronos** phone app, using [fbiego/chronos-esp32](https://github.com/fbiego/chronos-esp32).

Chronos supplies time,
weather, notifications, navigation, music and phone-battery data over BLE;
Dirgamochi owns the OLED face, a small piezo buzzer, and the three physical
touch buttons. Voice (INMP441 mic + MAX98357A amp) is wired in and the
driver code is present, but it ships **disabled** so the very first flash
is guaranteed to boot to a working face + Chronos link even before the
audio side is tuned.

## Hardware

| Part | Interface | Dirgamochi pin |
|---|---|---|
| ESP32-C3 Super Mini | — | brain |
| SSD1306 128x64 OLED | I2C | SDA=GPIO21, SCL=GPIO20 (addr `0x3C`) |
| INMP441 mic | I2S in | SCK=GPIO1, WS=GPIO2, SD=GPIO10 |
| MAX98357A amp | I2S out | BCLK=GPIO1, LRC=GPIO2, DIN=GPIO5 |
| TTP223 touch ×3 | digital | TALK=GPIO4, NEXT=GPIO6, MODE=GPIO7 |
| Passive piezo buzzer *(optional)* | PWM (ledc) | GPIO3, off by default — see below |

All pins are centralized in [`include/dirgamochi_config.h`](include/dirgamochi_config.h) —
change them there, not in the source files.

> **Strapping-pin note:** GPIO2, GPIO8 and GPIO9 are ESP32-C3 strapping pins.
> GPIO9 is the BOOT button and isn't used here. GPIO8 isn't used anywhere in
> this pin map either (the mic's `SD` line was moved to GPIO10), so no
> strapping-pin ambiguity remains. GPIO2 (shared mic `WS` / amp `LRC`) is
> only sampled by the ROM at the instant of reset and is otherwise a normal
> I2S line, so normal operation is fine. The buzzer's GPIO3 is likewise not
> a strapping pin, and is otherwise unused in this pin map.

## Buttons

- **TALK** (GPIO4) — short press: jump straight to the phone/status screen
  (or, on the QR screen, cycle to the next relayed link). Long press: send
  Chronos "Find Phone".
- **NEXT** (GPIO6) — short press: cycle FACE → TIME → WEATHER →
  NOTIFICATIONS → NAVIGATION → MUSIC → PHONE → QR → FACE. **Long press:
  open the settings menu.**
- **MODE** (GPIO7) — short press: jump back to FACE (or exit the menu).
  Long press: send Chronos `MUSIC_TOGGLE`.

Inside the **settings menu**, the same three buttons are repurposed: NEXT
moves the selection, TALK activates/toggles the highlighted item, MODE
exits back to the face.

## Face

Six expressions, matching the reference photos (round eyes, highlight
"sparkle" dot, small blush-style mouths): **Normal, Happy, Sleepy,
Surprised, Angry, Cute**. All of them are drawn procedurally with
`Adafruit_GFX` primitives (circles/arcs), not stored bitmaps, which keeps
flash usage tiny and makes it trivial to tweak proportions in
[`src/face_engine.cpp`](src/face_engine.cpp).

The face is no longer static between events:

- Blink timing is seeded from the chip's hardware RNG (`esp_random()`), so
  it's genuinely different every boot instead of following a fixed pattern,
  and blinks occasionally chain into a quick **double-blink** (~18% chance)
  like a real eye does.
- The blink itself follows an eased close/open curve (`easeInOut`) instead
  of moving at constant linear speed.
- When idle, the eyes **drift and glance around** on their own, easing
  smoothly toward each new look target rather than snapping to it.
- A small connection dot (filled = paired, hollow = advertising) sits in
  the top-right corner.

### Bitmap idle moods (assets/faces/*.zip)

When nothing else is going on (plain idle), the OLED shows a 98x64 bitmap
face instead of the procedural eyes:

- **Rests** on a neutral frame (no redraw while nothing changes, so it
  costs almost no I2C/CPU time).
- **Blinks** every 2.5-5.5s using a real "eyes closed" frame lifted from
  the animations, sometimes twice in a row (double-blink).
- Every 20-45s it plays one **randomly chosen full mood animation**
  (~17s at 10fps: dizzy eyes, heart eyes, alert triangle eyes, content
  squint, ...) then returns to rest.

Every named expression - Happy on BLE connect, Surprised on
notification/alarm/find-phone, Cute on nav/music, Sleepy in quiet hours or
after the idle timeout, the "petting" reaction - stays on the procedural
face above, unchanged, and hands back to the bitmap face afterwards.
Timings/FPS are tunable in `dirgamochi_config.h` (`SPRITE_*`). To make the
moods play back-to-back nonstop, set both `SPRITE_SHOW_INTERVAL_*` values
to something tiny.

**How the assets get into the firmware:** the repo keeps only the small
PNG zips (`assets/faces/mochi_0.zip` ... `mochi_9.zip`, ~3.3MB total).
`tools/generate_faces.py` converts every frame to a 1-bit dithered bitmap
and writes `src/generated/face_frames.cpp` + `include/generated/face_frames.h`
(~1.4MB of flash; git-ignored, never committed). It runs automatically
before every `pio run` (via `extra_scripts` in `platformio.ini`) and as an
explicit "Extract face animations" step in the GitHub Actions workflow
before compiling. It only regenerates when a zip or the script changed.
To swap in new animations, just replace the zips (each `mochi_N.zip` holds
numbered PNGs; keep 98x64) and push. Requires Python + Pillow
(`pip install pillow`; PlatformIO installs it on demand).

### Ambient behaviour (quiet hours / idle / touch)

- If Chronos reports its **quiet hours** or **sleep schedule** as active,
  the face settles into the Sleepy expression and wakes back up once
  those hours end.
- With no button press, notification or navigation for
  `IDLE_SLEEPY_TIMEOUT_MS` (5 minutes by default, see
  `dirgamochi_config.h`), the face drifts to Sleepy on its own and wakes on
  the next touch or event.
- If the Chronos app relays a **remote touch** (`RemoteTouch`, already
  present in the vendored library but previously unused), the eyes look
  toward the touched point, and releasing gives a brief Happy "petting"
  reaction. This depends on the Chronos app actually sending touch data for
  this screen profile — treat it as experimental and confirm on real
  hardware.

## Buzzer / notification sound

By default, sound comes out of the **speaker you already have** — the
MAX98357A amp — through a small non-blocking I2S tone generator in
`audio_engine.cpp` (`ENABLE_SPEAKER_BEEP`, on by default). It only drives
the amp's existing pins (`SPK_BCLK`/`SPK_LRC`/`SPK_DIN`) in TX-only mode
and never touches the mic pin, so it works even with `ENABLE_AUDIO` (the
full mic+voice pipeline) left at `0`. Renders:

- Two short rising chirps on an incoming **notification**.
- A single short beep when **navigation** starts.
- A repeating two-tone pattern while a Chronos **alarm** is ringing —
  silenced by pressing any one of the three buttons.
- A quiet click when moving through the **settings menu**.

If you additionally wire a separate passive piezo buzzer to GPIO3, flip
`ENABLE_BUZZER` to `1` in `include/dirgamochi_config.h` to have the same
patterns also (or only) play through it via PWM (`ledc`) — off by default
since most boards built from the reference pinout don't have one.

Either output can be muted independently from the settings menu's
**Buzzer** toggle (persists across reboots) — that toggle silences
whichever of the two is enabled.

No sound at all after flashing? Check which speaker hardware you actually
have:
- **MAX98357A + speaker** (the default assumption above) — verify
  `SPK_BCLK`/`SPK_LRC`/`SPK_DIN` match your wiring, and that the module's
  gain/shutdown pin (if it has one, often tied off via a resistor on the
  breakout rather than a GPIO) is actually enabled.
- **A separate piezo on GPIO3** — make sure `ENABLE_BUZZER` is set to `1`;
  it's off by default.

## Settings menu (NEXT long-press)

A tiny on-device list, navigable with the existing three buttons, backed
by NVS (`Preferences`) so choices survive a power cycle:

- **Buzzer** — beeps on/off.
- **Volume** — Low / Med / High (TALK cycles; you hear the new level
  immediately). Sets the sine amplitude of the speaker beeps; levels are
  `BEEP_VOLUME_*` in `dirgamochi_config.h`.
- **Rotate** — flips `OLED_ROTATION` live, no reflash needed.
- **Reset BLE** — restarts BLE advertising (`stop()`/`begin()`), for when
  the phone has a stale Chronos pairing entry.
- **Device info** — full-screen panel: BLE name, BLE status (Connected /
  Advertising), MAC address and firmware version. TALK or MODE closes the
  panel and returns to the list.

## QR / Link screen

Cycled to with NEXT, after PHONE. Renders whatever the Chronos app has
relayed via its QR/link-sharing feature
(`ChronosESP32::getQrAt()`/`setQr()` — present in the vendored library from
the start, just not wired to a screen until now) as an actual scannable QR
code (via the `ricmoo/QRCode` library), not just raw text. If more than one
link has been sent, TALK cycles between them.

## Chronos features wired up in this base

- Time sync + 12/24h clock screen
- Weather (current temp / high / low / city)
- Notifications (latest app/title/message) — text is UTF-8-sanitized
  before wrapping (see below), so emoji/smart-quotes/etc. no longer
  produce garbled or misaligned lines on long messages.
- Navigation (turn info / distance / ETA)
- Music metadata + `MUSIC_TOGGLE` control
- Phone battery + charging state
- Find-phone command
- Alarms (buzzer + face reaction while ringing)
- QR/link relay (dedicated screen)
- Remote touch relay ("petting" reaction)
- Quiet hours / sleep schedule (ambient sleepy face)

Pairing: open the **Chronos** app → *Watches* tab → *Watches* button →
*Pair New Devices* → *Search*, then select **Dirgamochi**. Do **not** pair
it from Android's normal Bluetooth settings — Chronos uses its own BLE
service, not classic Bluetooth pairing.

### Why long WhatsApp-style notifications used to look garbled

The OLED font (`Adafruit_GFX`'s built-in font) only has glyphs for 7-bit
ASCII. WhatsApp/Chronos notification text is UTF-8 and often contains
multi-byte sequences (emoji, curly "smart quotes", ellipsis characters,
...). Printing those raw bytes drew undefined-glyph boxes, and worse, the
old line-wrapper cut the string by **byte** count, which could slice a
multi-byte sequence in half - that's what made *long* messages look
specifically broken compared to short ones. `include/text_utils.h`'s
`sanitizeAsciiOled()` now collapses every such sequence into a single `?`
placeholder *before* wrapping, so wrapping is always byte-safe again.

## Building

### Locally (PlatformIO)

```bash
pio run -e esp32-c3-supermini          # base firmware, audio OFF
pio run -e esp32-c3-supermini -t upload
pio device monitor
```

To try the audio module once the base firmware is confirmed stable:

```bash
pio run -e esp32-c3-supermini-audio
```
(or set `ENABLE_AUDIO` to `1` in `include/dirgamochi_config.h`.)

### From GitHub Actions (no PC needed)

Push to `main` (or run the workflow manually from the **Actions** tab). The
`Build Dirgamochi-C3 firmware` workflow compiles the project and uploads a
`dirgamochi-c3-firmware` artifact containing:

- `esp32-c3-supermini-firmware.bin` — **app only**, flash at `0x10000` on a
  board that already has a bootloader (normal re-flash / update).
- `esp32-c3-supermini-merged.bin` — **full image**, flash at `0x0` on a
  blank/first-time board, e.g.:
  ```bash
  esptool.py --chip esp32c3 write_flash 0x0 esp32-c3-supermini-merged.bin
  ```
  or through a browser flasher (ESP Web Tools / esptool-js): load this one
  file at offset `0x0`.

## Notifications & navigation showing nothing?

That's almost always the phone side, not the firmware:

1. **Notifications** — Chronos needs Android's *Notification Access*
   permission (Settings → Apps → Special app access → Notification access
   → enable **Chronos**). Without it, Chronos never reads your phone's
   notifications, so nothing is ever sent to the ESP32 no matter how the
   firmware is written.
2. **Navigation** — only populates while you're *actively* navigating in a
   supported maps app (Google Maps / Waze / OsmAnd) with Chronos's
   navigation relay enabled in its own settings; it's empty the rest of the
   time by design (`getNavigation().active == false`).
3. Re-pair from **Chronos → Watches → Watches → Pair New Devices** if you
   changed the BLE name/config after a previous pairing — stale pairings on
   the phone can silently stop forwarding data even while BLE shows
   "connected". Or use **Reset pairing** in the on-device settings menu.

## Screen upside down?

Easiest: open the on-device settings menu (long-press NEXT) → **Rotate
180** — takes effect immediately and is remembered across reboots.
Equivalent without touching the menu: flip `OLED_ROTATION` in
`include/dirgamochi_config.h` between `0` and `2` and reflash (this is only
the *default* used the very first time, before any menu choice is saved).



1. Force download mode: hold **BOOT**, tap **RESET**, release **BOOT**,
   then flash again.
2. Try a different USB cable/port — many Super Mini clones brown-out on
   weak cables/hubs because the onboard 3.3V regulator is tiny; this shows
   up as a "boot loop" that isn't actually a firmware bug.
3. Fully erase flash once (`pio run -t erase`, or
   `esptool.py --chip esp32c3 erase_flash`) then reflash the **merged**
   image at `0x0`, not the app-only image.
4. Make sure you flashed the merged image at `0x0` and not at `0x10000` —
   flashing the app-only `.bin` at `0x0` on a blank chip is the single most
   common cause of an ESP32 boot loop.
5. Keep `ENABLE_AUDIO=0` (the default) until steps 1–4 are ruled out, so a
   possible I2S/wiring issue can't be confused with a base-firmware issue.
6. If it still loops, connect a serial monitor at 115200 baud and read the
   `Guru Meditation Error` / backtrace — that tells you exactly which line
   crashed, which is much more useful than guessing.

## Repository layout

```
dirgamochi-c3/
├── include/
│   ├── dirgamochi_config.h   # pin map + feature flags (edit pins here)
│   ├── face_engine.h
│   ├── chronos_ui.h
│   ├── button_engine.h
│   ├── audio_engine.h
│   ├── buzzer_engine.h       # passive-piezo beep patterns (ledc PWM)
│   ├── menu_engine.h         # on-device settings menu + NVS persistence
│   ├── sprite_face_engine.h  # bitmap idle-mood player (rest / blink / random mood)
│   ├── text_utils.h          # UTF-8-safe OLED text sanitizer
│   └── generated/            # (git-ignored) face_frames.h, from tools/generate_faces.py
├── src/
│   ├── main.cpp               # wiring everything together
│   ├── face_engine.cpp        # procedural round-eye faces + idle animation
│   ├── chronos_ui.cpp         # TIME/WEATHER/NOTIF/NAV/MUSIC/PHONE/QR/MENU
│   ├── button_engine.cpp      # debounced short/long press for TTP223 x3
│   ├── audio_engine.cpp       # I2S mic/amp, no-op unless ENABLE_AUDIO=1
│   ├── buzzer_engine.cpp
│   ├── menu_engine.cpp
│   ├── sprite_face_engine.cpp
│   ├── text_utils.cpp
│   └── generated/            # (git-ignored) face_frames.cpp, ~9MB of bitmap arrays
├── assets/faces/mochi_0..9.zip  # source PNG animations (167 frames each, 98x64)
├── tools/generate_faces.py      # PNG zips -> PROGMEM bitmap sources (pre-build)
├── lib/ChronosESP32/          # vendored fbiego/chronos-esp32 v1.9.1 (MIT)
├── .github/workflows/build.yml
├── platformio.ini
└── docs/reference-hardware.md
```

## Credits / references

- [fbiego/chronos-esp32](https://github.com/fbiego/chronos-esp32) — BLE
  link to the Chronos app (vendored in `lib/ChronosESP32`, MIT license).
- [fbiego/ESP32Time](https://github.com/fbiego/ESP32Time) — RTC helper used
  by ChronosESP32.
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — BLE
  stack used by ChronosESP32.
- [ricmoo/QRCode](https://github.com/ricmoo/QRCode) — QR code generation
  for the QR/link screen (MIT license).
- DasaiMochi-style desk companion concept — see community builds such as
  `maraulsav/Dasai-Mochi`, `SeoDan1/ESP32-Dasai-Mochi-Based-on-Huykhong-`
  and `MazBayID/Dasai-Mochi-Bot-firmware-for-ESP32-C3-Super-Mini` for the
  original rectangular-eyes design this project intentionally departs from.

## License

Firmware code in this repository: MIT (see `LICENSES.md`). The vendored
`lib/ChronosESP32` keeps its own upstream MIT license
(`lib/ChronosESP32/LICENSE-chronos`).

## v0.5 changes

- New bitmap idle-mood face system from 10 PNG animations (see above):
  rest / real-frame blink with double-blinks / random full mood shows.
  Assets stay zipped in the repo; the build (local + GitHub Actions)
  converts them to firmware sources first.
- Menu: new **Volume** (Low/Med/High) and **Device info** (BLE name/status,
  MAC, firmware) items.
- One-off reactions (connect, find-phone, nav, music toggle, alarm end) now
  fade back to the idle face on their own instead of sticking.

## v0.4.1 fix

- Notification/nav/alarm/menu beeps now come out of the **existing
  MAX98357A speaker** by default (non-blocking I2S TX-only tone in
  `audio_engine.cpp`), instead of assuming a separate piezo on GPIO3 that
  most boards built from the reference pinout don't actually have.
- The GPIO3 piezo path from v0.4 is kept as an optional extra
  (`ENABLE_BUZZER`, now `0` by default) for anyone who wires one in
  addition to the speaker.

## v0.4 changes

- Fixed garbled long notifications: text is UTF-8-sanitized before the
  existing byte-count line wrapper runs, so multi-byte sequences (emoji,
  smart quotes, ellipsis) can no longer be sliced in half.
- Real hardware-RNG blink timing, occasional double-blinks, eased blink
  envelope, and idle look-around with eased motion.
- New passive-piezo buzzer (GPIO3): beeps for notifications, navigation
  updates, and active alarms.
- New on-device settings menu (NEXT long-press): buzzer on/off, live OLED
  rotation toggle, BLE pairing reset, firmware info - persisted to NVS.
- New QR/link screen rendering Chronos's relayed links as a real QR code.
- Wired up Chronos RemoteTouch ("petting" reaction) and quiet/sleep-hours
  awareness (ambient sleepy face), plus an idle timeout to Sleepy.
- Added `ricmoo/QRCode` to `platformio.ini` lib_deps.

## v0.2 changes

- Uses the fully initialized default `ChronosESP32` constructor.
- Explicitly requests time/settings synchronization after BLE connection.
- Enables phone-battery notifications.
- Incoming notifications automatically open the notification screen.
- Notification text is wrapped across the 128x64 display.
- Active navigation automatically takes display priority.
- Chronos 48x48 navigation bitmap is rendered directly on the OLED.
- Navigation data and icon updates are logged to Serial.
- INMP441 SD is **GPIO10**.
- PlatformIO Espressif32 updated to 6.13.0.
