# Fully Jarvis desk controller

Replacement controller for a Fully Jarvis sit-stand desk (Jiecang control
box, RJ45 handset port). An ESP32 takes the handset's place entirely: two
physical buttons, a local web dashboard, and native HomeKit with no bridge
or server in the middle.

Runs on a 30-pin ESP-WROOM-32 devkit or a Seeed XIAO ESP32C3.

- **[docs/WIRING.md](docs/WIRING.md)** — pinout, level shifting, power
- **[docs/PARTS.md](docs/PARTS.md)** — what to buy
- **[docs/SETUP.md](docs/SETUP.md)** — flashing, testing, HomeKit pairing

---

## What it does

| | |
|---|---|
| **Physical buttons** | Hold to raise/lower. Wired straight to GPIO, handled in a dedicated realtime task. Work with WiFi down, HomeKit unpaired, router unplugged, or the radio compiled out. |
| **Web dashboard** | `http://desk.local` — height readout, hold-to-move, stop, position slider, presets. Any phone browser, no app. |
| **HomeKit** | Native pairing via HomeSpan. Appears as a Window Covering with a 0–100% slider. Siri: "set my desk to 50 percent". |
| **HTTP API** | `GET /api/state` plus `POST /api/{nudge,stop,goto,preset}` for anything else. |

No Home Assistant. No Homebridge. No Raspberry Pi. One ESP32.

---

## Why this stack

**Arduino/PlatformIO + HomeSpan, not ESPHome.** ESPHome has no native
HomeKit — reaching Siri means a Homebridge or Home Assistant box running
24/7, and if that box dies, so does voice control. HomeSpan pairs the ESP32
to HomeKit directly. Writing the firmware by hand also buys the two things
that matter most here: physical buttons on a task that preempts the network
stack, and a movement layer that cannot run away.

**Window Covering, not Garage Door.** Window Covering carries
`CurrentPosition` / `TargetPosition` / `PositionState`, which maps cleanly
onto a desk and gives you a percentage slider. Garage Door is binary, has no
position characteristic, and iOS treats it as a security accessory —
confirmation prompts, no unattended Siri.

**Buttons on their own FreeRTOS task.** They are scanned every 5 ms at
priority 3, pinned to core 1, above the Arduino `loopTask` that runs
HomeSpan and the web server. A blocked WiFi stack cannot delay a button
release or a safety stop.

---

## The safety design

The Jiecang protocol has a continuous-movement mode that, once triggered
over serial, the original handset cannot stop. **This firmware never uses
it.** There are only two movement primitives, and both stop on their own:

**1. Deadman stepping** — repeated `0x01`/`0x02` step packets. The control
box moves only while packets keep arriving. Every source of held movement
(physical button, web page, serial console) refreshes a 400 ms deadline; the
driver sends `STOP` the moment that lapses. A released button, a locked
phone, dropped WiFi, a crashed browser, or an unplugged ESP32 all converge
on the same outcome: the desk stops. This is a property of the mechanism,
not of the error handling.

**2. Bounded go-to-height** — `0x1B` with an absolute target. The box drives
there and halts by itself. Targets are clamped to the limits read live from
the box, and the move is watchdogged anyway.

On top of those:

| Guard | Behaviour |
|-------|-----------|
| Stall detection | Height not changing during a commanded move → stop after 1.5 s, report obstruction to HomeKit |
| Move ceiling | Any single move force-stopped after 25 s |
| Limit clamping | Targets clamped to the box's own reported min/max; go-to is refused outright until limits are known |
| Button priority | A physical press cancels any in-flight remote move |
| Panic gesture | Both buttons at once → stop (or fit an optional third dedicated stop button) |
| Frame validation | Checksum verified and length-driven parsing, so a payload byte of `0x7E` cannot desync the parser; implausible heights rejected |

[docs/SETUP.md](docs/SETUP.md) has a table of these you can walk through
deliberately once the hardware is together.

---

## Protocol reference

```
  F1 F1 <cmd> <len> <params...> <cksum> 7E     handset  → box
  F2 F2 <cmd> <len> <params...> <cksum> 7E     box → handset

  cksum = (cmd + len + Σ params) & 0xFF
  9600 baud, 8N1
```

Commands used:

| Cmd | Direction | Meaning |
|-----|-----------|---------|
| `0x01` / `0x02` | → box | step up / down (repeat to keep moving) |
| `0x2B` | → box | stop, and the wake-up knock when the box has gone idle |
| `0x1B` | → box | go to absolute height, 2 params, tenths |
| `0x05 06 27 28` | → box | move to preset 1–4 |
| `0x03 04 25 26` | → box | store preset 1–4 at current height |
| `0x0C` / `0x20` / `0x07` | → box | query physical range / soft limits / settings |
| `0x0E` | → box | set units (`0x01` = inches) |
| `0x01` | ← box | height report, 16-bit big-endian |
| `0x07` | ← box | physical range: `{max}{min}` |
| `0x20` / `0x21` / `0x22` | ← box | which soft limits are set / max / min |
| `0x23` | ← box | travel limit reached |
| `0x25`–`0x28` | ← box | preset heights |

The raw height word is **tenths of an inch** in IN mode and **millimetres**
in CM mode, so the firmware pins the box to inches at boot
(`FORCE_UNITS_INCHES` in `src/config.h`) to keep the numbers unambiguous.

Reverse engineering credit: [phord/Jarvis](https://github.com/phord/Jarvis)
for the protocol and pinout,
[Rocka84/esphome_components](https://github.com/Rocka84/esphome_components/tree/master/components/jiecang_desk_controller)
for the `0x1B` go-to-height command and the limit-discovery sequence.

---

## Layout

```
platformio.ini            pinned platform + the three stage environments
src/config.h              pins, timings, safety constants
src/secrets.h.example     copy to secrets.h for WiFi
src/JiecangDesk.{h,cpp}   protocol driver, parser, all safety timers
src/HomeKitDesk.h         HomeSpan Window Covering accessory
src/WebUI.{h,cpp}         HTTP server + JSON API
src/web_page.h            the dashboard, one self-contained page
src/main.cpp              tasks, button scanning, startup
```

---

## Caveats

- **This voids your warranty.** You are replacing the handset on a powered
  motor system.
- Tested against the protocol as documented for **JCB36N2CA / FullyCB2C-A**
  boxes. The two-button **JCB35N2** uses a different serial protocol behind
  an identical connector — if stage 1 sees no frames in either wiring
  orientation, that is the likely reason.
- The ESP32 is **not 5 V tolerant**. The RX divider in
  [WIRING.md §3](WIRING.md) is not optional.

## License

MIT. See [LICENSE](LICENSE).
