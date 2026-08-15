#include <Arduino.h>
#include <M5Unified.h>
#include "USB.h"
#include "USBMIDI.h"
#include "M5Chain.h"

#define RXD_PIN GPIO_NUM_6
#define TXD_PIN GPIO_NUM_5
#define MAX_DEVICES 16

const uint8_t MIDI_CH = 1;
const char FIRMWARE_VERSION[] = "1.1.0";

const uint8_t NOTE_KEY_BASE     = 60;
const uint8_t NOTE_ENC_BTN_BASE = 80;
const uint8_t NOTE_JOY_BTN_BASE = 90;

const uint8_t CC_ANGLE_BASE   = 1;
const uint8_t CC_ENC_REL_BASE = 20;
const uint8_t CC_JOY_X_BASE   = 40;
const uint8_t CC_JOY_Y_BASE   = 41;
const uint8_t CC_TOF_BASE     = 80;

const int ANGLE_MIN = 0;
const int ANGLE_MAX = 4095;

// 前回送信したCC値との差がこの値以上なら送信する。
const uint8_t ANALOG_CC_THRESHOLD = 2;

// ----- Chain ToF -----

// 30mm以下をCC 127、200mmをCC 0へ変換する。
// 200mm以上を検出したときはCC 0を一度だけ送信し、
// 再び200mm未満へ戻るまで送信を停止する。
const uint16_t TOF_SENSOR_MIN_MM = 30;
const uint16_t TOF_NEAR_MM       = 30;
const uint16_t TOF_FAR_MM        = 200;

// M5Chainの設定可能範囲は20～200ms。
const uint16_t TOF_MEASURE_TIME_MS = 33;
const uint32_t TOF_SAMPLE_INTERVAL_MS = 40;

// 大きいほど滑らかになるが反応は遅くなる。
const uint8_t TOF_FILTER_STRENGTH = 4;

const uint32_t RESCAN_INTERVAL_MS = 1000;

USBMIDI MIDI;
Chain M5Chain;

uint8_t keyIds[MAX_DEVICES];
uint8_t angleIds[MAX_DEVICES];
uint8_t encoderIds[MAX_DEVICES];
uint8_t joyIds[MAX_DEVICES];
uint8_t tofIds[MAX_DEVICES];

uint8_t keyCount = 0;
uint8_t angleCount = 0;
uint8_t encoderCount = 0;
uint8_t joyCount = 0;
uint8_t tofCount = 0;

uint8_t lastKeyStatus[MAX_DEVICES];
uint8_t lastEncBtn[MAX_DEVICES];
uint8_t lastJoyBtn[MAX_DEVICES];
uint8_t lastAngleCC[MAX_DEVICES];
uint8_t lastJoyXCC[MAX_DEVICES];
uint8_t lastJoyYCC[MAX_DEVICES];

uint8_t lastToFCC[MAX_DEVICES];
uint16_t filteredToFDistance[MAX_DEVICES];
uint32_t lastToFSampleMs[MAX_DEVICES];
bool tofFilterInitialized[MAX_DEVICES];
bool tofOutOfRange[MAX_DEVICES];

uint32_t lastRescanMs = 0;
bool chainWasConnected = false;

void drawHeader() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, 2);
  M5.Display.printf("Chain MIDI v%s\n", FIRMWARE_VERSION);
  M5.Display.drawFastHLine(0, 14, 128, TFT_DARKGREY);
}

void drawDeviceSummary() {
  M5.Display.fillRect(0, 16, 128, 16, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 18);
  M5.Display.printf(
      "K%d A%d E%d J%d T%d",
      keyCount,
      angleCount,
      encoderCount,
      joyCount,
      tofCount
  );
  M5.Display.drawFastHLine(0, 32, 128, TFT_DARKGREY);
}

void drawStatus(const char* action, const char* value) {
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

bool sameIds(const uint8_t* first, const uint8_t* second, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    if (first[i] != second[i]) {
      return false;
    }
  }
  return true;
}

uint8_t joyToCC(int8_t value) {
  long mapped = map((long)value, -128, 127, 0, 127);
  return (uint8_t)constrain(mapped, 0, 127);
}

bool analogCCChanged(
    uint8_t current,
    uint8_t previous,
    uint8_t threshold = ANALOG_CC_THRESHOLD
) {
  if (previous == 0xFF) {
    return true;
  }

  int difference = (int)current - (int)previous;
  if (difference < 0) {
    difference = -difference;
  }
  return difference >= threshold;
}

