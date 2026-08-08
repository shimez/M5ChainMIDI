#include <Arduino.h>
#include <M5Unified.h>
#include "USB.h"
#include "USBMIDI.h"
#include "M5Chain.h"

#define RXD_PIN GPIO_NUM_6
#define TXD_PIN GPIO_NUM_5
#define MAX_DEVICES 16

const uint8_t MIDI_CH = 1;

const uint8_t NOTE_KEY_BASE      = 60;
const uint8_t NOTE_ENC_BTN_BASE  = 80;
const uint8_t NOTE_JOY_BTN_BASE  = 90;
const uint8_t CC_ANGLE_BASE      = 1;
const uint8_t CC_ENC_REL_BASE    = 20;
const uint8_t CC_JOY_X_BASE      = 40;  // Joy0:X=40 Y=41, Joy1:X=42 Y=43...
const uint8_t CC_JOY_Y_BASE      = 41;

const int ANGLE_MIN = 0;
const int ANGLE_MAX = 4095;
const uint32_t RESCAN_INTERVAL_MS = 1000;

USBMIDI MIDI;
Chain M5Chain;

uint8_t keyIds[MAX_DEVICES];
uint8_t angleIds[MAX_DEVICES];
uint8_t encoderIds[MAX_DEVICES];
uint8_t joyIds[MAX_DEVICES];
uint8_t keyCount = 0, angleCount = 0, encoderCount = 0, joyCount = 0;

uint8_t lastKeyStatus[MAX_DEVICES];
uint8_t lastEncBtn[MAX_DEVICES];
uint8_t lastJoyBtn[MAX_DEVICES];
uint8_t lastAngleCC[MAX_DEVICES];
uint8_t lastJoyXCC[MAX_DEVICES];
uint8_t lastJoyYCC[MAX_DEVICES];

uint32_t lastRescanMs = 0;
bool chainWasConnected = false;

void drawHeader() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, 2);
  M5.Display.println("Chain MIDI");
  M5.Display.drawFastHLine(0, 14, 128, TFT_DARKGREY);
}

void drawDeviceSummary() {
  M5.Display.fillRect(0, 16, 128, 16, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 18);
  M5.Display.printf("K%d A%d E%d J%d", keyCount, angleCount, encoderCount, joyCount);
  M5.Display.drawFastHLine(0, 32, 128, TFT_DARKGREY);
}

void drawStatus(const char *action, const char *value) {
  M5.Display.fillRect(0, 36, 128, 92, TFT_BLACK);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(2, 38);
  M5.Display.print("Act:");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 50);
  M5.Display.println(action);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(2, 68);
  M5.Display.print("Val:");
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setCursor(2, 80);
  M5.Display.println(value);
}

bool sameIds(const uint8_t *a, const uint8_t *b, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) if (a[i] != b[i]) return false;
  return true;
}

// int8 (-128..127) → MIDI CC (0..127)
uint8_t joyToCC(int8_t v) {
  long x = map((long)v, -128, 127, 0, 127);
  return (uint8_t)constrain(x, 0, 127);
}

