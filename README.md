# Chain MIDI Controller for M5 AtomS3R

M5Stack **Chainシリーズ**（Key / Angle / Encoder / Joystick / ToF）をUSB MIDIコントローラーとして使うファームウェアです。  
VRChatやDAWなどのMIDI対応アプリから、ボタン、ノブ、ジョイスティック、距離センサーを外部ハードウェア入力として利用できます。

> Built with [Grok](https://x.ai/grok) by xAI — 設計・実装の相談からコード生成までGrokと一緒に進めました。

---

## Web Installer

Preview版は、デスクトップ版ChromeまたはEdgeからAtomS3Rへ書き込めます。

**[M5ChainMIDI Preview Web Installer](https://shimez.github.io/M5ChainMIDI/installer/preview/)**

> Preview版は開発中のファームウェアです。対応機種、注意事項、実機検証状況をInstaller画面で確認してから使用してください。

Arduino IDEから書き込む場合は、後述の「開発環境」と「使い方」を参照してください。

---

## Demo

ホットプラグ対応の動作デモ（動画付き）:

> M5Stack AtomS3RとChainデバイスを使った簡易MIDIデバイス、ホットプラグに対応させた。  
> 今後は [#M5ChainMIDI](https://x.com/search?q=%23M5ChainMIDI) でまとめる
>
> — [しめじ (@ctake_shimez)](https://x.com/ctake_shimez/status/2085917857997783502) · 2026-08-08

📹 [Xで動画を見る](https://x.com/ctake_shimez/status/2085917857997783502)

関連ポストは [#M5ChainMIDI](https://x.com/search?q=%23M5ChainMIDI&src=hashtag_click) でまとめています。

---

## 特徴

- **USB MIDIデバイス**として動作
- 対応デバイス
  - **Chain Key** — 押下でNote On/Off
  - **Chain Angle** — 回転で絶対CC（0–127）
  - **Chain Encoder** — 回転で相対CC、クリックでNote
  - **Chain Joystick** — X/Y軸で絶対CC、クリックでNote
  - **Chain ToF** — 手や物体までの距離を絶対CCへ変換
- 接続順にNote / CC番号を自動割り当て
- **ホットプラグ対応**（起動後の抜き差しを再スキャンで認識）
- Chain切断・構成変更時に押下中ノートを解放
- Angle / Joystick / ToFの細かな値の揺れを抑制
- AtomS3Rの画面に接続台数と最後の操作を表示

---

## 必要なハードウェア

| 部品 | 用途 |
|------|------|
| [M5 AtomS3R](https://docs.m5stack.com/en/core/AtomS3R) | メインコントローラー |
| [Atomic ToChain Base](https://docs.m5stack.com/en/atom/Atomic%20ToChain%20Base) | Chain Bus接続 |
| Chain Key / Angle / Encoder / Joystick / ToF | 入力デバイス（必要なものだけ） |

接続例:

```text
AtomS3R + Atomic ToChain Base
  └─ Chain Key / Angle / Encoder / Joystick / ToF …（デイジーチェーン可）
```

USBはAtomS3Rの **USBポート（OTG）** 側をPCに接続してください。

Chainデバイスは、マスターから各デバイスのIN、各デバイスのOUTから次のデバイスのINへ接続します。

---

## 開発環境

- Arduino IDEまたはPlatformIO
- ボード: **M5Stack AtomS3R**（またはESP32-S3系でAtomS3R相当）
- ライブラリ
  - [M5Unified](https://github.com/m5stack/M5Unified)
  - [M5Chain](https://github.com/m5stack/M5Chain)
- ボード設定の目安
  - **USB Mode**: USB-OTG (TinyUSB)
  - **USB CDC On Boot**: Enabled（シリアルログ用）

本スケッチはArduino-ESP32標準の`USB.h` / `USBMIDI.h`を使用します。

---

## MIDI割り当て

接続順（同じ種類の中での順番）で番号が決まります。

| デバイス | 操作 | MIDI |
|----------|------|------|
| Chain Key | 押す / 離す | Note **60, 61, 62…** On/Off |
| Chain Angle | 回す | CC **1, 2, 3…** = 0–127（絶対） |
| Chain Encoder | 回す | CC **20, 21, 22…**（相対・64中心） |
| Chain Encoder | クリック | Note **80, 81, 82…** On/Off |
| Chain Joystick | X軸 | CC **40, 42, 44…** = 0–127（絶対） |
| Chain Joystick | Y軸 | CC **41, 43, 45…** = 0–127（絶対） |
| Chain Joystick | クリック | Note **90, 91, 92…** On/Off |
| Chain ToF | 距離 | CC **80, 81, 82…** = 0–127（絶対・近いほど大きい） |

### Encoder相対CC

- **64** — 変化なし
- **65以上** — 右回転（プラス）
- **63以下** — 左回転（マイナス）

VRChatなどでは`value - 64`をデルタとしてスライダーに加算すると扱いやすくなります。

### Chain ToF

既定では、約50～1000mmの距離をCCへ変換します。

| 距離 | CC値 |
|------|------|
| 50mm以下 | 127 |
| 50～1000mm | 127～0 |
| 1000mm以上 | 0 |

値の揺れを抑えるため、平滑化、送信デッドバンド、読み取り周期制限を適用しています。

---

## 使い方

### Web Installerを使う

1. [Preview Web Installer](https://shimez.github.io/M5ChainMIDI/installer/preview/)をデスクトップ版ChromeまたはEdgeで開く
2. AtomS3RをUSBデータケーブルでPCへ接続する
3. `Install M5ChainMIDI Preview`を押す
4. 書き込み対象のAtomS3Rを選択する
5. 書き込みと再起動が完了するまで待つ

### Arduino IDEから書き込む

1. リポジトリのスケッチを開く
2. ライブラリとボード設定を確認する
3. AtomS3Rへ書き込む
4. PCへUSB接続し、MIDIデバイスとして認識されることを確認する
5. Chainデバイスを接続する（起動後でも可）
6. 画面の接続台数とMIDIモニターのNote / CCを確認する

ピン（Atomic ToChain Base + AtomS3R）:

```cpp
RXD_PIN = GPIO 6
TXD_PIN = GPIO 5
```

環境によってTX/RXが逆の場合は入れ替えてください。

---

## VRChatでの利用例

ワールド側で **VRC Midi Listener** を使い、Udonから次を受け取ります。

- `MidiNoteOn` / `MidiNoteOff` — Key / Encoderクリック / Joystickクリック
- `MidiControlChange` — Angle / Encoder回転 / Joystick軸 / ToF距離

例:

- Note 60 → ボタンの`Interact`相当を発火
- CC 1 → スライダーを絶対位置で更新
- CC 20 → `value - 64`を加算して相対操作
- CC 80 → 手を近づけるほどエフェクト量を増やす

ワールド側スクリプトは用途に合わせて別途用意してください。

---

## 画面表示

```text
Chain MIDI
----------------
K2 A1 E1 J1 T1
----------------
Act:
ToF
Val:
CC80=96 280mm
```

- 上段: 認識中の台数（Key / Angle / Encoder / Joystick / ToF）
- 下段: 直近の操作とMIDI値

---

## 設定のカスタマイズ

スケッチ先頭付近の定数で変更できます。

```cpp
MAX_DEVICES                // 最大認識台数
RESCAN_INTERVAL_MS         // ホットプラグ再スキャン間隔
ANALOG_CC_THRESHOLD        // アナログCCの送信デッドバンド
NOTE_KEY_BASE              // KeyのNote開始番号
CC_ANGLE_BASE              // AngleのCC開始番号
CC_ENC_REL_BASE            // Encoder相対CC開始番号
CC_JOY_X_BASE / Y          // Joystick軸CC
NOTE_ENC_BTN_BASE          // EncoderクリックNote
NOTE_JOY_BTN_BASE          // JoystickクリックNote
CC_TOF_BASE                // ToFのCC開始番号
TOF_NEAR_MM / TOF_FAR_MM   // ToFのMIDI操作範囲
TOF_MEASURE_TIME_MS        // ToFの測定時間
TOF_SAMPLE_INTERVAL_MS     // ToFの読み取り周期
TOF_FILTER_STRENGTH        // ToFの平滑化強度
```

---

## トラブルシュート

| 症状 | 確認点 |
|------|--------|
| MIDIとして見えない | USB ModeがOTG (TinyUSB)か / 正しいUSBポートか |
| Web Installerでポートが表示されない | デスクトップ版Chrome / Edgeか、USBデータケーブルか、他のシリアルアプリが閉じているか |
| Chainデバイスが増えない | IN / OUTとケーブル向き、画面の台数表示、シリアルのRescanログ |
| Angle / Joystickの端が足りない | 個体差に合わせて入力範囲やmapを調整 |
| ToFの値が揺れる | `TOF_FILTER_STRENGTH`または`ANALOG_CC_THRESHOLD`を増やす |
| ToFの反応が遅い | `TOF_FILTER_STRENGTH`または`TOF_SAMPLE_INTERVAL_MS`を小さくする |
| ToFが意図した範囲で0～127にならない | `TOF_NEAR_MM`と`TOF_FAR_MM`を設置環境に合わせる |
| TX/RXエラー | `RXD_PIN` / `TXD_PIN`を入れ替える |

---

## ライセンス

MIT License

M5Stack製品名・ライブラリは各権利者に帰属します。

---

## Credits

- Hardware & Chain protocol: [M5Stack](https://m5stack.com/)
- Libraries: M5Unified, M5Chain, Arduino-ESP32 USB MIDI
- Firmware design & implementation assistance: **[Grok](https://x.ai/grok) (xAI)**
- Documentation & Web Installer creation: **[Codex](https://openai.com/codex/) (OpenAI)**

Issueや改善案があればPull Request / Issue歓迎です。
