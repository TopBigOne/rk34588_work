# adb shell 会吃掉终端输入：一次粘贴多行命令时后面的行消失了

> 2026-10-18 真实遇到：一次粘贴两行命令，第二行没执行，第一行还报了莫名其妙的错。
> 修法：脚本里所有 `adb shell` 都加 `< /dev/null`。day1、day2、day3 的脚本都已经改了。
> 前置：[重定向和_dev_null.md](重定向和_dev_null.md)（0 号口 stdin、`<`、`/dev/null`）

---

## 1. 现象

一次粘贴了两行：
```bash
KEEP=1 ./run_move_file_from_board_via_usb.sh /userdata/av/cam_m2.nv12
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 result_from_board/cam_m2.nv12
```

输出（实测）：
```
=== 从板子拉取 /userdata/av/cam_m2.nv12（ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 result_from_board/cam_m2.nv12
3110400 字节）→ result_from_board/cam_m2.nv12 ===
/userdata/av/cam_m2.nv12: 1 file pulled, 0 skipped. 9.2 MB/s (3110400 bytes in 0.323s)
大小不一致：板子 ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 result_from_board/cam_m2.nv12
3110400 字节，Mac 3110400 字节。板子上的文件保留，没有删除
```

两个怪事：
1. **第二行 `ffplay ...` 跑进了"板子上的文件大小"里**，于是报"大小不一致"（明明都是 3110400）
2. **ffplay 没有执行**

---

## 2. 为什么

### 2.1 粘贴的多行，先排队在终端的"输入"里

粘贴两行时，终端把两行都放进 shell 的输入里排队：shell 先执行第一行（脚本），**第二行还在输入里等着**，等脚本结束了再执行。

这个"输入"就是**0 号口 stdin**（见 [重定向和_dev_null.md](重定向和_dev_null.md) 第 1 节）。脚本里的每个命令默认都继承这个 stdin。

### 2.2 `adb shell` 会读 stdin，转发到板子

`adb shell "命令"` 的设计是：**把 Mac 这边的输入转发给板子上的程序**（这样在板子上跑需要输入的程序时，可以在 Mac 上打字）。所以它会去读 stdin——正好把排队的第二行读走了。

脚本里查文件大小这一句：
```bash
BOARD_SIZE=$($ADB shell "stat -c %s '$SRC' 2>/dev/null" | tr -d '\r')
```
adb 读走了 `ffplay ...` 那一行，结果它混进了 `$( )` 捕获的输出里，`BOARD_SIZE` 变成了"ffplay ... 换行 3110400"。和 Mac 上的 `3110400` 一比，当然不一样。
（为什么读走的输入会出现在输出里，和 adb 的实现有关，没深究；结果就是变量被污染了。）

第二行被 adb 读走、转发到板子上就没了，所以**ffplay 不会再执行**。

### 2.3 复现（不用真的粘贴）

用 `printf ... |` 给脚本"喂"一行输入，效果和粘贴一样（2026-10-18 实测）：
```bash
printf 'echo pasted-line\n' | KEEP=1 ./run_move_file_from_board_via_usb.sh /userdata/av/cam_m2.nv12
# 修之前：大小不一致：板子 echo pasted-line
#         3110400 字节，Mac 3110400 字节。……
```

---

## 3. 修法：给 adb 一个空的输入 `< /dev/null`

```bash
$ADB shell "stat -c %s '$SRC' 2>/dev/null" < /dev/null | tr -dc '0-9'
#                                          └ 0 号口改成从黑洞读：adb 一读就是"结束"，不会去碰终端的输入
#                                                              └ 只留数字，再防一层
```

**脚本里所有 `adb shell` 都这样加**（day3 8 处、day1 10 处、day2 9 处，共 27 处，2026-10-18 提交）：
```bash
$ADB shell "mkdir -p $REMOTE_DIR" < /dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1
exec $ADB shell "LD_LIBRARY_PATH=... $REMOTE_DIR/$BIN_NAME $*" < /dev/null     # 运行程序的这一句也加
```

### 3.1 验证（2026-10-18 实测）

| 测试 | 修之前 | 修之后 |
|---|---|---|
| `printf 'echo pasted-line\n' \| KEEP=1 ./run_move_file_from_board_via_usb.sh ...` | 报"大小不一致" | ✅ `已拷贝到 result_from_board/cam_m2.nv12` |
| 正常用法 / 文件不存在 | — | ✅ 正常 / `板子上找不到 ...`，退出码 1 |
| `printf 'echo SECOND-LINE\n' \| { ./run_on_board_with_mac.sh; cat; }` | 第二行被吃掉 | ✅ 程序照常跑完，**脚本结束后还能读到第二行** |

现在可以放心一次粘贴多行了。

---

## 4. 什么时候**不能**加 `< /dev/null`

加了以后，板子上的程序**收不到任何键盘输入**。以下情况不要加：

| 情况 | 例子 |
|---|---|
| 进入板子的交互式 shell | `adb shell`（不带命令），要在里面敲命令 |
| 板子上的程序要你输入东西 | 程序里有 `scanf`、`fgets(stdin)`，或者要按回车继续 |

我们的程序都不读键盘，所以都加了。**Ctrl+C 不受影响**：Ctrl+C 是终端发给本地 adb 进程的信号，不走 stdin，adb 退出后板子上的程序也会结束。

> 另一种写法 `adb shell -n "命令"`（`-n` = 不读 stdin），效果应该一样，没实测，脚本里用的是 `< /dev/null`。

---

## 5. 举一反三

不只是 adb，**会读 stdin 的命令放在脚本里，都可能吃掉粘贴的下一行**。常见的还有 `ssh 主机 "命令"`（和 adb shell 一样会转发输入）、在 `while read` 循环里调用 ssh 等。解决办法一样：`< /dev/null`，或者用它们自己的"不读输入"选项（ssh 是 `-n`）。

---

## 6. 自测

- [ ] 粘贴两行命令时，第二行在执行第一行的时候放在哪里？
- [ ] `adb shell` 为什么会读 stdin？它读走第二行以后发生了什么？
- [ ] 不真的粘贴，怎么复现这个问题？
- [ ] `< /dev/null` 为什么能解决？
- [ ] 什么情况下不能给 adb shell 加 `< /dev/null`？