bool scanDevices(bool forceRedraw) {
  uint8_t nk[MAX_DEVICES], na[MAX_DEVICES], ne[MAX_DEVICES], nj[MAX_DEVICES];
  uint8_t ck = 0, ca = 0, ce = 0, cj = 0;

  if (!M5Chain.isDeviceConnected()) {
    if (keyCount || angleCount || encoderCount || joyCount || forceRedraw) {
      keyCount = angleCount = encoderCount = joyCount = 0;
      memset(lastKeyStatus, 0, sizeof(lastKeyStatus));
      memset(lastEncBtn, 0, sizeof(lastEncBtn));
      memset(lastJoyBtn, 0, sizeof(lastJoyBtn));
      memset(lastAngleCC, 0xFF, sizeof(lastAngleCC));
      memset(lastJoyXCC, 0xFF, sizeof(lastJoyXCC));
      memset(lastJoyYCC, 0xFF, sizeof(lastJoyYCC));
      drawDeviceSummary();
      drawStatus("No Device", "-");
      return true;
    }
    return false;
  }

  uint16_t device_count = 0;
  if (M5Chain.getDeviceNum(&device_count) != CHAIN_OK || device_count == 0) return false;
  if (device_count > MAX_DEVICES) device_count = MAX_DEVICES;

  device_list_t *list = (device_list_t *)malloc(sizeof(device_list_t));
  if (!list) return false;
  list->count = device_count;
  list->devices = (device_info_t *)malloc(sizeof(device_info_t) * device_count);
  if (!list->devices) { free(list); return false; }

  if (!M5Chain.getDeviceList(list)) {
    free(list->devices); free(list);
    return false;
  }

  for (uint16_t i = 0; i < list->count; i++) {
    uint8_t id = (uint8_t)list->devices[i].id;
    uint16_t type = list->devices[i].device_type;
    if (type == CHAIN_KEY_TYPE_CODE && ck < MAX_DEVICES) nk[ck++] = id;
    else if (type == CHAIN_ANGLE_TYPE_CODE && ca < MAX_DEVICES) na[ca++] = id;
    else if (type == CHAIN_ENCODER_TYPE_CODE && ce < MAX_DEVICES) ne[ce++] = id;
    else if (type == CHAIN_JOYSTICK_TYPE_CODE && cj < MAX_DEVICES) nj[cj++] = id;
  }
  free(list->devices); free(list);

  bool changed =
      (ck != keyCount) || (ca != angleCount) || (ce != encoderCount) || (cj != joyCount) ||
      !sameIds(keyIds, nk, ck) || !sameIds(angleIds, na, ca) ||
      !sameIds(encoderIds, ne, ce) || !sameIds(joyIds, nj, cj);

  if (!changed && !forceRedraw) return false;

  keyCount = ck; angleCount = ca; encoderCount = ce; joyCount = cj;
  memcpy(keyIds, nk, ck);
  memcpy(angleIds, na, ca);
  memcpy(encoderIds, ne, ce);
  memcpy(joyIds, nj, cj);

  memset(lastKeyStatus, 0, sizeof(lastKeyStatus));
  memset(lastEncBtn, 0, sizeof(lastEncBtn));
  memset(lastJoyBtn, 0, sizeof(lastJoyBtn));
  memset(lastAngleCC, 0xFF, sizeof(lastAngleCC));
  memset(lastJoyXCC, 0xFF, sizeof(lastJoyXCC));
  memset(lastJoyYCC, 0xFF, sizeof(lastJoyYCC));

  drawDeviceSummary();
  char buf[28];
  snprintf(buf, sizeof(buf), "K%d A%d E%d J%d", keyCount, angleCount, encoderCount, joyCount);
  drawStatus("Rescan", buf);
  Serial.printf("Rescan K%d A%d E%d J%d\n", keyCount, angleCount, encoderCount, joyCount);
  return true;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(80);

  drawHeader();
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 20);
  M5.Display.println("Init MIDI...");

  MIDI.begin();
  USB.begin();
  M5Chain.begin(&Serial2, 115200, RXD_PIN, TXD_PIN);

  drawHeader();
  drawDeviceSummary();
  drawStatus("Ready", "Hotplug OK");

  scanDevices(true);
  lastRescanMs = millis();
  chainWasConnected = M5Chain.isDeviceConnected();
}

