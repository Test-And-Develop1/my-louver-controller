// LouverController: three-zone louver control for Arduino Nano (ATmega328P).
//
// Hardware: 1.8" ST7735S TFT (128x160, 4-wire SPI), EC11 rotary encoder with
// push switch, START key (K0), DS18B20 temperature sensor, and three
// bidirectional louver motors driven through A/B direction pins.
//
// Controls:
//   Home      push = open menu, K0 = start/stop the whole system
//   Menus     turn = move cursor, push = select, K0 = back to Home
//   Editing   turn = change value, push = save, K0 = cancel

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
const uint8_t PIN_ONEWIRE = 2;
const uint8_t PIN_ENC_A   = 3;  // INT1
const uint8_t PIN_ENC_B   = 4;
const uint8_t PIN_ENC_SW  = 5;
const uint8_t PIN_KEY_K0  = 6;

// D12 is the hardware SPI MISO pin. While the AVR SPI peripheral runs as
// master it forces MISO to an input, which would leave Motor 1 pin B floating.
// The TFT is therefore driven by software SPI on the same D11/D13 wires, so the
// SPI peripheral is never enabled and D12 stays a real output.
const uint8_t PIN_TFT_CS   = 10;
const uint8_t PIN_TFT_DC   = 9;
const uint8_t PIN_TFT_RST  = 8;
const uint8_t PIN_TFT_MOSI = 11;  // SDA
const uint8_t PIN_TFT_SCLK = 13;  // SCL

const uint8_t LOUVER_COUNT = 3;

struct MotorPins {
  uint8_t a;  // HIGH = drive open
  uint8_t b;  // HIGH = drive closed
};

const MotorPins MOTOR_PINS[LOUVER_COUNT] = {
  {7, 12},
  {A0, A1},
  {A2, A3},
};

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------
const unsigned long LOUVER_TRAVEL_MS   = 8000;  // Full open<->closed run time
const unsigned long MOTOR_DEADTIME_MS  = 300;   // Both-LOW pause between moves
const int16_t HYSTERESIS_TENTHS        = 5;     // +/-0.5 C around each setpoint
const int16_t SETPOINT_MIN_TENTHS      = 0;
const int16_t SETPOINT_MAX_TENTHS      = 500;
const int16_t SETPOINT_STEP_TENTHS     = 5;
const int16_t SETPOINT_DEFAULT_TENTHS  = 250;
const unsigned long TEMP_PERIOD_MS     = 1000;
const unsigned long TEMP_CONVERSION_MS = 750;   // DS18B20 at 12-bit resolution
const unsigned long UI_FRAME_MS        = 25;
const unsigned long BUTTON_DEBOUNCE_MS = 30;
const unsigned long ENC_DEBOUNCE_US    = 1500;
const int8_t ENC_DIRECTION             = 1;     // Set to -1 if the knob feels reversed
const bool AUTO_START_ON_BOOT          = true;  // false = wait for K0 after power-up
const uint8_t TFT_INIT_TAB             = INITR_BLACKTAB;

// ---------------------------------------------------------------------------
// Louver state
// ---------------------------------------------------------------------------
// All types live above the first function: the Arduino builder inserts
// auto-generated prototypes there, and they must be able to see these types.
enum Position : uint8_t { POS_UNKNOWN, POS_OPEN, POS_CLOSED };
enum Drive : uint8_t { DRIVE_STOP, DRIVE_OPEN, DRIVE_CLOSE };
enum Mode : uint8_t { MODE_AUTO, MODE_FORCE_OPEN, MODE_FORCE_CLOSED };

struct Louver {
  Position pos;
  Position target;
  Drive drive;
  Mode mode;
  int16_t setpoint;         // Tenths of a degree C; louver opens above it
  unsigned long changedAt;  // millis() of the last drive change
};

struct Button {
  uint8_t pin;
  bool stable;
  bool lastRaw;
  unsigned long changedAt;
};

