![GameInstance.com esp32 audio player](docs/logo.svg)

# esp32-audio-player - GameInstance.com

An ESP32 audio player powered by PCM56 DAC chips.


## Setup

Create the work directory in which you'll clone the **esp32-audio-player** repository and its
dependencies: **basics**, **stream**, **audio**, **esp-idf-cpp**.

```
mkdir ~/gameinstance
cd ~/gameinstance
git clone https://github.com/gameinstance/basics
git clone https://github.com/gameinstance/stream
git clone https://github.com/gameinstance/audio
git clone https://github.com/gameinstance/esp-idf-cpp
git clone https://github.com/gameinstance/esp32-audio-player
```

## Prerequisite

* [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html) environment,
* the [player hardware](../hardware) implementation or a breadboard prototype,
* a serial connection to the ESP32 controller with control over the flashing GPIO pins,
* WIFI_AP and WIFI_PASS configuration in **esp32-audio-player/firmware/main/esp32-audio-player.cc** .

## Build and flash

```
cd ~/gameinstance/esp32-audio-player/firmware
idf.py build flash monitor

```
