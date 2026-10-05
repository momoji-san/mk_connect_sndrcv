# mk_connect_sndrcv
This is a program that send program file and receives text of the execution results from MachiKania.

電子辞書Brainで動作するLinuxのBrainux上で動作する、MachiKania Type-P/PU とのUSB CDC接続用の`pcconnect`のカスタマイズ版です。

MachiKaniaの`pcconnect`をBrainux/Linux環境へ移植・改修し、BASICプログラムの転送だけでなく、MachiKania側から返ってくるコンパイル結果や`PRINT`などのテキスト出力をBrainuxのターミナルへ表示できるようにしています。

## 主な機能

- Brainuxの`machikap/`以下のファイルをMachiKaniaへ転送（connect.ini の設定による）
- MachiKaniaのUSB CDCシリアル通信（`/dev/ttyACM*`）に対応
- USBを抜き差しした際、`ttyACM0`から`ttyACM1`などへ番号が変わっても自動的に再接続
- MachiKaniaのリセットを待機し、`MACHIKAP`通信を検出すると自動的にファイル転送を開始
- 転送完了後、MachiKaniaから送られてくる起動メッセージ、コンパイル結果、BASICプログラムの出力をターミナルへ表示
- MachiKaniaをリセットすると、同じプログラムファイルを繰り返し転送・実行可能
- 出力表示中にBrainux側で任意のキーを押すと、次のMachiKaniaリセット待ちへ戻る  
   → BASICプログラムが無限ループしていてもBrainux側のキー入力で受信処理を終了しリセット待ち

## 動作の流れ

```text
Brainuxで本プログラムを起動
        ↓
MachiKaniaのリセット待ち
        ↓
MachiKaniaからのMACHIKAPを検出
        ↓
MachiKaniaへBASファイルを転送
        ↓
MachiKaniaがコンパイル・実行  
(MACHIKAP.INIで自動実行設定)
        ↓
コンパイル結果やPRINT出力をBrainuxへ表示
        ↓
任意のキーを押す
        ↓
次のMachiKaniaリセット待ち
        ↓
MachiKaniaのリセットすると再び転送  
        ↓  
  （以降繰り返し Ctrl+Cで終了）  
```

## 使用例

`machikap/`に`MACHIKAP.BAS`を置いてプログラムを起動します。  
connect.iniを設定しておきます。設定方法は下記を参照。  
https://rad51.net/blog/mycom/index.php?itemid=972  

プログラムをコンパイルして、実行。  
```sh
gcc -O2 -Wall -o connect_sndrcv connect__sndrcv.c
./connect_sndrcv
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
- MachiKania Type-P/PU
- MachiKaniaとBrainを接続する OTGケーブル（給電付）
- Brainux側でUSB CDC ACMが使用できること（Brainuxのリビルドが必要）  
　Brainuxのリビルドは下記を参照  
 　https://momoji-san.hateblo.jp/entry/2026/09/13/162228
- `/dev/ttyACM0`～`/dev/ttyACM9`のいずれかでMachiKaniaが認識されること
- GCC(Brainuxには標準でインストール済)

## ファイル構成例

```text
.
├── connect_sndrcv.c
└── machikap/
    └── MACHIKAP.BAS
```

## 注意

このプログラムは、Brainux上でMachiKaniaとのプログラム転送・実行・出力確認を繰り返し行うための実験・開発用プログラムです。

MachiKania側のUSBキーボード対応ファームウェアと`pcconnect`のファイル転送機能には組み合わせ上の制約があり、USBキーボード対応ファームウェアでは本プログラムは動作しません（MachiKaniaのpcconnectの仕様）。

## Source

- `connect_sndrcv.c` — Brainux向け改修版pcconnect

