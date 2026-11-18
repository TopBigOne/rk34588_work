# 配置测试rk3588 开发环境（Mac M 系列 + Ubuntu）

在 **Mac（Apple M 系列芯片）** 和 **Ubuntu 20.04** 上用 CLion 开发 RK3588 程序：**写代码 → 一键交叉编译 → 自动上传到开发板 → 运行 / 断点调试**。两边用同一个工具链（GCC 10.4），编出来的程序一样。

✅ 2026-10-01 两边都验证通过：断点停在 `mpp_init` 一行，能查看 MPP 的变量；一键运行能在 Run 窗口看到板子上的输出。

## 文档
| 文档 | 内容 |
|---|---|
| [1_在mac上搭建调试环境.md](doc/1_在mac上搭建调试环境.md) | 完整搭建步骤：工具链、CLion 的 CMake / SSH / Remote GDB Server 配置、查看板子 IP、常见问题 |
| [2_在ubuntn上搭建运行和调试环境.md](doc/2_在ubuntn上搭建运行和调试环境.md) | Ubuntu 版：不需要 OrbStack 和转发脚本；工具链安装、工具链文件、SSH 免密、CLion 配置、一键运行 ▶、断点调试 🐞 全部 ✅ 已验证（2026-10-01） |


## 文件
| 文件 | 作用 |
|---|---|
| `main.cpp` / `CMakeLists.txt` | 测试程序：C++17 + 链接 MPP，在板子上创建 H.264 硬件编码器 |
| `run_on_board_with_mac.sh` / `run_on_board_with_ubuntu.sh` | 一键运行：编译 → 上传 → 在板子上运行。两个版本只有 `CMAKE=` 路径不一样。板子 IP 自动获取 |
| `run_get_board_ip.sh` | 查板子 IP（调用公共脚本） |
| `../../common_shell/get_board_ip.sh` | **公共脚本**：通过 adb 获取板子 IP，只输出 IP，所有工程共用 |
| `doc/`、`img/` | 文档和截图 |

## 运行结果（在开发板上）
```
Hello from ATK-DLRK3588 (aarch64, kernel 5.10.160)
C++ standard: 201703
MPP: H.264 hardware encoder created OK
```


```shell
adb shell poweroff
```