// 30mm以下: CC 127
// 200mm:    CC 0
uint8_t tofDistanceToCC(uint16_t distanceMm) {
  distanceMm = constrain(distanceMm, TOF_NEAR_MM, TOF_FAR_MM);

  long cc = map(
      distanceMm,
      TOF_NEAR_MM,
      TOF_FAR_MM,
      127,
      0
  );

  return (uint8_t)constrain(cc, 0, 127);
}

void releaseAllNotes() {
  for (uint8_t i = 0; i < keyCount; i++) {
    if (lastKeyStatus[i]) {
      MIDI.noteOff(NOTE_KEY_BASE + i, 0, MIDI_CH);
    }
  }

  for (uint8_t i = 0; i < encoderCount; i++) {
    if (lastEncBtn[i]) {
      MIDI.noteOff(NOTE_ENC_BTN_BASE + i, 0, MIDI_CH);
    }
  }

  for (uint8_t i = 0; i < joyCount; i++) {
    if (lastJoyBtn[i]) {
      MIDI.noteOff(NOTE_JOY_BTN_BASE + i, 0, MIDI_CH);
    }
  }
}

void resetToFStates() {
  memset(lastToFCC, 0xFF, sizeof(lastToFCC));
  memset(filteredToFDistance, 0, sizeof(filteredToFDistance));
  memset(lastToFSampleMs, 0, sizeof(lastToFSampleMs));
  memset(tofFilterInitialized, 0, sizeof(tofFilterInitialized));
  memset(tofOutOfRange, 0, sizeof(tofOutOfRange));
}

void resetInputStates() {
  memset(lastKeyStatus, 0, sizeof(lastKeyStatus));
  memset(lastEncBtn, 0, sizeof(lastEncBtn));
  memset(lastJoyBtn, 0, sizeof(lastJoyBtn));
  memset(lastAngleCC, 0xFF, sizeof(lastAngleCC));
  memset(lastJoyXCC, 0xFF, sizeof(lastJoyXCC));
  memset(lastJoyYCC, 0xFF, sizeof(lastJoyYCC));
  resetToFStates();
}

void configureToFDevices() {
  for (uint8_t i = 0; i < tofCount; i++) {
    uint8_t operationStatus = 0;

    chain_status_t result = M5Chain.setToFMeasureTime(
        tofIds[i],
        TOF_MEASURE_TIME_MS,
        &operationStatus
    );

    if (result != CHAIN_OK || !operationStatus) {
      Serial.printf(
          "ToF ID %u: set measure time failed status=%d operation=%u\n",
          tofIds[i],
          result,
          operationStatus
      );
    }

    operationStatus = 0;
    result = M5Chain.setToFMeasureMode(
        tofIds[i],
        CHAIN_TOF_MODE_CONTINUOUS,
        &operationStatus
    );

    if (result != CHAIN_OK || !operationStatus) {
      Serial.printf(
          "ToF ID %u: set continuous mode failed status=%d operation=%u\n",
          tofIds[i],
          result,
          operationStatus
      );
    }
  }
}

