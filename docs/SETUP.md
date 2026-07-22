# Setup

The firmware builds in three stages. Each one is independently testable and
each adds exactly one layer. Do them in order — it means that when something
breaks you already know which layer broke.

| Stage | Command | What you get |
|-------|---------|--------------|
| 1 | `pio run -e stage1 -t upload` | Buttons + serial console. No radio at all. |
| 2 | `pio run -e stage2 -t upload` | + web dashboard |
| 3 | `pio run -e stage3 -t upload` | + native HomeKit (default env) |

Each stage is a PlatformIO environment, so switching is a command-line flag
and never an edit you have to remember to undo.

**On a Seeed XIAO ESP32C3, use `xiao1` / `xiao2` / `xiao3` instead** —
same stages, different pins and chip settings. Everything below applies
unchanged apart from the environment name. Wiring for it is in
[WIRING.md §5](WIRING.md).

---

## 0. Toolchain

```bash
brew install platformio        # or: pipx install platformio
cd fully-desk-control
pio run                        # first build downloads ~500 MB, once
```

The first build pulls the pioarduino ESP32 platform and HomeSpan. Later
builds take seconds.

Find your board's serial port:

```bash
pio device list
```

macOS shows something like `/dev/cu.usbserial-0001` or `/dev/cu.SLAB_USBtoUART`.
If nothing appears, you are missing the USB-UART driver — CP2102 boards need
Silicon Labs' driver, CH340 boards need WCH's.

---

## Stage 1 — buttons only, no radio

**This is the stage that proves your wiring.** Do not skip ahead to WiFi.

### 1a. Buttons in isolation, desk disconnected

Wire only the two buttons. Leave the RJ45 harness unplugged from the control
box entirely.

```bash
pio run -e stage1 -t upload
pio device monitor
```

You should see `stage 1: buttons + serial only.` Type `?` and press enter:

```
height=0.0 in  pct=nan  limits=0.0..0.0  motion=0  link=0  frames ok=0 bad=0  fault=none
```

`link=0` and `frames ok=0` are correct here — nothing is connected. This
step confirms the board boots, the task starts, and the console responds.

To confirm the buttons themselves are read correctly before trusting them
with a motor, temporarily add this to the top of `loop()` in `src/main.cpp`:

```cpp
static uint32_t t; if (millis() - t > 250) { t = millis();
  Serial.printf("up=%d down=%d\n", !digitalRead(PIN_BTN_UP), !digitalRead(PIN_BTN_DOWN)); }
```

Reflash, press each button, confirm the right one reads `1`. **Remove the
snippet afterwards** — it prints forever and clutters the console.

### 1b. Connect the desk

Power everything down. Connect the RJ45 harness per
[WIRING.md §5](WIRING.md). Power up.

In the monitor, type `?` again. Within a few seconds you want:

```
height=42.3 in  pct=38.4  limits=24.5..50.2  motion=0  link=1  frames ok=57 bad=0
```

Diagnosis:

| Symptom | Cause | Fix |
|---------|-------|-----|
| `frames ok=0 bad=0` | TX/RX swapped, or no link | Power down, swap RJ45 pins 2 and 4 (divider stays on the GPIO16 side, 330 Ω on the GPIO17 side) |
| `frames ok=0 bad>0` | Right pins, wrong baud or noise | Check the divider actually outputs ~3.2 V, check the GND connection |
| Still nothing after swapping | Probably a **JCB35N2** box | Different serial protocol despite the same connector. See phord/Jarvis discussion #4 |
| `limits=0.0..0.0` but height reads | Limit query not answered yet | Wait ~10 s; the firmware retries the sweep every 3 s |
| Board resets when you move the desk | 5 V rail sagging | Add/increase the bulk cap, or move to USB power ([WIRING.md §4](WIRING.md)) |

### 1c. First movement — do this with your hand on the desk

Console commands:

```
u        raise for 1 second, then stop
d        lower for 1 second, then stop
s        stop immediately
g50      go to 50% of travel
p2       go to preset 2
?        status
```

Start with `u`. The desk should move for about a second and stop on its own.
Then test the physical buttons: hold UP, the desk rises; release, it stops.

