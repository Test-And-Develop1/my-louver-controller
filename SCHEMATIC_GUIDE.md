# Louver Controller — Schematic & Wiring Guide

Point-to-point wiring for `LouverController/LouverController.ino`, written for
KiCad schematic capture and breadboard builds.

**Reference designators used throughout**

| Ref | Part | Role |
|-----|------|------|
| A1  | Arduino Nano v3 (ATmega328P) | Controller |
| U1  | L293D | H-bridge for Motor 1 (ch 1/2) and Motor 2 (ch 3/4) |
| U2  | L293D | H-bridge for Motor 3 (ch 1/2); ch 3/4 unused |
| DS1 | 1.8" TFT, ST7735S, 128x160, 4-wire SPI | Display |
| SW1 | EC11 rotary encoder with push switch | Menu knob |
| SW2 | Momentary push button (K0 / KEY) | START key |
| U3  | DS18B20 (TO-92, 3-wire) | Temperature sensor |
| M1–M3 | DC louver motors | Louver actuators |
| J1  | 12 V DC input | Motor supply |

**Nano header pin numbers.** Physical pin numbers follow the standard 30-pin
Nano header used by KiCad's `MCU_Module:Arduino_Nano_v3.x` symbol:
1 D1/TX, 2 D0/RX, 3 RESET, 4 GND, 5 D2, 6 D3, 7 D4, 8 D5, 9 D6, 10 D7, 11 D8,
12 D9, 13 D10, 14 D11, 15 D12, 16 D13, 17 3V3, 18 AREF, 19 A0, 20 A1, 21 A2,
22 A3, 23 A4, 24 A5, 25 A6, 26 A7, 27 +5V, 28 RESET, 29 GND, 30 VIN.
Check these numbers against the symbol in your KiCad library before you annotate.

**L293D pinout (DIP-16).** 1 EN1,2 · 2 1A · 3 1Y · 4 GND · 5 GND · 6 2Y · 7 2A ·
8 VCC2 · 9 EN3,4 · 10 3A · 11 3Y · 12 GND · 13 GND · 14 4Y · 15 4A · 16 VCC1

---

## 1. Power & Ground Isolation Network

### 1.1 12 V motor supply (`+12V`)

| From | To | Notes |
|------|----|-------|
| J1 (+) | F1 fuse, pin 1 | Fast-blow fuse sized to the total stall current of your motors (≤ 2 A for two L293Ds) |
| F1 fuse, pin 2 | Net `+12V` | |
| `+12V` | U1 pin 8 (VCC2) | Motor supply for U1's output stages |
| `+12V` | U2 pin 8 (VCC2) | Motor supply for U2's output stages |
| `+12V` | C1 (100 µF / 25 V electrolytic) + → GND | Bulk capacitor, placed at U1 |
| `+12V` | C2 (100 nF ceramic) → GND | Decoupling capacitor, right at U1 pin 8 |
| `+12V` | C3 (100 µF / 25 V electrolytic) + → GND | Bulk capacitor, placed at U2 |
| `+12V` | C4 (100 nF ceramic) → GND | Decoupling capacitor, right at U2 pin 8 |

