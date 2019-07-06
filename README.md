![Terra](./misc/Logo.png)

## What's this?

The yet another audio plugin hosting application. (alpha version)

![ScreenShot](./misc/ScreenShot.png)

## Features

* Load VST3 plugins.
* Open audio input/output devices.
* Open MIDI input devices.
* Connect plugins and devices.
    * To connect, drag from a pin of a plugin/device node to a pin of an another node.
    * To disconnect, cut connections by shift + drag.
* Send MIDI Notes with the computer keyboard.
    * A, W, S, ..., O, L, and P keys are mapped to C3, C#3, D3, ..., C#4, D4, and D#4.
    * Z and X keys change octaves.

## How to build

Currently Terra can be built on these platforms:

* macOS 10.13 & Xcode 9.3.1
* macOS 10.14 & Xcode 10.2.1
* Windows 10 & Visual Studio 2017
* Windows 10 & Visual Studio 2019

### Prerequisites

* Java JRE (or JDK) version 8 or higher (for Gradle)
* Git 2.8.1 or later
* CMake 3.14.1 or later
* Xcode 9.3.1 or later
* Visual Studio 2017 or later

### macOS

```sh
cd ./gradle

./gradlew build_all [-Pconfig=Debug]
# The `config` property is optional. For release build, use `-Pconfig=Release` instead.

open ../build_debug/Debug/Terra.app
```

### Windows

```bat
cd .\gradle

gradlew build_all [-Pconfig=Debug] [-Pmsvc_version="..."]
: The `config` property is optional. For release build, use `-Pconfig=Release` instead.
: The `msvc_version` property can be either `"Visual Studio 16 2019"` or `"Visual Studio 15 2017"`.
: The former is the default value.
: If you have only Visual Studio 2017, specify the latter value to the property.
: For non-English locales, add `-Dfile.encoding=UTF-8` option to prevent Mojibake.

start ..\build_debug\Debug\Terra.exe
```

### TIPS

* Building submodules may fail after checking out new commit which refers another submodule commits.
If it occured, run the `gradlew` command again with `clean_all` task followed by the `build_all` task like this:

```sh
./gradlew clean_all build_all [-Pconfig=Debug]
```

* To cleanup the build directory of a specific submodule, remove `/path/to/Terra/ext/<submodule-name>/build_<config-name>` directory manually.
* If submodules are already built and only Terra need to be built, you can run the `gradlew` command with `prepare_app` and `build_app` tasks to skip rebuilding submodules:

```sh
./gradlew prepare_app build_app [-Pconfig=Debug]
```

## License and dependencies.

Terra is licensed under MIT License.

Terra uses these libraries.

* [wxWidgets](http://www.wxwidgets.org/)
* [PortAudio](http://www.portaudio.com/)
* [VST3 SDK](https://github.com/steinbergmedia/vst3sdk)
* [cppformat](http://fmtlib.net)
* [RtMidi](https://github.com/thestk/rtmidi)
* [Protocol Buffers](https://developers.google.com/protocol-buffers/)
* [MPark.Variant](https://github.com/mpark/variant)
* [midifile](https://github.com/craigsapp/midifile)

## Contact

hotwatermorning@gmail.com

https://twitter.com/hotwatermorning
