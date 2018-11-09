# Vst3HostDemo

## What's this?

A Demo application which hosts vst3 plugins.

現在は、OSX 10.13.4 & Xcode 9.3.1でのビルドを確認しています。

## 機能

プラグインをロードし、ピアノの鍵盤画面を押すとそのプラグインで音を鳴らせます。
また、Ableton Live方式で、PCのキーボードををソフトウェアキーボードとして使用できます。
    * A, W, S, ..., O, L, Pまでの範囲をドレミ...に割り当てています。
    * ZとXでオクターブを変更できます。

## プログラムのビルド方法

```
cd ./gradle
./gradlew build_all
open build_debug/Debug/Vst3HostDemo
```

## ライセンス

このソースコードは、Boost Software License, Version 1.0で公開します。

また、以下のライブラリを使用しています。

* [wxWidgets](http://www.wxwidgets.org/)
* [PortAudio](http://www.portaudio.com/)
* [VST3 SDK](https://github.com/steinbergmedia/vst3sdk)

-----

https://twitter.com/hotwatermorning
hotwatermorning@gmail.com
