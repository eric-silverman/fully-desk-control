# Wiring

Read this whole file before stripping anything. The verification pass in
step 2 is the part that protects your control box and your ESP32.

---

## 1. RJ45 pinout on the Jiecang control box

Pin numbering, so we agree on which end is which:

> Hold an RJ45 plug with the **gold contacts facing you** and the **cable
> pointing down**. The locking tab is then on the far side, pointing away
> from you. **Pin 1 is the leftmost contact, pin 8 the rightmost.**

The jack on the control box mates 1-to-1, so a plug's pin 3 lands on the
jack's pin 3.

| Pin | Signal | Direction | T568B colour | Use it? |
|-----|--------|-----------|--------------|---------|
| 1 | HS3 | handset → box | white/orange | no |
| 2 | **DTX** | **box → handset** | **orange** | **yes — ESP32 RX** |
| 3 | **GND** | — | **white/green** | **yes** |
| 4 | **HTX** | **handset → box** | **blue** | **yes — ESP32 TX** |
| 5 | **VCC +5 V** | box → handset | **white/blue** | **yes — power** |
| 6 | HS2 | handset → box | green | no |
| 7 | HS1 | handset → box | white/brown | no |
| 8 | HS0 | handset → box | brown | no |

HS0–HS3 are the original handset's button-multiplex lines. We are replacing
the handset entirely and driving everything over the serial link, so all four
stay unconnected.

**The colour column is a convenience, not a guarantee.** Cheap patch cables
are not always wired to T568B. Verify with continuity before you trust it
(step 2a).

Line characteristics, from phord's reverse engineering:

- 9600 baud, 8 data bits, no parity, 1 stop bit
- Every signal line has a **5 V pull-up inside the control box**
- The +5 V rail can supply at least 300 mA

---

## 2. Verify before you connect (you said you have a multimeter — use it)

### 2a. Map the cable

Cut one end off an ethernet patch cable. Leave the other end as an intact
plug. Strip and tin the eight conductors.

With the DMM on continuity, poke each stripped conductor and find which
plug contact it beeps against. Write down the colour for pins 2, 3, 4, 5.
Do not skip this on the assumption that the cable is T568B.

### 2b. Find GND and +5 V

Desk powered on. Original handset **unplugged**. Plug your cut cable into
the control box's handset port. Nothing else connected.

1. DMM to DC volts. Black probe on a bare metal screw or the metal chassis
   of the control box.
2. Probe your pin-3 conductor. Expect **0.00 V**.
3. Switch to continuity, probe pin 3 against the chassis. Expect a **beep /
   near 0 Ω**. That confirms pin 3 is ground.
4. Black probe back on pin 3. Probe your pin-5 conductor. Expect
   **+4.8 to +5.2 V**. That confirms pin 5 is VCC.
5. Probe pins 2 and 4. Both should idle at roughly **+5 V** (that is the
   pull-up). This confirms they are the signal pair but does **not** tell
   you which is which.

**If pin 3 is not ground or pin 5 is not 5 V, stop.** You have a different
box variant. Find the ground and 5 V pins the same way and re-map before
continuing.

### 2c. TX vs RX

A multimeter cannot distinguish DTX from HTX — both idle high. You resolve
this empirically in step 5, and the **330 Ω resistor in the ESP32's TX line**
makes getting it backwards harmless (it limits drive contention to about
10 mA instead of letting two output stages fight at full current).

---

## 3. Level shifting — the part you cannot skip

The control box runs 5 V logic. **ESP32 GPIOs are not 5 V tolerant.** Feeding
DTX straight into GPIO16 will damage the pin, immediately or slowly.

### RX (box → ESP32): divider is mandatory

```
  RJ45 pin 2 (DTX, 5 V) ──┬── 1.8 kΩ ──┬── ESP32 GPIO16 (RX)
                          │            │
                       (source)      3.3 kΩ
                                       │
                                      GND
```

5 V × 3.3 / (1.8 + 3.3) = **3.24 V**. Comfortably inside spec, comfortably
above the ESP32's ~2.5 V logic-high threshold. Load on the line is about
1 mA, which the box's pull-up does not notice.

Any ratio landing in 3.0–3.3 V works. 1 kΩ / 2 kΩ gives 3.33 V if that is
what you have in the drawer.

### TX (ESP32 → box): series resistor, no shifter needed

```
  ESP32 GPIO17 (TX) ── 330 Ω ── RJ45 pin 4 (HTX)
```

The ESP32 drives 3.3 V. The box's input sits behind a 5 V pull-up, and its
logic-high threshold is low enough that 3.3 V registers cleanly — this is
what every project in the reference list does. The 330 Ω is insurance
against a pins-2-and-4-swapped wiring mistake, not a level-shifting device.

### Alternative: one 4-channel BSS138 module

If you would rather buy a part than count resistors, a BSS138 level shifter
module handles both lines. HV side to 5 V, LV side to 3.3 V, one channel per
direction. At 9600 baud its open-drain edges are irrelevant. Slightly
cleaner, functionally the same. The resistor version is what the parts list
assumes.

---

## 4. Power

You chose: run off the desk's 5 V now, keep the USB port usable.

```
  RJ45 pin 5 (+5 V) ──►|── ESP32 "5V" / VIN pin
                     1N5817
                  (band toward ESP32)

  Across ESP32 5V ↔ GND, mounted close to the board:
      1000 µF electrolytic (16 V)   ‖   0.1 µF ceramic
```

Why each piece:

- **The Schottky diode** stops USB 5 V from back-feeding into the control
  box when you plug in to flash. On most 30-pin devkits the USB rail and the
  VIN pin are the same net with no protection. Without the diode you must
  physically unplug pin 5 every time you flash. Drop is about 0.3 V, leaving
  4.7 V into the AMS1117, which needs 4.4 V. Fine.
