---
name: feedback-default-test-media
description: 2026-11-23 用户定：上板测试播放默认用 /userdata/day8_media/yequ_640x480.mp4
metadata:
  type: feedback
---

上板测试播放器、解码时，默认片源用 `/userdata/day8_media/yequ_640x480.mp4`（12 MB，640x480），不再默认用 `sync_test_1080p.mp4`。

**Why:** 2026-11-23 用户明确要求"以后默认用 yequ_640x480.mp4 做测试"。

**How to apply:**
- 新写的运行脚本里 `DEFAULT_MEDIA_PATH` 用这个文件；day9_mpp_ffplayer/run_on_board_with_mac.sh 已经改过来了
- 只有要测音画同步（需要 1000 Hz"滴"声对齐）时才换 sync_test_1080p.mp4，换之前说明原因
- 片源有声音，出声规则照 [[feedback-ask-before-sound]]：先静音，要听先问
- 板子上其他 mp4：/oem/SampleVideo_1280x720_5mb.mp4、/opt/ui/src/apps/resource/media/movies/梵天神器片段1080p.mp4
