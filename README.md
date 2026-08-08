# Chain MIDI Controller for M5 AtomS3R

M5Stack **Chain シリーズ**（Key / Angle / Encoder / Joystick）を USB MIDI コントローラとして使うファームウェアです。  
VRChat などの MIDI 対応アプリから、ボタン・スライダー・相対回転などを外部ハードウェアで操作できます。

> Built with [Grok](https://x.ai/grok) by xAI — 設計・実装の相談からコード生成まで Grok と一緒に進めました。

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

- **USB MIDI デバイス**として動作（追加ドライバ不要な環境が多いです）
- 対応デバイス
  - **Chain Key** … 押下 → Note On/Off
  - **Chain Angle** … 回転 → 絶対 CC（0–127）
  - **Chain Encoder** … 回転 → 相対 CC / クリック → Note
  - **Chain Joystick** … X/Y → 絶対 CC / クリック → Note
- **任意台数**対応（接続順に Note / CC 番号を自動割当）
- **ホットプラグ**対応（起動後の抜き差しを再スキャンで認識）
- AtomS3R の画面に接続台数と最後の操作を表示

---

## 必要なハードウェア

| 部品 | 用途 |
|------|------|
| [M5 AtomS3R](https://docs.m5stack.com/en/core/AtomS3R) | メインコントローラ |
| [Atomic ToChain Base](https://docs.m5stack.com/en/atom/Atomic%20ToChain%20Base) | Chain バス接続 |
| Chain Key / Angle / Encoder / Joystick | 入力デバイス（必要なものだけ） |

接続例:

```
AtomS3R + Atomic ToChain Base
  └─ Chain Key / Angle / Encoder / Joystick …（デイジーチェーン可）
```

USB は AtomS3R の **USB ポート（OTG）** 側を PC に接続してください。

---

## 開発環境

- Arduino IDE または PlatformIO
- ボード: **M5Stack AtomS3R**（または ESP32-S3 系で AtomS3R 相当）
- ライブラリ
  - [M5Unified](https://github.com/m5stack/M5Unified)
  - [M5Chain](https://github.com/m5stack/M5Chain)
- ボード設定の目安
  - **USB Mode**: USB-OTG (TinyUSB)
  - **USB CDC On Boot**: Enabled（シリアルログ用）

本スケッチは Arduino-ESP32 標準の `USB.h` / `USBMIDI.h` を使用します。

---

## MIDI 割り当て

接続順（同じ種類の中での順番）で番号が決まります。

| デバイス | 操作 | MIDI |
|----------|------|------|
| Chain Key | 押す / 離す | Note **60, 61, 62…** On/Off |
| Chain Angle | 回す | CC **1, 2, 3…** = 0–127（絶対） |
| Chain Encoder | 回す | CC **20, 21, 22…**（相対・64 中心） |
| Chain Encoder | クリック | Note **80, 81, 82…** On/Off |
| Chain Joystick | X 軸 | CC **40, 42, 44…** = 0–127（絶対） |
| Chain Joystick | Y 軸 | CC **41, 43, 45…** = 0–127（絶対） |
| Chain Joystick | クリック | Note **90, 91, 92…** On/Off |

### Encoder 相対 CC について

- **64** … 変化なし
- **65 以上** … 右回転（プラス）
- **63 以下** … 左回転（マイナス）

VRChat などでは `value - 64` をデルタとしてスライダーに加算すると扱いやすいです。

---

## 使い方

1. リポジトリのスケッチを開く
2. ライブラリとボード設定を確認
3. AtomS3R に書き込む
4. PC に USB 接続し、MIDI デバイスとして認識されることを確認
   - Windows: デバイスマネージャー / MIDI-OX など
5. Chain デバイスを接続（起動後でも可）
6. 画面に `K n A n E n J n` と操作ログが出ることを確認

ピン（Atomic ToChain Base + AtomS3R）:

```cpp
RXD_PIN = GPIO 6
TXD_PIN = GPIO 5
```

環境によって TX/RX が逆の場合は入れ替えてください。

---

## VRChat での利用例

ワールド側で **VRC Midi Listener** を使い、Udon から次を受け取ります。

- `MidiNoteOn` / `MidiNoteOff` … Key / Encoderクリック / Joystickクリック
- `MidiControlChange` … Angle / Encoder回転 / Joystick 軸

例:

- Note 60 → ボタンの `Interact` 相当を発火
- CC1 → スライダーを絶対位置で更新
- CC20 → `value - 64` を加算して相対操作

（ワールド側スクリプトは用途に合わせて別途用意してください。）

---

## 画面表示

```
Chain MIDI
----------------
K2 A1 E1 J1
----------------
Act:
Joy
Val:
X CC40=64
```

- 上段: 認識中の台数（Key / Angle / Encoder / Joystick）
- 下段: 直近の操作と MIDI 値

---

## 設定のカスタマイズ

スケッチ先頭付近の定数で変更できます。

```cpp
MAX_DEVICES            // 種類ごとの最大台数（既定 16）
RESCAN_INTERVAL_MS     // ホットプラグ再スキャン間隔（既定 1000ms）
NOTE_KEY_BASE          // Key の Note 開始番号
CC_ANGLE_BASE          // Angle の CC 開始番号
CC_ENC_REL_BASE        // Encoder 相対 CC 開始番号
CC_JOY_X_BASE / Y      // Joystick 軸 CC
NOTE_ENC_BTN_BASE      // Encoder クリック Note
NOTE_JOY_BTN_BASE      // Joystick クリック Note
```

---

## トラブルシュート

| 症状 | 確認点 |
|------|--------|
| MIDI として見えない | USB Mode が OTG (TinyUSB) か / 正しい USB ポートか |
| デバイスが増えない | ケーブル向き・Daisy chain / 画面の台数表示 / シリアルの Rescan ログ |
| Angle / Joy の端が足りない | 個体差あり。`ANGLE_MIN/MAX` や Joy の map 範囲を実測に合わせる |
| TX/RX エラー | `RXD_PIN` / `TXD_PIN` を入れ替え |

---

## ライセンス

MIT License

M5Stack 製品名・ライブラリは各権利者に帰属します。

---

## Credits

- Hardware & Chain protocol: [M5Stack](https://m5stack.com/)
- Libraries: M5Unified, M5Chain, Arduino-ESP32 USB MIDI
- Firmware design & implementation assistance: **[Grok](https://x.ai/grok) (xAI)**

Issue や改善案があれば Pull Request / Issue 歓迎です。
```