enum Screen : uint8_t { SCR_HOME, SCR_MENU, SCR_SETPOINT, SCR_MANUAL };
enum Style : uint8_t { ST_NORMAL, ST_SELECTED, ST_EDIT, ST_GOOD, ST_WARN, ST_NONE };

Louver louvers[LOUVER_COUNT];
bool systemRunning = AUTO_START_ON_BOOT;

// The only function that writes motor pins. Both pins go LOW before either is
// raised, so A and B are never HIGH at the same time.
void writeMotorPins(uint8_t i, Drive d) {
  const MotorPins &m = MOTOR_PINS[i];
  digitalWrite(m.a, LOW);
  digitalWrite(m.b, LOW);
  if (d == DRIVE_OPEN) {
    digitalWrite(m.a, HIGH);
  } else if (d == DRIVE_CLOSE) {
    digitalWrite(m.b, HIGH);
  }
}

// Interlock: a running motor may only be stopped, never reversed directly, and
// a stopped motor may only start after the dead time has elapsed.
void setDrive(uint8_t i, Drive d, unsigned long now) {
  Louver &l = louvers[i];
  if (d == l.drive) return;
  if (d != DRIVE_STOP) {
    if (l.drive != DRIVE_STOP) d = DRIVE_STOP;
    else if (now - l.changedAt < MOTOR_DEADTIME_MS) return;
  }
  l.drive = d;
  l.changedAt = now;
  writeMotorPins(i, d);
}

void updateLouver(uint8_t i, unsigned long now) {
  Louver &l = louvers[i];
  Drive wanted = DRIVE_STOP;
  if (systemRunning && l.target != l.pos) {
    wanted = (l.target == POS_OPEN) ? DRIVE_OPEN : DRIVE_CLOSE;
  }

  if (l.drive != DRIVE_STOP) {
    if (wanted != l.drive) {
      // Reversal or stop request mid-travel: position is no longer known.
      setDrive(i, DRIVE_STOP, now);
      l.pos = POS_UNKNOWN;
    } else if (now - l.changedAt >= LOUVER_TRAVEL_MS) {
      setDrive(i, DRIVE_STOP, now);
      l.pos = l.target;
    }
  } else if (wanted != DRIVE_STOP) {
    setDrive(i, wanted, now);
  }
}

// ---------------------------------------------------------------------------
// Temperature (non-blocking DS18B20)
// ---------------------------------------------------------------------------
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddr;
bool sensorFound = false;
bool tempValid = false;
bool conversionPending = false;
int16_t tempTenths = 0;
unsigned long tempTimer = 0;

void beginSensor() {
  sensors.begin();
  sensorFound = sensors.getAddress(sensorAddr, 0);
  if (sensorFound) sensors.setResolution(sensorAddr, 12);
  sensors.setWaitForConversion(false);
}

void updateTemperature(unsigned long now) {
  if (!conversionPending) {
    if (now - tempTimer < TEMP_PERIOD_MS) return;
    tempTimer = now;
    if (!sensorFound) {
      beginSensor();
      if (!sensorFound) {
        tempValid = false;
        return;
      }
    }
    sensors.requestTemperaturesByAddress(sensorAddr);
    conversionPending = true;
  } else if (now - tempTimer >= TEMP_CONVERSION_MS) {
    conversionPending = false;
    float c = sensors.getTempC(sensorAddr);
    if (c == DEVICE_DISCONNECTED_C) {
      tempValid = false;
      sensorFound = false;
    } else {
      tempValid = true;
      tempTenths = (int16_t)lround(c * 10.0);
    }
  }
}

