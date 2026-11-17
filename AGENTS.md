# 项目代理约束

## 硬件边界

- 本仓库只构建 UV-K5 V1 固件，目标硬件为 DP32G030 + BK4819。
- 禁止把本仓库产物标记为或提供给 UV-K1、UV-K5 V3；这些机型使用 PY32F071 + BK4829，固件不能混刷。

## 构建与发布

- 修改前先阅读现有实现并复用已有组件、函数和工具，保持现有代码风格。
- 所有开发、验证和正式构建都必须在隔离的构建目录或容器内部源码副本中完成；禁止在源码树中生成 `.o`、`.d`、`firmware`、`firmware.bin`、`firmware.packed.bin` 等构建产物。
- 构建脚本只能把最终产物复制到约定的 `build/` 或 `releases/` 目录；不得把根目录或 `compiled-firmware/` 中的临时二进制当作发布物。
- 正式打包必须执行 `./package-release.sh <version> <release-notes-file>`，不得手工拼装 ZIP。
- UV-K5 V1 的内嵌版本最多 7 个字符；版本必须同时出现在固件和发布包目录/ZIP 名中。
- 所有可交付产物只能写入 `releases/uvk5-v1-<version>/` 及同名 ZIP；已有版本禁止覆盖或静默重打。
- 发布包必须包含 `firmware.bin`、`firmware.packed.bin`、`BUILDINFO.txt`、`RELEASE_NOTES.md`、`SHA256SUMS`、`LICENSE`、`NOTICE` 和 `THIRD_PARTY_LICENSES/`。
- 打包前必须执行干净的 Docker 构建；验证镜像不超过 60 KiB、内嵌版本正确、ZIP 可解压且 SHA-256 校验通过。
- 历史 `releases/` 内容视为不可变；除非用户明确要求，不得修改或重新压缩既有版本。
- Radio Remote CAT V3 开发包固定执行 `./package-dev-radio-remote-v3.sh`，只写入被忽略的 `build/dev-radio-remote-v3/`，版本标识必须为 `RRCAT3`；不得将开发包写入 `releases/` 或作为正式发布物。

## Git 提交

- 完成后检查 `git status --short` 和 staged diff，只暂存本任务改动，不得夹带用户已有修改。
- 验证通过且存在实际变更时创建本地提交，提交信息使用简体中文 Conventional Commits；默认不推送。