bool scanDevices(bool forceRedraw) {
  uint8_t newKeyIds[MAX_DEVICES];
  uint8_t newAngleIds[MAX_DEVICES];
  uint8_t newEncoderIds[MAX_DEVICES];
  uint8_t newJoyIds[MAX_DEVICES];
  uint8_t newToFIds[MAX_DEVICES];

  uint8_t newKeyCount = 0;
  uint8_t newAngleCount = 0;
  uint8_t newEncoderCount = 0;
  uint8_t newJoyCount = 0;
  uint8_t newToFCount = 0;

  if (!M5Chain.isDeviceConnected()) {
    bool hadDevices =
        keyCount || angleCount || encoderCount || joyCount || tofCount;

    if (hadDevices || forceRedraw) {
      releaseAllNotes();

      keyCount = 0;
      angleCount = 0;
      encoderCount = 0;
      joyCount = 0;
      tofCount = 0;

      resetInputStates();
      drawDeviceSummary();
      drawStatus("No Device", "-");

      Serial.println("Chain disconnected");
      return true;
    }
    return false;
  }

  uint16_t deviceCount = 0;
  if (M5Chain.getDeviceNum(&deviceCount) != CHAIN_OK) {
    Serial.println("Rescan failed: getDeviceNum");
    return false;
  }

  if (deviceCount == 0) {
    return false;
  }

  if (deviceCount > MAX_DEVICES) {
    deviceCount = MAX_DEVICES;
  }

  device_list_t* list =
      (device_list_t*)malloc(sizeof(device_list_t));

  if (!list) {
    Serial.println("Rescan failed: list allocation");
    return false;
  }

  list->count = deviceCount;
  list->devices = (device_info_t*)malloc(
      sizeof(device_info_t) * deviceCount
  );

  if (!list->devices) {
    Serial.println("Rescan failed: device allocation");
    free(list);
    return false;
  }

  // getDeviceList()はboolを返す。
  if (!M5Chain.getDeviceList(list)) {
    Serial.println("Rescan failed: getDeviceList");
    free(list->devices);
    free(list);
    return false;
  }

  for (uint16_t i = 0; i < list->count; i++) {
    uint8_t id = (uint8_t)list->devices[i].id;
    uint16_t type = list->devices[i].device_type;

    if (type == CHAIN_KEY_TYPE_CODE && newKeyCount < MAX_DEVICES) {
      newKeyIds[newKeyCount++] = id;
    } else if (
        type == CHAIN_ANGLE_TYPE_CODE &&
        newAngleCount < MAX_DEVICES
    ) {
      newAngleIds[newAngleCount++] = id;
    } else if (
        type == CHAIN_ENCODER_TYPE_CODE &&
        newEncoderCount < MAX_DEVICES
    ) {
      newEncoderIds[newEncoderCount++] = id;
    } else if (
        type == CHAIN_JOYSTICK_TYPE_CODE &&
        newJoyCount < MAX_DEVICES
    ) {
      newJoyIds[newJoyCount++] = id;
    } else if (
        type == CHAIN_TOF_TYPE_CODE &&
        newToFCount < MAX_DEVICES
    ) {
      newToFIds[newToFCount++] = id;
    }
  }

  free(list->devices);
  free(list);

  bool changed =
      newKeyCount != keyCount ||
      newAngleCount != angleCount ||
      newEncoderCount != encoderCount ||
      newJoyCount != joyCount ||
      newToFCount != tofCount ||
      !sameIds(keyIds, newKeyIds, newKeyCount) ||
      !sameIds(angleIds, newAngleIds, newAngleCount) ||
      !sameIds(encoderIds, newEncoderIds, newEncoderCount) ||
      !sameIds(joyIds, newJoyIds, newJoyCount) ||
      !sameIds(tofIds, newToFIds, newToFCount);

  if (!changed && !forceRedraw) {
    return false;
  }

  releaseAllNotes();

  keyCount = newKeyCount;
  angleCount = newAngleCount;
  encoderCount = newEncoderCount;
  joyCount = newJoyCount;
  tofCount = newToFCount;

  memcpy(keyIds, newKeyIds, newKeyCount);
  memcpy(angleIds, newAngleIds, newAngleCount);
  memcpy(encoderIds, newEncoderIds, newEncoderCount);
  memcpy(joyIds, newJoyIds, newJoyCount);
  memcpy(tofIds, newToFIds, newToFCount);

  resetInputStates();
  configureToFDevices();

  drawDeviceSummary();

  char buffer[32];
  snprintf(
      buffer,
      sizeof(buffer),
      "K%d A%d E%d J%d T%d",
      keyCount,
      angleCount,
      encoderCount,
      joyCount,
      tofCount
  );

  drawStatus("Rescan", buffer);
  Serial.printf(
      "Rescan K%d A%d E%d J%d T%d\n",
      keyCount,
      angleCount,
      encoderCount,
      joyCount,
      tofCount
  );

  return true;
}

void setup() {
  auto config = M5.config();
  M5.begin(config);

  M5.Display.setRotation(3);
  M5.Display.setBrightness(80);

  drawHeader();
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(2, 20);
  M5.Display.println("Init MIDI...");

  MIDI.begin();
  USB.begin();

  M5Chain.begin(&Serial2, 115200, RXD_PIN, TXD_PIN);

  resetInputStates();

  drawHeader();
  drawDeviceSummary();
  drawStatus("Ready", "Hotplug OK");

  scanDevices(true);

  lastRescanMs = millis();
  chainWasConnected = M5Chain.isDeviceConnected();
}