// Manual modes win; in AUTO the louver opens above setpoint+hysteresis and
// closes below setpoint-hysteresis. Without a valid reading it holds position.
void updateTargets() {
  for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
    Louver &l = louvers[i];
    if (l.mode == MODE_FORCE_OPEN) {
      l.target = POS_OPEN;
    } else if (l.mode == MODE_FORCE_CLOSED) {
      l.target = POS_CLOSED;
    } else if (tempValid) {
      if (tempTenths >= l.setpoint + HYSTERESIS_TENTHS) l.target = POS_OPEN;
      else if (tempTenths <= l.setpoint - HYSTERESIS_TENTHS) l.target = POS_CLOSED;
    }
  }
}

// ---------------------------------------------------------------------------
// Settings (EEPROM)
// ---------------------------------------------------------------------------
const uint8_t SETTINGS_MAGIC = 0xA7;

struct Settings {
  uint8_t magic;
  int16_t setpoint[LOUVER_COUNT];
};

void loadSettings() {
  Settings s;
  EEPROM.get(0, s);
  for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
    int16_t sp = s.setpoint[i];
    bool ok = s.magic == SETTINGS_MAGIC && sp >= SETPOINT_MIN_TENTHS &&
              sp <= SETPOINT_MAX_TENTHS;
    louvers[i].setpoint = ok ? sp : SETPOINT_DEFAULT_TENTHS;
  }
}

void saveSettings() {
  Settings s;
  s.magic = SETTINGS_MAGIC;
  for (uint8_t i = 0; i < LOUVER_COUNT; i++) s.setpoint[i] = louvers[i].setpoint;
  EEPROM.put(0, s);  // put() only rewrites bytes that changed
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------
volatile int8_t encoderSteps = 0;

void encoderISR() {
  static unsigned long lastUs = 0;
  unsigned long nowUs = micros();
  if (nowUs - lastUs < ENC_DEBOUNCE_US) return;
  lastUs = nowUs;
  encoderSteps += digitalRead(PIN_ENC_B) ? ENC_DIRECTION : -ENC_DIRECTION;
}

int8_t takeEncoderSteps() {
  noInterrupts();
  int8_t steps = encoderSteps;
  encoderSteps = 0;
  interrupts();
  return steps;
}

Button btnEnc = {PIN_ENC_SW, HIGH, HIGH, 0};
Button btnKey = {PIN_KEY_K0, HIGH, HIGH, 0};

// Returns true once per debounced press (active LOW).
bool buttonPressed(Button &b, unsigned long now) {
  bool raw = digitalRead(b.pin);
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changedAt = now;
  }
  if (raw != b.stable && now - b.changedAt >= BUTTON_DEBOUNCE_MS) {
    b.stable = raw;
    return b.stable == LOW;
  }
  return false;
}

// ---------------------------------------------------------------------------
// UI state machine
// ---------------------------------------------------------------------------
const uint8_t MENU_ROWS = 3;            // Setpoints, Manual, Back
const uint8_t LIST_ROWS = LOUVER_COUNT + 1;  // Louvers + Back

Screen screen = SCR_HOME;
int8_t cursor = 0;
bool editing = false;
int16_t editValue = 0;
bool screenDirty = true;

void goTo(Screen s) {
  screen = s;
  cursor = 0;
  editing = false;
  screenDirty = true;
}

void moveCursor(int8_t steps, uint8_t rows) {
  cursor = constrain(cursor + steps, 0, rows - 1);
}

