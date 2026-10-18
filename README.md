# scrcpy-cam

A camera client for [scrcpy](https://github.com/Genymobile/scrcpy) with manual
camera controls. It pushes the scrcpy server to a device over adb, streams the
camera, and exposes the controls scrcpy does not have a UI for: focus
distance, autofocus mode, exposure time, ISO, zoom and torch.

Decoded frames are published as a virtual camera, so any application that can
open a webcam can use the phone's cameras.

| | |
|---|---|
| Device | Android 12 or higher (camera streaming requirement) |
| Client | Windows and Linux |
| Virtual camera | e2eSoft VCam SDK on Windows, v4l2loopback on Linux |
| UI | Dear ImGui, SDL3 + OpenGL |

## Requirements

- **Android platform-tools**, with `adb` on your `PATH`. scrcpy-cam shells out to
  `adb` and does not bundle it. If adb lives somewhere else, set the `ADB`
  environment variable to its full path.
- A device **already authorized** for this adb server. Check with `adb devices`:
  a usable device shows as `device`, not `unauthorized` or `offline`.

### Connecting a device

**USB** — turn on Developer options and USB debugging, plug the phone in, and
accept the "Allow USB debugging?" prompt. The device then appears by its serial
after **Scan for adb devices**.

**Network** — enable Wireless debugging, or run `adb tcpip 5555`, then
`adb connect <ip>:5555`. You can also type the address into the `host:port` field
and press **Connect host**. USB and network devices can be used at the same time;
pick whichever you want from the list.

The scan only shows devices adb already knows about, which is why the manual
serial and `host:port` fields exist.

## Features

- Finds cameras over adb, by scanning or by entering a serial or `host:port`
- Lists the cameras the device exposes, including the wide and telephoto lenses
  that some devices hide behind a single logical camera
- Resolution, frame rate, bitrate and high speed capture selection
- Manual focus distance, AF mode and trigger, exposure time, ISO
- Zoom, torch, flip X, flip Y and rotate applied to the virtual camera output

## Building

### Windows

```
build.bat
```

FFmpeg and SDL3 are downloaded into `third_party/` on the first run. Requires
Visual Studio (C++ workload) and CMake.

### Linux

```
sudo apt install build-essential cmake pkg-config \
                 libavcodec-dev libswscale-dev libavutil-dev libsdl3-dev
./build.sh
```

### Virtual camera on Linux

```
sudo modprobe v4l2loopback
```

The v4l2loopback device then shows up in OBS, Zoom and the rest as a normal
camera.

## Usage

Run `scrcpy-cam`, press **Scan for adb devices**, pick a device and press
**Connect**. The resolution, frame rate and bitrate pickers fill in once the
device has been queried. Choose a camera and press **Start stream**.

To connect to a device that is not on adb yet, type its address in the
`host:port` field and press **Connect host**; the typed address takes
precedence over the scanned list.

## Credits

- [scrcpy](https://github.com/Genymobile/scrcpy) by Genymobile, Apache-2.0.
  This client uses the scrcpy server, its control protocol, and the structure of
  its desktop client.
- [Dear ImGui](https://github.com/ocornut/imgui), MIT.
- [FFmpeg](https://ffmpeg.org/), LGPL. Binaries are downloaded at build time,
  not vendored here.
- [SDL](https://libsdl.org/), zlib. Binaries are downloaded at build time.
- The virtual camera sink follows the approach in scrcpy's `--v4l2` support.

## The scrcpy server

The server is not the stock upstream build. It comes from a fork that adds the
manual camera parameter messages this client sends:

- Repository: <https://github.com/serifpersia/scrcpy>
- Server source: `server/src/main/java/com/genymobile/scrcpy/video/CameraCapture.java`
- Protocol: `ControlMessage.java` (`TYPE_CAMERA_SET_PARAM`),
  `ControlMessageReader.java`, `Controller.java`
- Capability report: `LogUtils.buildCameraListMessage()`

Build it with `gradlew -p server assembleRelease`. The resulting APK is committed
here as `vendor/scrcpy-server` so the client always ships a matching server.

The client sends the following control message:

```
byte 0      23          (TYPE_CAMERA_SET_PARAM)
byte 1      parameter id
int32       value, big-endian
```

| id | parameter | value |
|----|-----------|-------|
| 0 | AF mode | `CONTROL_AF_MODE` |
| 1 | Focus distance | diopters x 1000 |
| 2 | AE mode | `CONTROL_AE_MODE` |
| 3 | Exposure time | nanoseconds |
| 4 | Sensitivity | ISO |
| 5 | AF trigger | ignored, runs one autofocus pass |

## License

scrcpy-cam is released under the Apache License, Version 2.0, matching scrcpy.
See [LICENSE](LICENSE). FFmpeg and SDL3 are downloaded at build time rather
than vendored, so they are not redistributed as part of this repository.