- **The 1000 µF cap** rides out WiFi transmit spikes. The desk rail is good
  for ~300 mA; an ESP32 associating or transmitting can briefly ask for
  more, and a bare rail browns out and resets the board.
- **The 0.1 µF** handles the high-frequency edge the electrolytic is too slow
  for. Standard practice, costs nothing.

**Symptoms that the desk rail is not enough:** reboot loops when WiFi
connects, `Brownout detector was triggered` in the serial log, the dashboard
dropping every few minutes. Fix by cutting pin 5 out of the harness and
running the ESP32 from a USB brick instead — GND still shares through pin 3,
which is all the serial link needs. No firmware change either way.

---

## 5. Full connection list

| From | Through | To |
|------|---------|-----|
| RJ45 pin 3 (GND) | — | ESP32 GND |
| RJ45 pin 5 (+5 V) | 1N5817, band toward ESP32 | ESP32 5V / VIN |
| RJ45 pin 2 (DTX) | 1.8 kΩ series, 3.3 kΩ to GND | ESP32 GPIO16 |
| RJ45 pin 4 (HTX) | 330 Ω series | ESP32 GPIO17 |
| — | 1000 µF + 0.1 µF | ESP32 5V ↔ GND |
| UP button leg 1 | — | ESP32 GPIO32 |
| UP button leg 2 | — | ESP32 GND |
| DOWN button leg 1 | — | ESP32 GPIO33 |
| DOWN button leg 2 | — | ESP32 GND |
| STOP button (optional) | — | GPIO25 and GND |

Buttons need **no external resistors**. The firmware enables the ESP32's
internal pull-ups and reads a press as the pin going low. If you fit the
optional third button, set `PIN_BTN_STOP` to `25` in `src/config.h`; with
only two buttons, pressing both at once is the stop gesture.

### If you are using a Seeed XIAO ESP32C3 instead

Same everything on the desk side — pinout, divider, series resistor, diode,
caps. Only the ESP32 pads change. Build with the `xiao1/2/3` environments
and the firmware picks these up automatically.

| From | Through | To (XIAO pad) |
|------|---------|---------------|
| RJ45 pin 3 (GND) | — | `GND` |
| RJ45 pin 5 (+5 V) | 1N5817, band toward XIAO | `5V` |
| RJ45 pin 2 (DTX) | 1.8 kΩ series, 3.3 kΩ to GND | `D4` (GPIO6) |
| RJ45 pin 4 (HTX) | 330 Ω series | `D5` (GPIO7) |
| UP button | — | `D1` (GPIO3) and `GND` |
| DOWN button | — | `D2` (GPIO4) and `GND` |
| STOP button (optional) | — | `D3` (GPIO5) and `GND` |

Four things that differ, all of which matter:

- **Do not use D6/D7 (GPIO21/20) for the desk**, even though they are the
  labelled UART pads. GPIO21 is where the ROM bootloader prints its startup
  log, so every reset would spray 115200-baud chatter down the desk's HTX
  line. The GPIO matrix puts UART1 on D4/D5 for free.
- **Avoid GPIO2, GPIO8, GPIO9 for buttons.** All three are strapping pins,
  and GPIO9 is BOOT — a button holding it low at reset drops the chip into
  firmware download mode instead of running.
- **The `5V` pad is tied directly to USB VBUS**, with no protection at all.
  The 1N5817 is not optional here; without it, plugging in USB feeds 5 V
  straight back into the control box.
- **Plug the antenna in.** The XIAO ships with a separate IPEX antenna and
  its onboard trace antenna is not an antenna. WiFi range without it is
  measured in feet.

No status LED line: the XIAO ESP32C3 has no user-controllable LED, so
`PIN_STATUS_LED` is `-1` and every use of it compiles out.

### Determining TX/RX orientation

Wire as above, flash stage 1 (see [SETUP.md](SETUP.md)), open the serial
monitor and type `?`.

- `frames ok` climbing → correct, done.
- `frames ok=0 bad=0` after 30 seconds → power down, swap the wires on
  pins 2 and 4 (keep the divider on whichever one feeds GPIO16 and the
  330 Ω on whichever feeds GPIO17), power up, retry.
- Still nothing after the swap → your box is likely a **JCB35N2**, which
  uses a different serial protocol despite the identical connector. See
  phord/Jarvis discussion #4.

---

## 6. Pins deliberately avoided

| Pin | Why |
|-----|-----|
| GPIO6–11 | wired to the SPI flash; using them bricks boot |
| GPIO12 | strapping — must read low at reset, a pull-up here stops boot |
| GPIO0, 15 | strapping — affect boot mode |
| GPIO34–39 | input only, **no internal pull-up**, unusable for buttons |

GPIO16/17 (UART2), 32/33 (buttons) and 25 (optional stop) carry no boot-time
duty. GPIO2 drives the onboard LED and is a strapping pin, but only matters
at reset — using it as a status output afterwards is fine.

---

## 7. Assembly notes

- **Wire gauge:** 24 AWG is right, and it is what is already inside the
  ethernet cable. Nothing here carries meaningful current.
- **Length:** keep the RJ45 tail under about 1 m. 9600 baud is forgiving,
  but there is no reason to add antenna.
- **Solid vs stranded:** patch cable is usually solid core, which is stiff
  and breaks if it flexes repeatedly. Anchor the cable so the joints at the
  ESP32 do not take strain.
- **Ordering:** solder and verify the divider on a bench **before** the
  first connection to the desk. Measure the divider output with 5 V applied
  and confirm you read ~3.2 V, not ~5 V. Two minutes now beats a dead GPIO.
