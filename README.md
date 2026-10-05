# mk_connect_sndrcv
MachiKania file send and text receive program.

Brainux上で動作する、MachiKania Type-PとのUSB CDC接続用の`pcconnect`のカスタマイズ版です。

MachiKaniaの`pcconnect`をBrainux/Linux環境へ移植・改修し、BASICプログラムの転送だけでなく、MachiKania側から返ってくるコンパイル結果や`PRINT`などのテキストをBrainuxのターミナルへ表示できるようにしています。

## 主な機能

- `machikap/`以下のファイルをMachiKaniaへ転送
- MachiKaniaのUSB CDCシリアル通信（`/dev/ttyACM*`）に対応
- USBを抜き差しした際、`ttyACM0`から`ttyACM1`などへ番号が変わっても自動的に再接続
- MachiKaniaのリセットを待機し、`MACHIKAP`通信を検出すると自動的にファイル転送を開始
- 転送完了後、MachiKaniaから送られてくる起動メッセージ、コンパイル結果、BASICプログラムの出力をターミナルへ表示
- MachiKaniaをリセットすると、同じプログラムを繰り返し転送・実行可能
- 出力表示中にBrainux側で任意のキーを押すと、次のMachiKaniaリセット待ちへ戻る
- BASICプログラムが無限ループしていても、Brainux側のキー入力で受信処理を終了し、次の実行へ移行可能

## 動作の流れ

```text
Brainuxでプログラム起動
        ↓
MachiKaniaのリセット待ち
        ↓
MACHIKAPを検出
        ↓
MachiKaniaへBASファイルを転送
        ↓
MachiKaniaがコンパイル・実行
        ↓
コンパイル結果やPRINT出力をBrainuxへ表示
        ↓
任意のキーを押す
        ↓
次のMachiKaniaリセット待ち
        ↓
リセットすると再び転送
```

## 使用例

`machikap/`に`MACHIKAP.BAS`を置いてプログラムを起動します。

```sh
gcc -O2 -Wall -o connect_receive_wait_reconnect connect_receive_wait_reconnect.c
./connect_receive_wait_reconnect
```

MachiKaniaをリセットすると、BASICファイルが転送されます。

転送後は、例えば次のようなMachiKaniaの出力がBrainuxのターミナルに表示されます。

```text
--- MachiKania output ---  Hit any key to continue.
MachiKania BASIC System
 Ver Phyllosoma 1.7.1.0
BASIC Compiler KM-1513 by Katsumi
LCD, File, & Keyboard systems by KENKEN

Compiling MACHIKAP.BAS

HELLO MachiKania 333
```

ここでキーを押すと、次のMachiKaniaリセット待ちになります。

## USBデバイス番号の変化への対応

MachiKaniaをリセットしたりUSB CDCデバイスが再接続されたとき、Linux側で``/dev/ttyACM0``が``/dev/ttyACM1``などに変わる場合があります。

このプログラムでは`/dev/ttyACM0`～`/dev/ttyACM9`を順番に探して接続することで、この変化に対応しています。

## 通信について

MachiKaniaとの通信では、`MACHIKAP`、`SENDCMDS`、`SENDFILE`、`DONEDONE`、`ALL DONE`などのプロトコル上の文字列を検出します。

また、USBシリアル通信はメッセージ単位ではなくバイトストリームとして扱われるため、1回の`read()`で複数のメッセージを受信したり、1つのメッセージが複数回の`read()`に分割されることがあります。本プログラムでは受信データを継続的に処理しながら、次の`MACHIKAP`も検出できるようにしています。

## 必要な環境

- Brainuxが動作するSHARP Brain
- MachiKania Type-P
- Brainux側でUSB CDC ACMが使用できること
- `/dev/ttyACM0`～`/dev/ttyACM9`のいずれかでMachiKaniaが認識されること
- GCC

## ファイル構成例

```text
.
├── connect_receive_wait_reconnect.c
└── machikap/
    └── MACHIKAP.BAS
```

## 注意

このプログラムは、Brainux上でMachiKaniaとのプログラム転送・実行・出力確認を繰り返し行うための実験・開発用プログラムです。

特に、MachiKania側のUSBキーボード対応ファームウェアと`pcconnect`のファイル転送機能には組み合わせ上の制約があります。使用するMachiKaniaファームウェアの仕様に合わせてください。

## Source

- `connect_receive_wait_reconnect.c` — Brainux向け改良版pcconnect

