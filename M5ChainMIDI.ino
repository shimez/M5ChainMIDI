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
const uint8_t CC_JOY_X_BASE      = 40;
const uint8_t CC_JOY_Y_BASE      = 41;

const int ANGLE_MIN = 0;
const int ANGLE_MAX = 4095;

// 前回送信値との差がこの値以上ならCCを送信
const uint8_t ANALOG_CC_THRESHOLD = 2;

const uint32_t RESCAN_INTERVAL_MS = 1000;

USBMIDI MIDI;
Chain M5Chain;

uint8_t keyIds[MAX_DEVICES];
uint8_t angleIds[MAX_DEVICES];
uint8_t encoderIds[MAX_DEVICES];
uint8_t joyIds[MAX_DEVICES];

uint8_t keyCount = 0;
uint8_t angleCount = 0;
uint8_t encoderCount = 0;
uint8_t joyCount = 0;

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
  M5.Display.printf(
      "K%d A%d E%d J%d",
      keyCount,
      angleCount,
      encoderCount,
      joyCount
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

bool sameIds(const uint8_t* a, const uint8_t* b, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }

  return true;
}

// int8_t（-128～127）をMIDI CC（0～127）へ変換
uint8_t joyToCC(int8_t value) {
  long mapped = map((long)value, -128, 127, 0, 127);
  return (uint8_t)constrain(mapped, 0, 127);
}

// 前回送信値との差がしきい値以上か判定する
// 0xFFは未送信状態を表す
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

// 現在押下中として記録されている全ノートを解放する
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

void resetInputStates() {
  memset(lastKeyStatus, 0, sizeof(lastKeyStatus));
  memset(lastEncBtn, 0, sizeof(lastEncBtn));
  memset(lastJoyBtn, 0, sizeof(lastJoyBtn));

  memset(lastAngleCC, 0xFF, sizeof(lastAngleCC));
  memset(lastJoyXCC, 0xFF, sizeof(lastJoyXCC));
  memset(lastJoyYCC, 0xFF, sizeof(lastJoyYCC));
}