void loop() {
  M5.update();

  char buffer[32];
  uint32_t now = millis();
  bool connected = M5Chain.isDeviceConnected();

  if (
      connected != chainWasConnected ||
      now - lastRescanMs >= RESCAN_INTERVAL_MS
  ) {
    scanDevices(false);
    lastRescanMs = now;
    chainWasConnected = connected;
  }

  // ----- Key -----

  for (uint8_t i = 0; i < keyCount; i++) {
    uint8_t status = 0;

    if (M5Chain.getKeyButtonStatus(keyIds[i], &status) != CHAIN_OK) {
      continue;
    }

    uint8_t note = NOTE_KEY_BASE + i;

    if (status && !lastKeyStatus[i]) {
      MIDI.noteOn(note, 100, MIDI_CH);
      snprintf(buffer, sizeof(buffer), "NoteOn %d", note);
      drawStatus("Key", buffer);
    } else if (!status && lastKeyStatus[i]) {
      MIDI.noteOff(note, 0, MIDI_CH);
      snprintf(buffer, sizeof(buffer), "NoteOff %d", note);
      drawStatus("Key", buffer);
    }

    lastKeyStatus[i] = status;
  }

  // ----- Angle -----

  for (uint8_t i = 0; i < angleCount; i++) {
    uint16_t raw = 0;

    if (M5Chain.getAngle12BitAdc(angleIds[i], &raw) != CHAIN_OK) {
      continue;
    }

    uint8_t currentCC = (uint8_t)map(
        constrain(raw, ANGLE_MIN, ANGLE_MAX),
        ANGLE_MIN,
        ANGLE_MAX,
        0,
        127
    );

    if (!analogCCChanged(currentCC, lastAngleCC[i])) {
      continue;
    }

    uint8_t ccNumber = CC_ANGLE_BASE + i;
    MIDI.controlChange(ccNumber, currentCC, MIDI_CH);
    lastAngleCC[i] = currentCC;

    snprintf(buffer, sizeof(buffer), "CC%d=%d", ccNumber, currentCC);
    drawStatus("Angle", buffer);
  }

  // ----- Encoder -----

  for (uint8_t i = 0; i < encoderCount; i++) {
    int16_t increment = 0;

    if (
        M5Chain.getEncoderIncValue(encoderIds[i], &increment) == CHAIN_OK &&
        increment != 0
    ) {
      int delta = constrain(increment, -63, 63);
      uint8_t ccNumber = CC_ENC_REL_BASE + i;

      MIDI.controlChange(ccNumber, (uint8_t)(64 + delta), MIDI_CH);
      snprintf(
          buffer,
          sizeof(buffer),
          "CC%d inc=%d",
          ccNumber,
          increment
      );
      drawStatus("EncRot", buffer);
    }

    uint8_t button = 0;
    if (
        M5Chain.getEncoderButtonStatus(encoderIds[i], &button) == CHAIN_OK
    ) {
      uint8_t note = NOTE_ENC_BTN_BASE + i;

      if (button && !lastEncBtn[i]) {
        MIDI.noteOn(note, 100, MIDI_CH);
        snprintf(buffer, sizeof(buffer), "NoteOn %d", note);
        drawStatus("EncBtn", buffer);
      } else if (!button && lastEncBtn[i]) {
        MIDI.noteOff(note, 0, MIDI_CH);
        snprintf(buffer, sizeof(buffer), "NoteOff %d", note);
        drawStatus("EncBtn", buffer);
      }

      lastEncBtn[i] = button;
    }
  }

  // ----- Joystick -----

  for (uint8_t i = 0; i < joyCount; i++) {
    int8_t x = 0;
    int8_t y = 0;

    if (
        M5Chain.getJoystickMappedInt8Value(joyIds[i], &x, &y) == CHAIN_OK
    ) {
      uint8_t currentX = joyToCC(x);
      uint8_t currentY = joyToCC(y);
      uint8_t ccNumberX = CC_JOY_X_BASE + i * 2;
      uint8_t ccNumberY = CC_JOY_Y_BASE + i * 2;

      if (analogCCChanged(currentX, lastJoyXCC[i])) {
        MIDI.controlChange(ccNumberX, currentX, MIDI_CH);
        lastJoyXCC[i] = currentX;
        snprintf(
            buffer,
            sizeof(buffer),
            "X CC%d=%d",
            ccNumberX,
            currentX
        );
        drawStatus("Joy", buffer);
      }

      if (analogCCChanged(currentY, lastJoyYCC[i])) {
        MIDI.controlChange(ccNumberY, currentY, MIDI_CH);
        lastJoyYCC[i] = currentY;
        snprintf(
            buffer,
            sizeof(buffer),
            "Y CC%d=%d",
            ccNumberY,
            currentY
        );
        drawStatus("Joy", buffer);
      }
    }

    uint8_t button = 0;
    if (
        M5Chain.getJoystickButtonStatus(joyIds[i], &button) == CHAIN_OK
    ) {
      uint8_t note = NOTE_JOY_BTN_BASE + i;

      if (button && !lastJoyBtn[i]) {
        MIDI.noteOn(note, 100, MIDI_CH);
        snprintf(buffer, sizeof(buffer), "NoteOn %d", note);
        drawStatus("JoyBtn", buffer);
      } else if (!button && lastJoyBtn[i]) {
        MIDI.noteOff(note, 0, MIDI_CH);
        snprintf(buffer, sizeof(buffer), "NoteOff %d", note);
        drawStatus("JoyBtn", buffer);
      }

      lastJoyBtn[i] = button;
    }
  }

  // ----- ToF -----

  for (uint8_t i = 0; i < tofCount; i++) {
    if (now - lastToFSampleMs[i] < TOF_SAMPLE_INTERVAL_MS) {
      continue;
    }

    lastToFSampleMs[i] = now;

    uint16_t rawDistance = 0;
    if (
        M5Chain.getToFDistance(tofIds[i], &rawDistance) != CHAIN_OK
    ) {
      continue;
    }

    // 0は不正値として扱い、MIDI CCを送信しない。
    if (rawDistance == 0) {
      tofFilterInitialized[i] = false;
      continue;
    }

    // 最大操作距離以上を初めて検出したときだけCC 0を送信する。
    // 以降は範囲内へ戻るまでMIDI送信を停止する。
    if (rawDistance >= TOF_FAR_MM) {
      if (!tofOutOfRange[i]) {
        uint8_t ccNumber = CC_TOF_BASE + i;

        MIDI.controlChange(ccNumber, 0, MIDI_CH);
        lastToFCC[i] = 0;
        tofOutOfRange[i] = true;

        snprintf(
            buffer,
            sizeof(buffer),
            "CC%d=0 OUT",
            ccNumber
        );
        drawStatus("ToF", buffer);
      }

      tofFilterInitialized[i] = false;
      continue;
    }

    // 範囲内へ戻った直後は、現在値から平滑化とCC送信を再開する。
    if (tofOutOfRange[i]) {
      tofOutOfRange[i] = false;
      tofFilterInitialized[i] = false;
      lastToFCC[i] = 0xFF;
    }

    if (rawDistance < TOF_SENSOR_MIN_MM) {
      rawDistance = TOF_SENSOR_MIN_MM;
    }

    // 最小距離では平滑化せず、CC 127へ確実に到達させる。
    if (rawDistance <= TOF_NEAR_MM) {
      filteredToFDistance[i] = TOF_NEAR_MM;
      tofFilterInitialized[i] = true;
    } else if (!tofFilterInitialized[i]) {
      filteredToFDistance[i] = rawDistance;
      tofFilterInitialized[i] = true;
    } else {
      filteredToFDistance[i] =
          (
              (uint32_t)filteredToFDistance[i] *
                  (TOF_FILTER_STRENGTH - 1) +
              rawDistance
          ) /
          TOF_FILTER_STRENGTH;
    }

    uint8_t currentCC = tofDistanceToCC(filteredToFDistance[i]);

    if (!analogCCChanged(currentCC, lastToFCC[i])) {
      continue;
    }

    uint8_t ccNumber = CC_TOF_BASE + i;
    MIDI.controlChange(ccNumber, currentCC, MIDI_CH);
    lastToFCC[i] = currentCC;

    snprintf(
        buffer,
        sizeof(buffer),
        "CC%d=%d %umm",
        ccNumber,
        currentCC,
        filteredToFDistance[i]
    );
    drawStatus("ToF", buffer);
  }

  delay(10);
}
