# UV-K5 V1 Radio Remote 固件

GitHub 仓库：[EarthHam/uv-k5-firmware-custom](https://github.com/EarthHam/uv-k5-firmware-custom)。

本仓库维护面向 Star / Radio Remote CAT V3 的社区衍生固件。保留上游固件功能，并增加角色化 CAT、屏幕推送、按键、带租约的 PTT 和静噪控制。

> **仅支持 UV-K5 V1，硬件为 DP32G030 + BK4819。不能用于 UV-K1 / UV-K5 V3，两种平台固件不能混刷。**

## 下载和刷机

在本仓库顶部的 **Releases** 页面下载与你的硬件匹配的 `uvk5-v1-<version>.zip`。GitHub 自动生成的 **Source code** 压缩包是源码，不是刷机包。

- 解压后的固件为 `firmware.packed.bin`（常规刷机）和 `firmware.bin`（原始镜像）。先阅读包内 `RELEASE_NOTES.md`，再使用支持该硬件和镜像格式的刷机工具。
- 刷机前备份本机信道、设置和校准数据；不要恢复其他电台的校准备份。连接稳定供电，刷写期间不要断开连接。
- 在解压目录执行 `shasum -a 256 -c SHA256SUMS` 检查文件完整性。Windows 可使用支持 SHA-256 的工具核对相应条目。
- 固件包包含构建信息、许可证和第三方声明。CHIRP 驱动及恢复工具不包含在本维护版本的包内；使用前核对驱动是否匹配该版本。
- 测试版本会标记为 Pre-release；编译和自动测试通过不代表已经通过实机验证。刷机失败时，先按对应机型的恢复说明操作，勿尝试另一硬件平台的固件。

## 本维护版本的改动

- Radio Remote CAT V3：`RHI3;` 协商能力，提供 CAT 状态、屏幕、普通按键、PTT 和静噪控制。
- `MAIN` 支持独立收发频率及亚音；本地操作会推送状态，串口残包会超时清理。
- 38400 波特 UART；保留 AT 行回显扩展与协议共存处理。
- 协议说明：[Radio Remote CAT V3](https://www.cloudsat.cn/open-platform/)。

## 编译和发布

正式包统一使用 Docker 构建，发布前提交所有改动，保持工作区干净：

```bash
./package-release.sh 1.0.0r3 release-notes/1.0.0r3.md
```

输出位于 `releases/uvk5-v1-1.0.0r3/` 及同名 ZIP；已有版本禁止覆盖。固件内嵌版本最多 7 个字符。 发布标签为 `v<version>`，不把标签的 `v` 写入内嵌版本。

本维护版本采用本地构建发布，不使用 GitHub Actions。先在本地运行测试和完整打包校验，再上传对应源码标签与 ZIP，以 **Pre-release** 提供候选测试版。实机验证状态写入发布说明，确认后再决定是否标为稳定版。具体流程见 [发布说明](PUBLISHING.md)。

## 上游和许可证

基于 Egzumer 固件，继承 Dual Tachyon、OneOfEleven、fagci 及其他贡献者的工作。

主固件沿用 [Apache-2.0](LICENSE)，原作者及第三方声明见 [NOTICE](NOTICE)。修改文件须保留原有版权并标明修改。上游功能、操作说明和完整致谢保留在 [上游说明](README.upstream.md)，其中的下载链接指向上游版本。

固件按现状提供，不保证适用于所有设备；发射前请确认设备设置及使用条件符合所在地要求。
