# 开发与发布说明

[中文（当前页面）](DEVELOPMENT.md) · [English](DEVELOPMENT.en.md)

## 仓库边界

本仓库只负责 DINPUT8 代理、链接定义、配置模板和打包工作流。
仓库不包含游戏文件和第三方运行时；本地运行时请放在 checkout 之外，
或放入已被忽略的 `runtime/` 目录。

## 构建结构

`src/proxy/dinput8.c` 使用无 CRT 的 C 代码编译。它通过
`dinput8.def` 转发六个原生 DINPUT8 导出，并在 DS2LE/ReShade 的
`dxgi.dll` 宿主出现后启动 worker。worker 从游戏目录加载 Bridge
和 RenoDX addon，补齐缺失的 ReShade 入口，重定向 LE 的六个 stub，并维护
LE 在设备初始化期间会清空的事件槽。

配置 poke 表绑定 RenoDX DLSS5 `4.60`。如果新 addon 改变布局，必须重新推导
并测试地址表后再发布；不要用不加限定的 `latest` 下载掩盖兼容性变化。

`src/tests/fake_dxgi.c` 是用于链接器/导出实验的离线小型 fixture，
不模拟真实游戏，也不能证明运行时兼容。

## 本地检查

```powershell
cmd /c src\proxy\build.cmd
.\tools\package-release.ps1 -Version local
git diff --check
```

提交 Pull Request 前确认没有把 `.dll`、`.addon64`、`.rar`
或游戏压缩包加入 Git。工作流会执行同样的策略检查。

## 发布清单

1. 在干净的游戏目录备份上测试 README 中列出的确切 DS2LE、Bridge、RenoDX
   和 DLSSNR 版本；
2. 记录测试显卡/驱动并更新兼容性表；
3. 构建并检查 SHA-256 输出；
4. 提交修改并创建带注释的 `vX.Y.Z` tag；
5. 推送分支和 tag，tag 工作流会发布 ZIP 与 `SHA256SUMS.txt`；
6. 在 GitHub Release 说明兼容性变化和已知运行时限制，不要暗示已打包
   第三方二进制。

## 设计建议

- 新 offset 应进入版本化 manifest，并在遇到未知 addon 时明确停用，而不是
  默默向未知地址写内存；
- 安装流程必须可逆：备份放在 Release 包之外，未来安装器绝不能删除现有游戏文件；
- 日志是诊断信息，不等于证明某种画面风格已经启用。回报问题时收集
  `dlss5-loader.log`、`ReShade.log`、`DirectXHook.log`、
  exact tag 和运行时哈希；
- CI 应保持确定性：固定 action 主版本、使用 Windows runner，并且只发布
  tagged commit 产出的 artifact。
