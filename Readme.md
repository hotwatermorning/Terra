# VstHostDemo

## What's this?

[C++ Advent Calendar 2014](http://hwm.hatenablog.com/entry/2014/12/01/233320) のネタとして作成したVST3ホストアプリケーションです。
VST3のプラグインををロードして、音を鳴らせます。

## 機能 

ピアノの鍵盤画面を押すとノート情報を生成してプラグインに渡します。
また、PCのキーボードにも反応します。
Ableton Live方式で、A, W, S, ..., O, L, Pまでの範囲がソフトウェアキーボードとなり、ZとXでオクターブを変更できます。

また、Spaceキーを押しながら鍵盤画面をドラッグすると鍵盤の見える範囲を動かせます。

VST3プラグインをドラッグアンドドロップすると、そのプラグインをロードします。

### 64bit版に対応しました。

ただし、balorの64bit対応が必要です。
現在64bit版に対応したライブラリは公開されていませんが、以下のようにして64bit版のライブラリを作成可能です。

 * 既存の32bit版のプロジェクト構成を複製して64bit版のプロジェクト構成を作る
 * ビルドしていくつかのコンパイルエラーを取り除く

## プログラムのビルド方法

Visual Studio 2013でビルドが可能なのを確認しています

1. [Steinberg](http://japan.steinberg.net/)から、VST 3.6のSDKをダウンロードし、ソリューション内のvst3ディレクトリに展開したファイルをコピーする。
 1. ちょうど、`base`, `pluginterfaces`, `public.sdk`がvst3ディレクトリの下に配置されるようにする

2. [Boost](http://www.boost.org/)から、Boost.1.56.0をダウンロードしビルドする。（他のバージョンのBoostでも問題ないかもしれない）

3. [balorライブラリの公式ブログ](http://d.hatena.ne.jp/syanji/20110731/1312105612)から、balor 1.0.1をダウンロードし、ビルドする。

4. Vst3HostDemo.slnを開き、Boost, balorのインクルードディレクトリ、ライブラリディレクトリを設定する。

5. Vst3HostDemoプロジェクトをビルドする。

## ライセンス

このソースコードは、Boost Software License, Version 1.0で公開します。

-----

https://twitter.com/hotwatermorning
hotwatermorning@gmail.com