void loop() {
  M5.update();
  char buf[32];

  uint32_t now = millis();
  bool connected = M5Chain.isDeviceConnected();
  if (connected != chainWasConnected || (now - lastRescanMs) >= RESCAN_INTERVAL_MS) {
    scanDevices(false);
    lastRescanMs = now;
    chainWasConnected = connected;
  }

  // ----- Key -----
  for (uint8_t i = 0; i < keyCount; i++) {
    uint8_t st = 0;
    if (M5Chain.getKeyButtonStatus(keyIds[i], &st) != CHAIN_OK) continue;
    uint8_t note = NOTE_KEY_BASE + i;
    if (st && !lastKeyStatus[i]) {
      MIDI.noteOn(note, 100, MIDI_CH);
      snprintf(buf, sizeof(buf), "NoteOn %d", note);
      drawStatus("Key", buf);
    } else if (!st && lastKeyStatus[i]) {
      MIDI.noteOff(note, 0, MIDI_CH);
      snprintf(buf, sizeof(buf), "NoteOff %d", note);
      drawStatus("Key", buf);
    }
    lastKeyStatus[i] = st;
  }

  // ----- Angle -----
  for (uint8_t i = 0; i < angleCount; i++) {
    uint16_t raw = 0;
    if (M5Chain.getAngle12BitAdc(angleIds[i], &raw) != CHAIN_OK) continue;
    int cc = map(constrain(raw, ANGLE_MIN, ANGLE_MAX), ANGLE_MIN, ANGLE_MAX, 0, 127);
    if (cc == lastAngleCC[i]) continue;
    uint8_t ccNum = CC_ANGLE_BASE + i;
    MIDI.controlChange(ccNum, (uint8_t)cc, MIDI_CH);
    lastAngleCC[i] = (uint8_t)cc;
    snprintf(buf, sizeof(buf), "CC%d=%d", ccNum, cc);
    drawStatus("Angle", buf);
  }

  // ----- Encoder -----
  for (uint8_t i = 0; i < encoderCount; i++) {
    int16_t inc = 0;
    if (M5Chain.getEncoderIncValue(encoderIds[i], &inc) == CHAIN_OK && inc != 0) {
      int delta = constrain(inc, -63, 63);
      uint8_t ccNum = CC_ENC_REL_BASE + i;
      MIDI.controlChange(ccNum, (uint8_t)(64 + delta), MIDI_CH);
      snprintf(buf, sizeof(buf), "CC%d inc=%d", ccNum, inc);
      drawStatus("EncRot", buf);
    }
    uint8_t btn = 0;
    if (M5Chain.getEncoderButtonStatus(encoderIds[i], &btn) == CHAIN_OK) {
      uint8_t note = NOTE_ENC_BTN_BASE + i;
      if (btn && !lastEncBtn[i]) {
        MIDI.noteOn(note, 100, MIDI_CH);
        snprintf(buf, sizeof(buf), "NoteOn %d", note);
        drawStatus("EncBtn", buf);
      } else if (!btn && lastEncBtn[i]) {
        MIDI.noteOff(note, 0, MIDI_CH);
        snprintf(buf, sizeof(buf), "NoteOff %d", note);
        drawStatus("EncBtn", buf);
      }
      lastEncBtn[i] = btn;
    }
  }

  // ----- Joystick -----
  for (uint8_t i = 0; i < joyCount; i++) {
    int8_t x8 = 0, y8 = 0;
    // マップ済み -128〜127
    if (M5Chain.getJoystickMappedInt8Value(joyIds[i], &x8, &y8) == CHAIN_OK) {
      uint8_t ccX = joyToCC(x8);
      uint8_t ccY = joyToCC(y8);
      uint8_t ccNumX = CC_JOY_X_BASE + i * 2;
      uint8_t ccNumY = CC_JOY_Y_BASE + i * 2;

      if (ccX != lastJoyXCC[i]) {
        MIDI.controlChange(ccNumX, ccX, MIDI_CH);
        lastJoyXCC[i] = ccX;
        snprintf(buf, sizeof(buf), "X CC%d=%d", ccNumX, ccX);
        drawStatus("Joy", buf);
      }
      if (ccY != lastJoyYCC[i]) {
        MIDI.controlChange(ccNumY, ccY, MIDI_CH);
        lastJoyYCC[i] = ccY;
        snprintf(buf, sizeof(buf), "Y CC%d=%d", ccNumY, ccY);
        drawStatus("Joy", buf);
      }
    }

    uint8_t btn = 0;
    if (M5Chain.getJoystickButtonStatus(joyIds[i], &btn) == CHAIN_OK) {
      uint8_t note = NOTE_JOY_BTN_BASE + i;
      if (btn && !lastJoyBtn[i]) {
        MIDI.noteOn(note, 100, MIDI_CH);
        snprintf(buf, sizeof(buf), "NoteOn %d", note);
        drawStatus("JoyBtn", buf);
      } else if (!btn && lastJoyBtn[i]) {
        MIDI.noteOff(note, 0, MIDI_CH);
        snprintf(buf, sizeof(buf), "NoteOff %d", note);
        drawStatus("JoyBtn", buf);
      }
      lastJoyBtn[i] = btn;
    }
  }

  delay(10);
}