void handleInput(unsigned long now) {
  int8_t steps = takeEncoderSteps();
  bool push = buttonPressed(btnEnc, now);
  bool key = buttonPressed(btnKey, now);

  if (key) {
    if (editing) editing = false;
    else if (screen == SCR_HOME) systemRunning = !systemRunning;
    else goTo(SCR_HOME);
    return;
  }

  switch (screen) {
    case SCR_HOME:
      if (push) goTo(SCR_MENU);
      break;

    case SCR_MENU:
      moveCursor(steps, MENU_ROWS);
      if (push) {
        if (cursor == 0) goTo(SCR_SETPOINT);
        else if (cursor == 1) goTo(SCR_MANUAL);
        else goTo(SCR_HOME);
      }
      break;

    case SCR_SETPOINT:
      if (editing) {
        editValue = constrain(editValue + steps * SETPOINT_STEP_TENTHS,
                              SETPOINT_MIN_TENTHS, SETPOINT_MAX_TENTHS);
        if (push) {
          louvers[cursor].setpoint = editValue;
          saveSettings();
          editing = false;
        }
      } else {
        moveCursor(steps, LIST_ROWS);
        if (push) {
          if (cursor == LOUVER_COUNT) {
            goTo(SCR_MENU);
          } else {
            editing = true;
            editValue = louvers[cursor].setpoint;
          }
        }
      }
      break;

    case SCR_MANUAL:
      moveCursor(steps, LIST_ROWS);
      if (push) {
        if (cursor == LOUVER_COUNT) {
          goTo(SCR_MENU);
        } else {
          Louver &l = louvers[cursor];
          l.mode = (Mode)((l.mode + 1) % 3);
        }
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// Display: rows are cached and only redrawn when their text or style changes,
// which keeps software-SPI updates short.
// ---------------------------------------------------------------------------
Adafruit_ST7735 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_MOSI, PIN_TFT_SCLK, PIN_TFT_RST);

const uint8_t MAX_ROWS  = 5;
const uint8_t ROW_CHARS = 13;  // 13 * 12 px = 156 px at text size 2
const uint8_t ROW_X     = 2;
const uint8_t ROW_Y0    = 16;
const uint8_t ROW_PITCH = 22;
const uint8_t HEADER_H  = 12;

char rowCache[MAX_ROWS][ROW_CHARS + 1];
Style rowStyle[MAX_ROWS];

void drawRow(uint8_t i, const char *text, Style s) {
  char padded[ROW_CHARS + 1];
  uint8_t n = 0;
  while (n < ROW_CHARS && text[n]) {
    padded[n] = text[n];
    n++;
  }
  while (n < ROW_CHARS) padded[n++] = ' ';
  padded[ROW_CHARS] = '\0';

  if (rowStyle[i] == s && strcmp(rowCache[i], padded) == 0) return;
  strcpy(rowCache[i], padded);
  rowStyle[i] = s;

  uint16_t fg = ST77XX_WHITE, bg = ST77XX_BLACK;
  switch (s) {
    case ST_SELECTED: fg = ST77XX_BLACK; bg = ST77XX_CYAN; break;
    case ST_EDIT:     fg = ST77XX_BLACK; bg = ST77XX_YELLOW; break;
    case ST_GOOD:     fg = ST77XX_GREEN; break;
    case ST_WARN:     fg = ST77XX_RED; break;
    default: break;
  }
  tft.setTextSize(2);
  tft.setTextColor(fg, bg);
  tft.setCursor(ROW_X, ROW_Y0 + i * ROW_PITCH);
  tft.print(padded);
}

void drawHeader(const __FlashStringHelper *title) {
  tft.fillRect(0, 0, tft.width(), HEADER_H, ST77XX_BLUE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLUE);
  tft.setCursor(4, 2);
  tft.print(title);
}

void formatTenths(char *out, size_t n, int16_t t) {
  int16_t a = abs(t);
  snprintf(out, n, "%s%d.%d", t < 0 ? "-" : "", a / 10, a % 10);
}

const char *louverStateText(const Louver &l) {
  if (l.drive == DRIVE_OPEN) return "OPENING";
  if (l.drive == DRIVE_CLOSE) return "CLOSING";
  if (l.pos == POS_OPEN) return "OPEN";
  if (l.pos == POS_CLOSED) return "CLOSED";
  return "UNKNOWN";
}

const char *modeText(Mode m) {
  if (m == MODE_FORCE_OPEN) return "OPEN";
  if (m == MODE_FORCE_CLOSED) return "CLOSED";
  return "AUTO";
}

Style listStyle(uint8_t row) {
  if (row != cursor) return ST_NORMAL;
  return editing ? ST_EDIT : ST_SELECTED;
}

void render() {
  if (screenDirty) {
    screenDirty = false;
    tft.fillScreen(ST77XX_BLACK);
    for (uint8_t i = 0; i < MAX_ROWS; i++) rowStyle[i] = ST_NONE;
    switch (screen) {
      case SCR_HOME:     drawHeader(F("HOME   push:menu K0:run")); break;
      case SCR_MENU:     drawHeader(F("MENU          K0:home")); break;
      case SCR_SETPOINT: drawHeader(F("SETPOINTS (open above)")); break;
      case SCR_MANUAL:   drawHeader(F("MANUAL OVERRIDE")); break;
    }
  }

  char buf[ROW_CHARS + 1];
  char num[8];

  switch (screen) {
    case SCR_HOME:
      if (tempValid) {
        formatTenths(num, sizeof num, tempTenths);
        snprintf(buf, sizeof buf, "Temp %sC", num);
        drawRow(0, buf, ST_NORMAL);
      } else {
        drawRow(0, "Sensor ERR", ST_WARN);
      }
      for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
        const Louver &l = louvers[i];
        snprintf(buf, sizeof buf, "L%u %-7s %c", i + 1, louverStateText(l),
                 l.mode == MODE_AUTO ? 'A' : 'M');
        drawRow(i + 1, buf, l.pos == POS_OPEN && l.drive == DRIVE_STOP ? ST_GOOD : ST_NORMAL);
      }
      drawRow(4, systemRunning ? "RUNNING" : "STOPPED (K0)",
              systemRunning ? ST_GOOD : ST_WARN);
      break;

    case SCR_MENU:
      drawRow(0, "Setpoints", listStyle(0));
      drawRow(1, "Manual", listStyle(1));
      drawRow(2, "Back", listStyle(2));
      break;

    case SCR_SETPOINT:
      for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
        int16_t sp = (editing && cursor == i) ? editValue : louvers[i].setpoint;
        formatTenths(num, sizeof num, sp);
        snprintf(buf, sizeof buf, "L%u Set %sC", i + 1, num);
        drawRow(i, buf, listStyle(i));
      }
      drawRow(LOUVER_COUNT, "Back", listStyle(LOUVER_COUNT));
      break;

    case SCR_MANUAL:
      for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
        snprintf(buf, sizeof buf, "L%u: %s", i + 1, modeText(louvers[i].mode));
        drawRow(i, buf, listStyle(i));
      }
      drawRow(LOUVER_COUNT, "Back", listStyle(LOUVER_COUNT));
      break;
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
unsigned long lastFrameMs = 0;

void setup() {
  // Motors first, so every driver input is held LOW before anything else runs.
  for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
    digitalWrite(MOTOR_PINS[i].a, LOW);
    digitalWrite(MOTOR_PINS[i].b, LOW);
    pinMode(MOTOR_PINS[i].a, OUTPUT);
    pinMode(MOTOR_PINS[i].b, OUTPUT);
  }

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_KEY_K0, INPUT_PULLUP);

  loadSettings();
  for (uint8_t i = 0; i < LOUVER_COUNT; i++) {
    Louver &l = louvers[i];
    l.pos = POS_UNKNOWN;
    l.target = POS_CLOSED;  // Home to closed until the sensor says otherwise
    l.drive = DRIVE_STOP;
    l.mode = MODE_AUTO;
    l.changedAt = 0;
  }

  tft.initR(TFT_INIT_TAB);
  tft.setRotation(1);  // Landscape, 160x128
  tft.setTextWrap(false);

  beginSensor();
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, FALLING);
}

void loop() {
  unsigned long now = millis();

  handleInput(now);
  updateTemperature(now);
  updateTargets();
  for (uint8_t i = 0; i < LOUVER_COUNT; i++) updateLouver(i, now);

  if (now - lastFrameMs >= UI_FRAME_MS) {
    lastFrameMs = now;
    render();
  }
}