**Then test the failure mode deliberately.** Hold UP and, while still
holding, pull the ESP32's USB cable. The desk must stop within about half a
second. That is the deadman working: the driver only moves the desk while
step packets keep arriving, so a dead controller means a stopped desk. If it
does *not* stop, something is wrong — stop and re-read the firmware before
going further.

---

## Stage 2 — web dashboard

```bash
cp src/secrets.h.example src/secrets.h
```

Edit `src/secrets.h` with your SSID and password, then:

```bash
pio run -e stage2 -t upload && pio device monitor
```

The monitor prints the IP:

```
wifi.....
http://<desk-ip>/  (or http://desk.local/)
```

Open it on your phone. You get current height, hold-to-move raise/lower, a
stop button, a position slider, and the four presets. No app, no account,
no cloud — it is served off the ESP32.

Notes:

- **`desk.local` works from iOS and macOS.** Android has patchy mDNS; use
  the raw IP there, and give the ESP32 a DHCP reservation in your router so
  it stays put.
- Hold-to-move on the web page repeats a request every 120 ms and relies on
  the same deadman as the physical buttons. Lock your phone mid-hold and the
  desk stops. That is intended.
- `GET /api/state` returns JSON if you want to wire this into anything else.

---

## Stage 3 — HomeKit

Set a pairing code first. In `src/secrets.h` (copy from `secrets.h.example`):

```c
#define HOMEKIT_PAIRING_CODE "31415926"   // pick your own
```

Left undefined, it falls back to HomeSpan's public default `46637726`.

Eight digits. HomeKit rejects trivial codes (all identical digits,
`12345678`, `87654321`).

```bash
pio run -e stage3 -t upload && pio device monitor
```

Stage 3 is the default environment, so a bare `pio run -t upload` also
builds it.

### Pairing

1. iPhone → **Home** app → **+** → **Add Accessory**
2. **More options…** — there is no scannable code on a bare ESP32
3. **Desk** appears under nearby accessories; tap it
4. Enter your pairing code
5. Accept the "uncertified accessory" warning. That is expected for anything
   not in Apple's MFi program.

The desk lands as a **Window Covering** with a percentage slider. 0% is the
lower travel limit, 100% the upper, both read live from the control box.

### Siri

- "Hey Siri, set my desk to 50 percent"
- "Hey Siri, open my desk" / "close my desk" (up / down — Window Covering
  vocabulary, unavoidable)
- "Hey Siri, raise my desk" / "lower my desk"

Rename it in the Home app if you want different phrasing. "Desk" is the
name it advertises.

### HomeSpan serial console

In stage 3 HomeSpan owns the serial port. Useful commands:

| Key | Does |
|-----|------|
| `s` | status: WiFi, pairing state, accessory list |
| `W` | set WiFi credentials interactively |
| `A` | restart the WiFi/pairing setup |
| `U` | unpair — wipes HomeKit pairings, keeps WiFi |
| `F` | factory reset — wipes everything |
| `?` | full help |

If pairing fails and the Home app says the accessory is already paired,
press `U` and try again. That is nearly always a stale pairing from an
earlier flash.

---

## Verifying the safety behaviour

Worth doing once, on purpose, with the desk clear:

| Test | Expected |
|------|----------|
| Hold UP, release | Stops immediately |
| Hold UP, yank ESP32 power mid-move | Stops within ~0.5 s |
| Hold UP on the web page, lock the phone | Stops within ~0.5 s |
| Both buttons at once | Stops, ignores both |
| Siri "set desk to 100%", then press a physical button | Physical button wins, cancels the move |
| Siri "set desk to 100%", then block the desk with your hand | Stops within ~1.5 s, Home app shows obstruction |
| Any move running longer than 25 s | Force-stopped |

Every one of these is a firmware behaviour, not a coincidence — see the
guarantees section of the [README](../README.md).

---

## Reflashing later

Because the Schottky diode blocks USB back-feed, you can leave the desk
harness connected while flashing over USB. Both supplies can be live at
once; the diode makes sure current only goes one way.
