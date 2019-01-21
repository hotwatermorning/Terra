# Vst3HostDemo

## What's this?

A Demo application which hosts vst3 plugins.

現在は、OSX 10.13.4 & Xcode 9.3.1でのビルドを確認しています。

## 機能

プラグインをロードし、そのプラグインにオーディオデバイスやMIDIデバイスのノードを接続して、プラグインから音を鳴らしたりエフェクトをかけたりできます。

各ノードは、ノード下部の出力ピンからノード上部の入力ピンに向かって接続ができます。`Shift`キーを押しながらドラッグすると、接続を解除できます。

`Software Keyboard` デバイスをプラグインを繋ぐと、GUI上のピアノ鍵盤やPCのキーボードからMIDI信号をプラグインに送信できます。

PCのキーボードはAbleton Live方式で、
 * A, W, S, ..., O, L, Pまでの範囲をC3, C#3, D3, ..., C#4, D4, D#4に割り当てています。
 * ZとXでオクターブを変更できます。


## プログラムのビルド方法

```
cd ./gradle
./gradlew build_all
open build_debug/Debug/Vst3HostDemo -Pconfig=<Debug or Release>
```

## ライセンス

このソースコードは、Boost Software License, Version 1.0で公開します。

また、以下のライブラリを使用しています。

* [wxWidgets](http://www.wxwidgets.org/)
* [PortAudio](http://www.portaudio.com/)
* [VST3 SDK](https://github.com/steinbergmedia/vst3sdk)
* [cppformat](http://fmtlib.net)
* [RtMidi](https://github.com/thestk/rtmidi)
* [Protocol Buffers](https://developers.google.com/protocol-buffers/)
* [MPark.Variant](https://github.com/mpark/variant)

-----

https://twitter.com/hotwatermorning

hotwatermorning@gmail.com
