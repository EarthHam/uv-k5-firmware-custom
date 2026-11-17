# UV-K5 V1 发布流程

## 仓库与源码

本仓库仅发布 UV-K5 V1（DP32G030 + BK4819）。保留上游 Git 历史和许可证；维护版历史整理为 EarthHam 的初始提交，使用 GitHub 隐私邮箱。原个人提交历史仅保存在本地忽略目录 build/private-history/，不得上传。向你拥有的 GitHub 仓库推送当前分支；不要向上游仓库推送本维护版，也不要使用 `--mirror` 或强制推送。

首页的 Releases 入口属于当前仓库，不依赖固定账号。`README.upstream.md` 为历史资料，不应把其中的上游下载当作本项目发布物。

`.gitignore` 排除构建产物、私人设备备份和编辑器设置，`.dockerignore` 排除它们进入构建上下文。忽略规则无法清除已经提交的文件或历史。公开前应检查提交历史中的敏感信息；发现凭据应先撤销凭据再处理历史，不能只删除当前文件。

## 版本与构建

1. 在 `release-notes/<version>.md` 写明改动、兼容性、已知问题和实机验证结果。内嵌版本最多 7 个字符，仅使用字母、数字、点、下划线和连字符。标签为 `v<version>`，例如 `v1.0.0r2`。
2. 运行 `bash tests/run-cat-tests.sh` 和 `bash tests/run-uart-parser-tests.sh`。检查改动后提交，只在干净工作区执行正式打包。打包脚本会拒绝未提交或未跟踪文件。
3. 执行 `./package-release.sh <version> release-notes/<version>.md`。脚本在 Docker 内干净构建，记录完整源码提交、工具链和镜像标识，验证镜像尺寸、内嵌版本、许可证、SHA-256 和 ZIP。
4. 在包目录执行 `shasum -a 256 -c SHA256SUMS`，再用准确匹配的电台进行实机验证。版本目录和 ZIP 已存在时脚本拒绝覆盖；要重做请采用新版本号，保留旧版本。

工具链固定为 ARM GNU 13.3.Rel1（13.3.1），Python CRC 依赖固定为 1.7（仅 K5 V1 打包需要）。基础镜像及系统依赖仍可能更新，BUILDINFO 记录实际镜像 ID；不宣称逐字节可复现。

## 本地上传发布

本维护分支不包含 GitHub Actions 工作流；测试和构建全部在本地执行，GitHub 仅托管源码与发布附件。

1. 确认本地测试通过、工作区干净，运行正式打包脚本并检查包内 BUILDINFO、SHA256SUMS、许可证和目标硬件。
2. 只推送维护分支到 EarthHam 下对应仓库。禁止使用 `--mirror`，不要上传上游历史标签、旧个人引用或 build/ 下的私有备份。
3. 为本次构建的准确源码提交创建 `v<version>` 标签，推送该标签。
4. 上传完整 ZIP，使用同版本的 `release-notes/<version>.md` 创建 Release，并标记为 Pre-release。实机验证未完成时须明确写明，不得宣称已实机通过。
5. 公开发布前复核附件、硬件说明和标签提交；确认后公开候选测试版。稳定版须待实机验证完成后另行决定。

例如：`gh release create v1.0.0r2 <完整ZIP路径> --verify-tag --prerelease --notes-file release-notes/1.0.0r2.md`。本地构建通过后才上传，不依赖自动构建或 workflow 授权。

## 用户下载与恢复

面向用户发布 `uvk5-v1-<version>.zip`，保留包内原始文件名与校验清单。包内没有通用校准数据、原厂固件、引导程序或恢复工具。恢复时使用对应机型的官方或上游资料，且只恢复本机备份。

## 历史资料

本地历史 releases/ 目录保持不变，不会自动上传或作为新版本重新发布。
