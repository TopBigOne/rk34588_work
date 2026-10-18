# 板载麦克风录音：ALSA、arecord、amixer

> 板子能不能采集声音？**能**。板载一个麦克风（咪头），接在 ES8388 音频芯片上，用 Linux 的 ALSA 接口录音。
> 本文的输出都是 2026-10-18 在板子上实测的。

---

## 1. 硬件：麦克风接在哪

《ATK-DLRK3588 硬件参考手册》（A 盘 `10、用户手册/硬件参考手册/`）第 3.22 节：

| 部件 | 说明 |
|---|---|
| **ES8388**（顺芯） | 音频编解码芯片（codec）：内部有 ADC（声音 → 数字，录音用）、DAC（数字 → 声音，放音用）、麦克风放大器 |
| **MIC（咪头）** | 板载驻极体麦克风，接到 ES8388 的麦克风输入 |
| 耳机接口 | 3.5mm 4 段式，能放音，也能用耳机上的麦克风录音 |
| 喇叭 | 板载一个 8Ω 1W 小喇叭（经过 HT6872 功放），另有 SPKR / SPKL 两个外接端子 |

---

## 2. ALSA：Linux 的声音接口

和摄像头用 V4L2 一样，Linux 的声音设备统一用 **ALSA**（Advanced Linux Sound Architecture）。

| | 视频（摄像头） | 声音 |
|---|---|---|
| 内核接口 | V4L2 | **ALSA** |
| 设备文件 | `/dev/video22` | `/dev/snd/pcmC3D0c`（第 3 号声卡、0 号设备、c = capture 录音） |
| 命令行工具 | `v4l2-ctl`、`media-ctl` | **`arecord`（录）、`aplay`（放）、`amixer`（调设置）** |
| 写程序用 | `ioctl` + `<linux/videodev2.h>` | **`alsa-lib` 库**（`<alsa/asoundlib.h>`），一般不直接 ioctl |
| 编码 | MPP（H.264 / H.265） | MPP **不管音频**，要另外编（AAC 等） |

---

## 3. 板子上有哪些声卡（实测）

```bash
adb shell 'cat /proc/asound/cards'
```
```
 0 [rockchipdp0    ]: rockchip-dp0          ← DP 显示接口的声音
 1 [rockchipdp1    ]: rockchip-dp1
 2 [rockchiphdmiin ]: rockchip_hdmiin       ← HDMI 输入的声音
 3 [rockchipes8388 ]: rockchip-es8388       ← ⭐ 板载音频芯片：麦克风、耳机、喇叭
 4 [rockchiphdmi0  ]: rockchip-hdmi0        ← HDMI 输出的声音
 5 [rockchiphdmi1  ]: rockchip-hdmi1
```

**能录音的**（`arecord -l`，实测）：
```
card 2: rockchiphdmiin  ..., device 0   ← HDMI 输入进来的声音
card 3: rockchipes8388 ..., device 0    ← ⭐ 麦克风
```

---

## 4. 录音和放音

### 4.1 录音：arecord

```bash
adb shell 'arecord -D plughw:3,0 -f S16_LE -r 44100 -c 2 -d 5 /userdata/av/mic_test.wav' < /dev/null
```

| 参数 | 意思 |
|---|---|
| `-D plughw:3,0` | 用第 **3** 号声卡的第 **0** 号设备。`plug` = 参数（采样率、声道）不匹配时自动转换；`hw:3,0` 是直接用硬件，参数不支持就报错 |
| `-f S16_LE` | 每个采样 16 位有符号整数，小端（CD 音质就是这个） |
| `-r 44100` | 采样率：每秒 44100 次 |
| `-c 2` | 双声道 |
| `-d 5` | 录 5 秒 |

实测：`Recording WAVE ... Signed 16 bit Little Endian, Rate 44100 Hz, Stereo`，文件 882,124 字节。

> 算一下：44100 次/秒 × 2 声道 × 2 字节 × 5 秒 = **882,000 字节**，再加 wav 文件头，对得上。
> 对比视频：1080p NV12 一帧就 3MB，30fps 每秒 90MB；声音 CD 音质每秒才 176KB。

### 4.2 放音：aplay

```bash
adb shell 'aplay -D plughw:3,0 /userdata/av/mic_test.wav' < /dev/null      # 从板载喇叭放出来
```