The L293D accepts 4.5–36 V on VCC2. It can supply **600 mA continuous per
channel (1.2 A peak)**, so check that each louver motor's running and stall
current fits within that. The L293D has built-in output clamp diodes, so the
motors need no external flyback diodes. (The L293 without the "D" has no clamp
diodes; don't substitute it.)

### 1.2 5 V logic rail (`+5V`)

`+5V` is the net on Nano **pin 27 (+5V)**. Every logic load connects to it:

| From | To | Notes |
|------|----|-------|
| A1 pin 27 (+5V) | U1 pin 16 (VCC1) | L293D logic supply. Add C5 (100 nF) to GND right at the pin |
| A1 pin 27 (+5V) | U2 pin 16 (VCC1) | Add C6 (100 nF) to GND right at the pin |
| A1 pin 27 (+5V) | U1 pin 1 (EN1,2), U1 pin 9 (EN3,4), U2 pin 1 (EN1,2) | Keeps the bridges for M1–M3 permanently enabled (the firmware never drives the enable pins) |
| A1 pin 27 (+5V) | DS1 VCC | Display supply (the board is marked 5 V-compatible) |
| A1 pin 27 (+5V) | DS1 BLK | Backlight always on |
| A1 pin 27 (+5V) | U3 pin 3 (VDD) | DS18B20 in normal-power mode, not parasite power |
| A1 pin 27 (+5V) | R1 (4.7 kΩ) → `ONEWIRE_DQ` | 1-Wire pull-up resistor, see §3 |

**Where does the +5V rail get its power?** Choose one source:

- **Recommended: a 12 V → 5 V buck converter.** Use a module rated at least
  1 A (MP1584, LM2596 or similar). Connect its input to `+12V`, its output to
  the `+5V` net (Nano pin 27), and leave Nano VIN (pin 30) unconnected. The Nano's
  USB port has a series diode, so USB can stay plugged in for programming. Some
  clone Nanos leave this diode out, so check yours before connecting USB and the
  buck converter at the same time.
- **Bench only: 12 V into Nano VIN (pin 30).** The Nano's on-board regulator
  then has to drop 7 V for every load on the +5V rail. Two L293D logic supplies
  (up to 60 mA each) plus the display backlight can push that regulator past
  1 W, and the small regulators on Nano clones overheat at that level.

### 1.3 Common ground architecture (`GND`)

All grounds join into one net, `GND`, wired as a **star** from a single point
next to J1's negative terminal:

| Ground pin | Connects to |
|------------|-------------|
| J1 (−), the 12 V supply negative | Star point |
| A1 pin 4 (GND) and A1 pin 29 (GND) | Star point |
| DS1 GND | Star point (via the logic ground branch) |
| U1 pins 4, 5, 12, 13 | Star point (via the motor ground branch) |
| U2 pins 4, 5, 12, 13 | Star point (via the motor ground branch) |
| U3 pin 1 (GND) | Logic ground branch |
| SW1 encoder common (C) and one pin of the push switch | Logic ground branch |
| SW2 (K0), one pin | Logic ground branch |
| Buck converter GND (if used) | Star point |

Without this shared ground, the Nano's logic levels have no reference at the
L293D inputs, and those inputs float.

- **Keep motor current out of the Nano's ground.** Run the L293D ground pins to
  the star point on their own thick conductor, separate from the logic ground.
- **The L293D ground pins also carry heat.** Pins 4, 5, 12 and 13 are how the
  chip sinks heat. On a PCB, tie all four to a large copper pour. On a
  breadboard, wire all four even though they are connected inside the chip.

---

## 2. Complete Netlist Mapping Table

### 2.1 Nano signal map

| Component Pin Name | Arduino Nano Pin Number | Recommended KiCad Net Name |
|--------------------|-------------------------|----------------------------|
| DS1 SCL (SPI clock) | D13 (pin 16) | `TFT_SCL` |
| DS1 SDA (SPI data in) | D11 (pin 14) | `TFT_SDA` |
| DS1 RES (reset) | D8 (pin 11) | `TFT_RES` |
| DS1 DC (data/command) | D9 (pin 12) | `TFT_DC` |
| DS1 CS (chip select) | D10 (pin 13) | `TFT_CS` |
| DS1 BLK (backlight) | +5V (pin 27) | `+5V` |
| DS1 VCC | +5V (pin 27) | `+5V` |
| DS1 GND | GND (pin 4 / 29) | `GND` |
| SW1 Encoder A | D3 (pin 6) — INT1 | `ENC_A` |
| SW1 Encoder B | D4 (pin 7) | `ENC_B` |
| SW1 Encoder PUSH | D5 (pin 8) | `ENC_SW` |
| SW2 KEY K0 (START) | D6 (pin 9) | `KEY_K0` |
| U3 DS18B20 DQ (pin 2) | D2 (pin 5) | `ONEWIRE_DQ` |
| U1 pin 2 (1A) — Motor 1 A, drives OPEN | D7 (pin 10) | `M1_A` |
| U1 pin 7 (2A) — Motor 1 B, drives CLOSE | D12 (pin 15) | `M1_B` |
| U1 pin 10 (3A) — Motor 2 A, drives OPEN | A0 (pin 19) | `M2_A` |
| U1 pin 15 (4A) — Motor 2 B, drives CLOSE | A1 (pin 20) | `M2_B` |
| U2 pin 2 (1A) — Motor 3 A, drives OPEN | A2 (pin 21) | `M3_A` |
| U2 pin 7 (2A) — Motor 3 B, drives CLOSE | A3 (pin 22) | `M3_B` |

The firmware drives the display with **software SPI** on D11/D13 so that D12
works as a normal output for `M1_B`. When hardware SPI is enabled it forces
D12 (MISO) to be an input. Do not connect any SPI device's MISO line to D12.

Pins left free: D0/D1 (USB serial; keep them free for programming), A4, A5
(digital or analog), and A6, A7 (analog input only).

### 2.2 L293D full pin assignment

| Pin | U1 (Motor 1 + Motor 2) | U2 (Motor 3) |
|-----|------------------------|--------------|
| 1 EN1,2 | `+5V` | `+5V` |
| 2 1A | `M1_A` | `M3_A` |
| 3 1Y | `M1_OUT_A` → M1 terminal + | `M3_OUT_A` → M3 terminal + |
| 4, 5 GND | `GND` | `GND` |
| 6 2Y | `M1_OUT_B` → M1 terminal − | `M3_OUT_B` → M3 terminal − |
| 7 2A | `M1_B` | `M3_B` |
| 8 VCC2 | `+12V` | `+12V` |
| 9 EN3,4 | `+5V` | `GND` (unused half, disabled) |
| 10 3A | `M2_A` | `GND` (unused input tied low) |
| 11 3Y | `M2_OUT_A` → M2 terminal + | No connect (place a no-connect flag) |
| 12, 13 GND | `GND` | `GND` |
| 14 4Y | `M2_OUT_B` → M2 terminal − | No connect (place a no-connect flag) |
| 15 4A | `M2_B` | `GND` (unused input tied low) |
| 16 VCC1 | `+5V` | `+5V` |

**Motor direction.** The firmware sets pin A HIGH to open and pin B HIGH to
close. If a louver moves the wrong way, swap that motor's two terminal wires.
Leave the `Mx_A` / `Mx_B` signal wiring as it is.

### 2.3 Peripheral-side nets (no Nano pin)

| Component Pin | Net |
|---------------|-----|
| U3 pin 1 (GND) | `GND` |
| U3 pin 3 (VDD) | `+5V` |
| R1 pin 1 / pin 2 | `ONEWIRE_DQ` / `+5V` |
| SW1 encoder common (C) | `GND` |
| SW1 push-switch second pin | `GND` |
| SW2 second pin | `GND` |

DS18B20 TO-92 pinout: hold it with the flat face toward you and the legs
pointing down. From left to right the pins are 1 GND, 2 DQ, 3 VDD.

---

## 3. Bill of Materials (BOM) & Electrical Components

| Qty | Ref | Value / Part | KiCad symbol | Notes |
|-----|-----|--------------|--------------|-------|
| 1 | A1 | Arduino Nano v3 (ATmega328P) | `MCU_Module:Arduino_Nano_v3.x` | |
| 2 | U1, U2 | L293D, DIP-16 | `Driver_Motor:L293D` | Must be the "D" variant (built-in clamp diodes) |
| 1 | DS1 | 1.8" ST7735S TFT, 128x160, 8-pin SPI | `Connector_Generic:Conn_01x08` | Pin order varies between modules; follow your board's silkscreen |
| 1 | SW1 | EC11 rotary encoder with switch | `Device:RotaryEncoder_Switch` | |
| 1 | SW2 | Momentary push button (K0) | `Switch:SW_Push` | |
| 1 | U3 | DS18B20, TO-92 or waterproof probe | `Sensor_Temperature:DS18B20` | |
| 3 | M1–M3 | DC louver motors | `Motor:Motor_DC` | ≤ 600 mA continuous each at 12 V |
| 1 | **R1** | **4.7 kΩ, ¼ W — mandatory** | `Device:R` | Pull-up between `ONEWIRE_DQ` and `+5V` |
| 6 | R2–R7 | 10 kΩ, ¼ W — strongly recommended | `Device:R` | Pull-downs from each `Mx_A` / `Mx_B` net to `GND` (see §4 step 6) |
| 4 | C2, C4, C5, C6 | 100 nF ceramic | `Device:C` | Decoupling at each VCC1/VCC2 pin |
| 2 | C1, C3 | 100 µF / 25 V electrolytic | `Device:C_Polarized` | Bulk capacitor on `+12V`, one at each L293D |
| 1 | F1 | Fuse, 1–2 A fast-blow | `Device:Fuse` | On the 12 V input |
| 1 | J1 | DC barrel jack or 2-pin screw terminal | `Connector:Barrel_Jack` | 12 V input |
| 1 | — | 12 V → 5 V buck converter module, ≥ 1 A | `Connector_Generic:Conn_01x04` | Recommended source for `+5V` (§1.2) |

### 3.1 DS18B20 pull-up resistor (mandatory)

The 1-Wire data line is open-drain: the sensor and the Nano can only pull it
LOW. **R1 (4.7 kΩ) between `ONEWIRE_DQ` (D2) and `+5V`** is what brings the
line back HIGH. Without R1 the bus never idles HIGH. The firmware then finds
no sensor and the Home screen shows **"Sensor ERR"**, and AUTO louvers hold
their position. For a probe cable longer than about 5 m, lower R1 to 2.2–3.3 kΩ.

### 3.2 Internal pull-ups on the encoder and buttons (no external resistors)

The firmware sets D3, D4, D5 and D6 to `INPUT_PULLUP`, which turns on the
ATmega328P's internal pull-ups (roughly 20–50 kΩ to 5 V). Each input then:

- reads **HIGH when open** and **LOW when the contact closes to GND**, and
- needs **no external pull-up resistor**. Wire each contact between its Nano pin
  and `GND`.

| Input | Wiring |
|-------|--------|
| Encoder A, Encoder B | EC11 A → D3, B → D4, common C → GND |
| Encoder PUSH | Switch pin 1 → D5, pin 2 → GND |
| K0 START key | Pin 1 → D6, pin 2 → GND |

Contact bounce is filtered in firmware: 1.5 ms lockout on Encoder A, 30 ms
debounce on each button. For long or noisy cable runs, an optional 10 nF
capacitor from `ENC_A` and from `ENC_B` to GND adds hardware filtering.

If your encoder is on a breakout board (KY-040 style) with its own 10 kΩ
pull-ups and a "+" pin, connect "+" to `+5V`. The on-board pull-ups and the
internal ones then work in parallel, which is harmless.

---

## 4. Step-by-Step KiCad Schematic Capture Workflow

These steps use KiCad 7/8/9, where Eeschema is called the **Schematic Editor**.

1. **Create the project.** File → New Project → `LouverController`. Open the
   schematic and set the page size (A4 or A3) and the title block.

2. **Place the power sources first.** Press `A` and add:
   - J1 (`Connector:Barrel_Jack`), F1 (`Device:Fuse`), and the buck converter
     (`Conn_01x04`: VIN+, VIN−, VOUT+, VOUT−).
   - Power symbols: `power:+12V`, `power:+5V` and `power:GND`.
   - A `power:PWR_FLAG` on each of `+12V`, `+5V` and `GND`, so the electrical
     rules check (ERC) knows these nets are driven.
   Wire J1+ → F1 → `+12V` → buck VIN+, buck VOUT+ → `+5V`, and J1− / buck VIN− /
   buck VOUT− → `GND`.

3. **Place the controller.** Add A1 (`MCU_Module:Arduino_Nano_v3.x`) in the
   middle of the sheet. Connect pin 27 → `+5V` and pins 4 and 29 → `GND`. Put a
   no-connect flag (`Q`) on pins 30 (VIN), 17 (3V3), 18 (AREF), 3/28 (RESET),
   1/2 (TX/RX), 23, 24, 25 and 26. If you power from VIN instead, wire pin 30 to
   `+12V` and don't place the buck converter.

4. **Place the H-bridges.** Add U1 and U2 (`Driver_Motor:L293D`) on the right.
   - VCC2 (pin 8) → `+12V` and VCC1 (pin 16) → `+5V` on both chips.
   - Pins 4, 5, 12 and 13 → `GND` on both chips.
   - U1 pins 1 and 9, and U2 pin 1 → `+5V`. U2 pins 9, 10 and 15 → `GND`.
   - No-connect flags on U2 pins 11 and 14.
   - Place C1–C6 next to the pins they decouple: C1/C2 at U1 VCC2, C3/C4 at
     U2 VCC2, C5 at U1 VCC1, C6 at U2 VCC1.

5. **Add the motor control lines with net labels.** Draw short wires from A1
   and attach net labels (`L`) instead of long wires across the sheet:
   `M1_A` on D7, `M1_B` on D12, `M2_A` on A0, `M2_B` on A1, `M3_A` on A2 and
   `M3_B` on A3. Attach the same labels at the L293D inputs, following §2.2
   (U1 2/7/10/15, U2 2/7).

6. **Wire the hardware side of the interlock.** The firmware keeps A and B of a
   motor from ever being HIGH together. Build the hardware so it matches:
   - **One motor per H-bridge pair, A and B on separate inputs.** `Mx_A` and
     `Mx_B` go only to the 1A/2A (or 3A/4A) inputs of the same pair, and never to
     each other. Run ERC to confirm that no `Mx_A` label shares a net with an
     `Mx_B` label.
   - **Add pull-downs R2–R7 (10 kΩ) from each of the six `Mx_A` / `Mx_B` nets
     to `GND`.** During reset and the roughly 1 s bootloader window, the Nano's
     pins float (high impedance). The L293D's TTL inputs can drift HIGH and
     briefly run a motor. The pull-downs hold every input LOW, so all bridges
     stay off until `setup()` drives the pins LOW.
   - On the L293D, A and B HIGH together sets both outputs to +12 V. That
     brakes the motor; it does not short the supply, because the chip's half
     bridges are independent. The software interlock still matters: it keeps
     every state defined, and it protects the board if you later fit a driver
     where A and B HIGH together would cause a short.
   - Add the motors M1–M3 (`Motor:Motor_DC`) across each output pair:
     `M1_OUT_A`/`M1_OUT_B` on U1 pins 3/6, `M2_OUT_A`/`M2_OUT_B` on U1 pins
     11/14, and `M3_OUT_A`/`M3_OUT_B` on U2 pins 3/6. Draw these as thick wires
     or a bus, so the motor-current paths stand out from the logic wiring.

7. **Place the display.** Add DS1 as a `Conn_01x08`. Name its pins to match the
   module's silkscreen (commonly GND, VCC, SCL, SDA, RES, DC, CS, BLK).
   Connect `TFT_SCL` to D13, `TFT_SDA` to D11, `TFT_RES` to D8, `TFT_DC` to D9
   and `TFT_CS` to D10. Wire VCC and BLK to `+5V` and GND to `GND`.
   Before ordering parts, check that your module level-shifts its SPI inputs.
   The ST7735S itself runs on 3.3 V logic. If the module has no level shifter,
   put a 1 kΩ series resistor in each of the five signal lines, or use a
   74HC4050 buffer.

8. **Place the inputs.** Add SW1 (`Device:RotaryEncoder_Switch`): A → `ENC_A`
   (D3), B → `ENC_B` (D4), C → `GND`, switch → `ENC_SW` (D5) and `GND`.
   Add SW2 (`Switch:SW_Push`): one pin → `KEY_K0` (D6), the other → `GND`.
   Don't add external pull-ups (§3.2).

9. **Place the sensor.** Add U3 (`Sensor_Temperature:DS18B20`): GND → `GND`,
   VDD → `+5V`, DQ → `ONEWIRE_DQ` (D2). Place R1 (4.7 kΩ) between
   `ONEWIRE_DQ` and `+5V`, physically close to the Nano.

10. **Annotate and check.** Tools → Annotate Schematic, then Inspect →
    Electrical Rules Checker. Resolve every error. The usual ones are a
    missing `PWR_FLAG` or an unflagged unused pin. Then compare the netlist
    against §2.1 and §2.2 line by line.

11. **Assign footprints.** Tools → Assign Footprints. Typical choices:
    `Module:Arduino_Nano`, `Package_DIP:DIP-16_W7.62mm` for the L293Ds,
    `Package_TO_SOT_THT:TO-92_Inline` for the DS18B20, and the matching EC11
    and tactile-switch footprints.

12. **Breadboard bring-up order.** Power the board in stages so a wiring
    fault shows up before it can damage anything:
    1. Build the `+5V` and `GND` rails and the Nano with USB only. Flash the
       firmware and confirm the display and inputs work.
    2. Connect the L293Ds with the motors **disconnected**. Apply 12 V and
       measure each `Mx_OUT` pair as the louvers change state on screen.
    3. Connect the motors one at a time, and check that each one's direction
       matches OPEN/CLOSE.

---

*Source of truth for every pin above: the constants at the top of
`LouverController/LouverController.ino`. If you change a pin there, update
§2 here.*
