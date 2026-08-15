# M5ChainMIDI Web Installer

M5ChainMIDIの正式版ファームウェアをAtomS3Rへブラウザから書き込むためのWeb Installerです。

> [!IMPORTANT]
> M5ChainMIDIは個人が開発する非公式プロジェクトです。M5Stack Technology Co., Ltd.による公式製品ではなく、同社との提携または承認を示すものではありません。

## 公開URL

```text
https://shimez.github.io/M5ChainMIDI/installer/
```

デスクトップ版のChromeまたはEdgeを使用してください。

## バイナリの配置

Arduino IDEの`Sketch` → `Export Compiled Binary`で生成したmergedバイナリを、次の名前で配置します。

```text
docs/installer/firmware/M5ChainMIDI-1.1.0-AtomS3R-merged.bin
```

`manifest.json`は、このファイルをESP32-S3のoffset `0x0`へ書き込みます。

## リリース前の確認

- `manifest.json`の`version`とバイナリのファイル名が一致している
- mergedバイナリをoffset `0x0`から実機へ書き込める
- 消去済みAtomS3Rで起動し、USB MIDIデバイスとして認識される
- Key / Angle / Encoder / Joystick / ToFの主要操作が動作する
- Chainデバイスのホットプラグと切断時のNote Offが動作する
- バイナリのSHA-256とビルド元コミットをリリース記録へ残す
- HTTPSで公開したInstallerをChromeまたはEdgeから利用できる

SHA-256はPowerShellで確認できます。

```powershell
Get-FileHash .\firmware\M5ChainMIDI-1.1.0-AtomS3R-merged.bin -Algorithm SHA256
```

## ローカル確認

`docs/installer`ディレクトリでローカルWebサーバーを起動します。

```powershell
py -m http.server 8000 --bind 127.0.0.1
```

次のURLをデスクトップ版ChromeまたはEdgeで開きます。

```text
http://localhost:8000/
```

ESP Web ToolsをCDNから読み込むため、Installerの利用時にはインターネット接続が必要です。
