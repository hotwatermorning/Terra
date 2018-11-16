# Vst3HostDemo

## What's this?

A Demo application which hosts vst3 plugins.

現在は、OSX 10.13.4 & Xcode 9.3.1でのビルドを確認しています。

## 機能

プラグインをロードし、ピアノの鍵盤画面をクリックするとそのプラグインで音を鳴らせます。
また、Ableton Live方式で、PCのキーボードを鍵盤として使用できます。

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

-----

https://twitter.com/hotwatermorning

hotwatermorning@gmail.com