bool scanDevices(bool forceRedraw) {
  uint8_t newKeyIds[MAX_DEVICES];
  uint8_t newAngleIds[MAX_DEVICES];
  uint8_t newEncoderIds[MAX_DEVICES];
  uint8_t newJoyIds[MAX_DEVICES];

  uint8_t newKeyCount = 0;
  uint8_t newAngleCount = 0;
  uint8_t newEncoderCount = 0;
  uint8_t newJoyCount = 0;

  if (!M5Chain.isDeviceConnected()) {
    bool hadDevices =
        keyCount ||
        angleCount ||
        encoderCount ||
        joyCount;

    if (hadDevices || forceRedraw) {
      // 押したまま切断した場合のノート鳴りっぱなしを防止
      releaseAllNotes();

      keyCount = 0;
      angleCount = 0;
      encoderCount = 0;
      joyCount = 0;

      resetInputStates();

      drawDeviceSummary();
      drawStatus("No Device", "-");

      Serial.println("Chain disconnected");
      return true;
    }

    return false;
  }

  uint16_t deviceCount = 0;

  // getDeviceNum()はchain_status_tを返す
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
  list->devices =
      (device_info_t*)malloc(
          sizeof(device_info_t) * deviceCount
      );

  if (!list->devices) {
    Serial.println("Rescan failed: device allocation");
    free(list);
    return false;
  }

  // getDeviceList()はboolを返す
  // true = 成功、false = 失敗
  if (!M5Chain.getDeviceList(list)) {
    Serial.println("Rescan failed: getDeviceList");
    free(list->devices);
    free(list);
    return false;
  }

  for (uint16_t i = 0; i < list->count; i++) {
    uint8_t id = (uint8_t)list->devices[i].id;
    uint16_t type = list->devices[i].device_type;

    if (
        type == CHAIN_KEY_TYPE_CODE &&
        newKeyCount < MAX_DEVICES
    ) {
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
    }
  }

  free(list->devices);
  free(list);

  bool changed =
      newKeyCount != keyCount ||
      newAngleCount != angleCount ||
      newEncoderCount != encoderCount ||
      newJoyCount != joyCount ||
      !sameIds(keyIds, newKeyIds, newKeyCount) ||
      !sameIds(angleIds, newAngleIds, newAngleCount) ||
      !sameIds(encoderIds, newEncoderIds, newEncoderCount) ||
      !sameIds(joyIds, newJoyIds, newJoyCount);

  if (!changed && !forceRedraw) {
    return false;
  }

  // 構成を置き換える前に、旧構成の押下中ノートを解放
  releaseAllNotes();

  keyCount = newKeyCount;
  angleCount = newAngleCount;
  encoderCount = newEncoderCount;
  joyCount = newJoyCount;

  memcpy(keyIds, newKeyIds, newKeyCount);
  memcpy(angleIds, newAngleIds, newAngleCount);
  memcpy(encoderIds, newEncoderIds, newEncoderCount);
  memcpy(joyIds, newJoyIds, newJoyCount);

  resetInputStates();

  drawDeviceSummary();

  char buffer[28];

  snprintf(
      buffer,
      sizeof(buffer),
      "K%d A%d E%d J%d",
      keyCount,
      angleCount,
      encoderCount,
      joyCount
  );

  drawStatus("Rescan", buffer);

  Serial.printf(
      "Rescan K%d A%d E%d J%d\n",
      keyCount,
      angleCount,
      encoderCount,
      joyCount
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

  M5Chain.begin(
      &Serial2,
      115200,
      RXD_PIN,
      TXD_PIN
  );

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
      (now - lastRescanMs) >= RESCAN_INTERVAL_MS
  ) {
    scanDevices(false);

    lastRescanMs = now;
    chainWasConnected = connected;
  }

  // ----- Key -----

  for (uint8_t i = 0; i < keyCount; i++) {
    uint8_t status = 0;

    if (
        M5Chain.getKeyButtonStatus(
            keyIds[i],
            &status
        ) != CHAIN_OK
    ) {
      continue;
    }

    uint8_t note = NOTE_KEY_BASE + i;

    if (status && !lastKeyStatus[i]) {
      MIDI.noteOn(note, 100, MIDI_CH);

      snprintf(
          buffer,
          sizeof(buffer),
          "NoteOn %d",
          note
      );

      drawStatus("Key", buffer);
    } else if (!status && lastKeyStatus[i]) {
      MIDI.noteOff(note, 0, MIDI_CH);

      snprintf(
          buffer,
          sizeof(buffer),
          "NoteOff %d",
          note
      );

      drawStatus("Key", buffer);
    }

    lastKeyStatus[i] = status;
  }

  // ----- Angle -----

  for (uint8_t i = 0; i < angleCount; i++) {
    uint16_t raw = 0;

    if (
        M5Chain.getAngle12BitAdc(
            angleIds[i],
            &raw
        ) != CHAIN_OK
    ) {
      continue;
    }

    int ccValue = map(
        constrain(raw, ANGLE_MIN, ANGLE_MAX),
        ANGLE_MIN,
        ANGLE_MAX,
        0,
        127
    );

    uint8_t currentCC = (uint8_t)ccValue;

    // ADCノイズによる細かな変化は送信しない
    if (
        !analogCCChanged(
            currentCC,
            lastAngleCC[i]
        )
    ) {
      continue;
    }

    uint8_t ccNumber = CC_ANGLE_BASE + i;

    MIDI.controlChange(
        ccNumber,
        currentCC,
        MIDI_CH
    );

    lastAngleCC[i] = currentCC;

    snprintf(
        buffer,
        sizeof(buffer),
        "CC%d=%d",
        ccNumber,
        currentCC
    );

    drawStatus("Angle", buffer);
  }

  // ----- Encoder -----

  for (uint8_t i = 0; i < encoderCount; i++) {
    int16_t increment = 0;

    if (
        M5Chain.getEncoderIncValue(
            encoderIds[i],
            &increment
        ) == CHAIN_OK &&
        increment != 0
    ) {
      int delta = constrain(increment, -63, 63);
      uint8_t ccNumber = CC_ENC_REL_BASE + i;

      MIDI.controlChange(
          ccNumber,
          (uint8_t)(64 + delta),
          MIDI_CH
      );

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
        M5Chain.getEncoderButtonStatus(
            encoderIds[i],
            &button
        ) == CHAIN_OK
    ) {
      uint8_t note = NOTE_ENC_BTN_BASE + i;

      if (button && !lastEncBtn[i]) {
        MIDI.noteOn(note, 100, MIDI_CH);

        snprintf(
            buffer,
            sizeof(buffer),
            "NoteOn %d",
            note
        );

        drawStatus("EncBtn", buffer);
      } else if (!button && lastEncBtn[i]) {
        MIDI.noteOff(note, 0, MIDI_CH);

        snprintf(
            buffer,
            sizeof(buffer),
            "NoteOff %d",
            note
        );

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
        M5Chain.getJoystickMappedInt8Value(
            joyIds[i],
            &x,
            &y
        ) == CHAIN_OK
    ) {
      uint8_t currentX = joyToCC(x);
      uint8_t currentY = joyToCC(y);

      uint8_t ccNumberX =
          CC_JOY_X_BASE + i * 2;

      uint8_t ccNumberY =
          CC_JOY_Y_BASE + i * 2;

      // X軸の細かな揺れを除外
      if (
          analogCCChanged(
              currentX,
              lastJoyXCC[i]
          )
      ) {
        MIDI.controlChange(
            ccNumberX,
            currentX,
            MIDI_CH
        );

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

      // Y軸の細かな揺れを除外
      if (
          analogCCChanged(
              currentY,
              lastJoyYCC[i]
          )
      ) {
        MIDI.controlChange(
            ccNumberY,
            currentY,
            MIDI_CH
        );

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
        M5Chain.getJoystickButtonStatus(
            joyIds[i],
            &button
        ) == CHAIN_OK
    ) {
      uint8_t note = NOTE_JOY_BTN_BASE + i;

      if (button && !lastJoyBtn[i]) {
        MIDI.noteOn(note, 100, MIDI_CH);

        snprintf(
            buffer,
            sizeof(buffer),
            "NoteOn %d",
            note
        );

        drawStatus("JoyBtn", buffer);
      } else if (!button && lastJoyBtn[i]) {
        MIDI.noteOff(note, 0, MIDI_CH);

        snprintf(
            buffer,
            sizeof(buffer),
            "NoteOff %d",
            note
        );

        drawStatus("JoyBtn", buffer);
      }

      lastJoyBtn[i] = button;
    }
  }

  delay(10);
}