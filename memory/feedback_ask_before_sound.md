---
name: feedback-ask-before-sound
description: 2026-11-19 用户要求：在板子喇叭上放任何声音（测试音、"滴滴"声、播放带声音的视频）之前先问用户
metadata:
  type: feedback
---

让板子喇叭出声之前，**先问用户能不能放**：包括 sync_test 的 1000 Hz + 每秒"滴"、M3 只放声音、播放器带声音播放、aplay/speaker-test、解除静音（amixer Speaker on、pactl set-sink-mute 0）。

**Why:** 用户常在办公室，2026-11-19 喇叭突然响起"滴滴"声，用户让赶紧关掉，并说"下次要播放'滴滴'声，先问一下我"。

**How to apply:**
- 默认保持静音：`amixer -c 3 sset Speaker off` + `PULSE_SERVER=unix:/tmp/pulse-socket pactl set-sink-mute @DEFAULT_SINK@ 1`（ES8388 是 card 3，PulseAudio 默认 sink 就是它）。
- 不出声也能验证音频：M3 存 PCM 和 Mac 上 ffmpeg 解出来的比；播放器看日志里"播了 x 秒的声音"。
- 要出声验证时先问，并说清会响多久、什么声音；用完马上恢复静音。
- 用户说"关掉声音"时立即静音 + 结束播放进程，不先分析。
