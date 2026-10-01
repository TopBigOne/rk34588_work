# 配置测试rk3588 在mac M系列电脑上的开发环境

在 Mac（Apple M 系列芯片）上用 CLion 开发 RK3588 程序：**写代码 → 一键交叉编译 → 自动上传到开发板 → 断点调试**。

✅ 2026-10-01 验证通过：断点停在 `mpp_init` 一行，能查看 MPP 的变量。

## 文档
| 文档 | 内容 |
|---|---|
| [1_在mac上搭建调试环境.md](doc/1_在mac上搭建调试环境.md) | 完整搭建步骤：工具链、CLion 的 CMake / SSH / Remote GDB Server 配置、查看板子 IP、常见问题 |


## 运行结果（在开发板上）
```
Hello from ATK-DLRK3588 (aarch64, kernel 5.10.160)
C++ standard: 201703
MPP: H.264 hardware encoder created OK
```


```shell
adb shell poweroff
```