### 4.3 在 Mac 上听、看音量

```bash
cd /Users/dev/Documents/AV/rk_work/day3_cam2h264
KEEP=1 ./run_move_file_from_board_via_usb.sh /userdata/av/mic_test.wav
ffplay -autoexit result_from_board/mic_test.wav                                      # 听
ffmpeg -i result_from_board/mic_test.wav -af volumedetect -f null - 2>&1 | grep volume   # 看平均 / 最大音量
```

音量是 dB，0 dB 是最大（满幅），越负越小。**数字静音大约 -91 dB**（16 位的极限）。

---

## 5. 怎么证明麦克风真的在收声（实测）

房间安静时录 5 秒，平均 -75.8 dB、最大 -61.1 dB——不是完全静音（-91），但很低，分不清是"没收到"还是"本来就安静"。

**自己放、自己录**：录音的同时，用板载喇叭放 2 秒 1kHz 的"嘀"（-20dB，不刺耳），再分段测音量：

```bash
# Mac 上生成一个 2 秒 1kHz 的嘀声，推到板子
ffmpeg -f lavfi -i "sine=frequency=1000:duration=2" -af "volume=-20dB" -ar 44100 -ac 2 -sample_fmt s16 beep_1k.wav
adb push beep_1k.wav /userdata/av/
# 板子上：后台录 4 秒，1 秒后开始放嘀声
adb shell 'cd /userdata/av; arecord -D plughw:3,0 -f S16_LE -r 44100 -c 2 -d 4 mic_beep.wav & sleep 1; aplay -D plughw:3,0 beep_1k.wav; wait' < /dev/null
```

| 时间段 | 平均音量（实测） |
|---|---|
| 0～1 秒（还没放） | -78.5 dB |
| **1.2～2.8 秒（喇叭在响）** | **-35.8 dB**（高了 43 dB） |
| 3.3～3.9 秒（放完了） | -78.8 dB |

响的时候音量明显升高，停了又回到底噪 → **麦克风在收声**。

> 同一张声卡可以一边录一边放（全双工），所以这个测试能做。

---

## 6. 录音相关的设置：amixer

```bash
adb shell 'amixer -c 3 scontrols'           # 列出所有控制项
adb shell 'amixer -c 3 sget "Main Mic"'     # 看某一项
adb shell 'amixer -c 3 sset "Left Channel" 5'   # 改某一项（未实测）
```

当前设置（实测，出厂就配好了，录板载麦克风不用改）：

| 控制项 | 值 | 意思 |
|---|---|---|
| `Main Mic` | on | 用板载麦克风 |
| `Headset Mic` | off | 不用耳机上的麦克风 |
| `Left/Right PGA Mux` | DifferentialL / DifferentialR | 输入用差分方式 |
| `Differential Mux` | Line 2 | 差分输入选第 2 路（板载麦克风接的那一路） |
| `Left/Right Channel`（Capture） | 3（9 dB） | 麦克风前级放大，0～8 档，每档 3 dB |
| `ALC Capture Function` | Stereo | 自动增益（ALC）开：声音小自动放大 |
| `Capture Digital` | 192（0 dB，最大） | 数字音量 |
| `Capture Mute` | off | 没静音 |

声音太小可以先调 `Left/Right Channel`（前级放大）；太大会削顶失真。

---

## 7. 和视频的关系（以后用得到）

- 录"带声音的视频"：视频 V4L2 → MPP 编 H.264，声音 ALSA → 编 AAC（软件编码，比如 ffmpeg / fdk-aac），再一起封装成 mp4，或者一起推 RTSP
- 音视频要**同步**：两边的时间戳要对齐（V4L2 的 `timestamp`、ALSA 的时间戳）
- 这些在 Day 5（推流）或者以后做录像机时会用到

---

## 8. 自测

- [ ] 板子的麦克风接在哪个芯片上？系统里是第几号声卡？
- [ ] `plughw:3,0` 和 `hw:3,0` 有什么区别？
- [ ] 44.1kHz、双声道、16 位录 5 秒，文件大约多大？
- [ ] 录到的音量 -75 dB 是不是说明麦克风坏了？怎么证明它在收声？
- [ ] MPP 能编码声音吗？带声音的视频要怎么做？
