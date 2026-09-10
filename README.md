# DS2LE DLSS5 接入

[中文（当前页面）](README.md) · [English](README.en.md)

这是一个开源的 DINPUT8 代理，将 DS2LightingEngine（DS2LE）的 DirectX 11
DLSS 路径接入社区版 DLSS Neural Rendering（DLSSNR）运行时。

项目面向已经安装 DS2LE 光追版的《黑暗之魂 II：原罪学者》玩家。项目源码会
构建代理 DLL 和本项目维护的 pre-SR NR bridge carrier；项目不包含 DS2LE、
游戏、ReShade、RenoDX 或 NVIDIA 运行时。

## Release 包含什么

每个 GitHub Release 包含：

- `DINPUT8.dll`：本项目代理；
- `dlss5-bridge.addon64`：本项目已在真实场景验证的 pre-SR NR carrier；
- `ReShade.ini.example`：需要合并到游戏配置的示例；
- `dlss5-bridge.cfg.example`：启用 pre-SR NR 的配置示例；
- `PRE-SR-NR-BUILD.md`：carrier 构建和验证记录；
- `README.md` / `README.en.md`：中英文说明；
- `INSTALL.txt` / `INSTALL.en.txt`：中英文快速安装；
- `LICENSE`、`NOTICE.md`。

第三方 DLL 和 addon 不会被重新分发。玩家需要从下方的上游页面自行下载，
并遵守各自的许可和发布说明。本项目的 carrier 由本仓库源码构建，不需要
另外下载上游 Bridge。

## 使用要求

- Windows 10/11 x64；
- Steam 版《Dark Souls II: Scholar of the First Sin》；
- 先安装能正常运行的 DS2LightingEngine（DS2LE）光追版。请参阅
  [DS2LightingEngine Nexus 页面](https://www.nexusmods.com/darksouls2/mods/1146)；
- NVIDIA RTX 显卡，以及与显卡匹配的 DLSSNR 运行时。当前项目已验证的是
  RTX 40 + `310.8.0-RTX40`。

### 已验证组合

| 组件 | 已验证版本 |
| --- | --- |
| DS2LE | Path Tracing 公测版 `0.1` |
| RenoDX DLSS5 | `4.60`（通常称 v4.6） |
| DLSS5 Bridge | 本项目 pre-SR NR carrier（基于 `v1.4.12` / `5050b04`） |
| DLSSNR | `310.8.0-RTX40` |
| 显卡 / 驱动 | RTX 4070 SUPER / NVIDIA 616.64 |

代理中的内存偏移绑定 RenoDX `4.60`。不要把 addon 随意升级到其他版本后
直接继续使用；addon 升级必须重新做兼容性验证。

## 需要另行下载的运行时资源

请保留上游文件的原始发布说明和许可证：

| 文件 | 下载来源 | 放置位置 |
| --- | --- | --- |
| DS2LE 光追文件（包括 ReShade `dxgi.dll` 宿主） | [DS2LightingEngine Nexus](https://www.nexusmods.com/darksouls2/mods/1146) | 按作者说明完整安装 |
| `renodx-dlss5.addon64`（`4.60`） | [RankFTW/rhi-repo — RenoDX DLSS5 4.60](https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss5-4.60) | `Game\` |
| `nvngx_dlssnr.dll`（`310.8.0-RTX40`） | [RankFTW/rhi-repo Releases](https://github.com/RankFTW/rhi-repo/releases) | `Game\` |

Release 包中的 `dlss5-bridge.addon64` 是本项目源码构建并在真实场景验证的
pre-SR NR carrier，不需要另外下载上游 Bridge。

DS2LE 自带的 `nvngx_dlss.dll`、`nvngx_dlssd.dll`、`nvngx_dlssg.dll` 不需要
替换。本项目只增加 DLSSNR 路径。`Game\` 中每个 addon 只保留一份。

## 安装步骤

1. 退出游戏和 Steam。
2. 先安装 DS2LE，并启动一次确认原有光追和 `Game\dxgi.dll` 正常。
3. 将 `Game\DINPUT8.dll`、`Game\dxgi.dll`、`Game\ReShade.ini` 备份到游戏目录之外。
4. 将原版游戏输入代理重命名为 `Game\dinput8_orig.dll`。新代理通过这个
   文件名转发游戏原本的六个 DINPUT8 导出。
5. 将 Release 包中的 `DINPUT8.dll` 复制到 `Game\`。
6. 将包中的 `dlss5-bridge.addon64`，以及按上游链接下载的
   `renodx-dlss5.addon64`、匹配的 `nvngx_dlssnr.dll` 复制到同一个 `Game\` 目录。
7. 将 `dlss5-bridge.cfg.example` 中的 `pre_sr_nr=1` 合并到
   `Game\dlss5-bridge.cfg`；保留 Bridge 的其他配置项。
8. 打开现有的 `Game\ReShade.ini`，把 `ReShade.ini.example` 中的两个段合并
   进去。不要覆盖整个文件，以免丢失 DS2LE 设置。基础配置如下：

   ```ini
   [RenoDX.DLSS5]
   EnableHooks=2
   NRStyle=1
   NREnableUpscaling=0
   NeuralUplift=1

   [DLSS5Proxy]
   EventsBridge=0
   EventsRenodx=1
   EventsDxgi=1
   ```

9. 使用正常 Steam 快捷方式启动游戏。在 DS2LE 的 F1 菜单中保持抗锯齿为
   **NVIDIA DLSS**；启用神经渲染前先确定最终分辨率和显示模式。

代理会在游戏目录生成 `dlss5-loader.log`。成功时还应能在 `ReShade.log`、
`DirectXHook.log` 或 `dlss5-bridge.log` 中看到 DLSSNR 初始化和 feature 活动。

## 游戏内按键

- **F1**：DS2LE 设置菜单；
- **F2**：本项目实时配置窗口；
- **F5**：RenoDX 截图（约按住 1 秒）；
- **F6**：开关神经渲染（约按住 1 秒）。

F2 面板会写入 `ReShade.ini`。大多数逐帧参数约 1 秒内生效；`NREnableUpscaling`
等 feature 创建期参数需要重启。`pre_sr_nr=1` 时，carrier 先执行同分辨率
feature-18 NR，再把结果交给原生 feature-1 SR。已验证的 RTX 40 运行时可能拒绝升频契约
（`0xBAD00005`），随后回退到原生神经渲染，这是运行时能力限制。

## 故障排查与回滚

**启动或进入场景时崩溃**

- 确认每个 addon 只有一份；
- 确认使用的是包内本项目 carrier，而不是另一个未经验证的 Bridge；
- 确认 RenoDX 是已验证的 `4.60`；
- 保持 `[DLSS5Proxy] EventsBridge=0`，当前 LE 宿主中 Bridge 事件回调不安全；
- 临时将 `NRStyle` 改为 `0`，再查看新生成的日志。

**日志中没有 DLSSNR**

- 确认 DS2LE 的 `dxgi.dll` 仍在 `Game\`；
- 确认原版输入代理名为 `dinput8_orig.dll`；
- 确认 F1 菜单使用 NVIDIA DLSS；
- 确认所有 addon 和 DLL 都是 64 位，并且直接放在 `Game\`。

**恢复原状**

1. 退出游戏；
2. 删除本项目的 `DINPUT8.dll` 和包内的 `dlss5-bridge.addon64`；
3. 恢复备份的原版 `DINPUT8.dll`、`ReShade.ini`，必要时恢复 `dxgi.dll`。

本项目不会修改游戏资源文件。

## 从源码构建

代理使用不依赖 CRT 的 C 代码，通过 Microsoft x64 编译器和 GNU `ld` 生成
DINPUT8 转发器；bridge carrier 使用 Microsoft x64 C++ 编译器构建。

依赖：

- Visual Studio 2022 C++ x64 Build Tools；
- [MSYS2](https://www.msys2.org/) UCRT64 `binutils`（提供 `ld.exe`）。
- ReShade SDK 头文件（仅编译依赖，不会进入发布包）；可从
  [ReShade 源码](https://github.com/crosire/reshade) 的 `include/` 目录获取。

在 PowerShell 中运行：

```powershell
cmd /c src\proxy\build.cmd
cmd /c src\bridge\build.cmd
```

`DLSS5_RESHADE_INCLUDE` 必须指向包含 `reshade\reshade_events.hpp` 的目录。
输出文件分别为 `src\proxy\DINPUT8.dll` 和
`src\bridge\dlss5-bridge.addon64`。代理脚本会通过 `vswhere` 定位 Visual
Studio，并支持用 `DLSS5_LD` / `DLSS5_LIB` 覆盖链接器路径；CI 使用这些变量
和临时下载的固定 ReShade 头文件构建。

## GitHub Actions 发布

[`.github/workflows/build-release.yml`](.github/workflows/build-release.yml) 会在
Pull Request 和 `v*` tag 上运行：

1. Windows runner 临时获取固定版本的 ReShade SDK 头文件；
2. 使用 MSYS2 UCRT64 编译本项目代理，并编译本项目 carrier；
3. 检查仓库和玩家 ZIP 中没有 DS2LE、RenoDX、NVIDIA 或其他第三方运行时；
4. 生成包含两个本项目运行文件的玩家 ZIP 和 SHA-256 清单；
5. 对 `v*` tag 自动创建或更新 GitHub Release。

发布新版本：

```powershell
git add .
git commit -m "Prepare release"
git tag -a v0.1.0 -m "Initial player release"
git push origin main --follow-tags
```

每次 RenoDX 或 DS2LE 主机布局变化都应更新兼容性说明并重新测试后再打 tag。

## 开发建议

- 当前代理会在内存中修改 LE `dxgi.dll` 的导出表和事件派发表，因此它是
  针对特定版本的窄适配层，不是通用 ReShade 兼容层；
- 源码与闭源运行时分离，保持干净 checkout 可复现，并明确许可证边界；
- 后续最值得做的是版本化 offset manifest、PE 导出自动化冒烟测试，以及
  只负责备份/恢复而不下载第三方二进制的安装器。

## 许可与致谢

代理源码采用 MIT 许可证，见 [LICENSE](LICENSE)。第三方组件保持各自许可，
边界说明见 [NOTICE.md](NOTICE.md)。

- [DS2LightingEngine](https://www.nexusmods.com/darksouls2/mods/1146)
- [NIGos/dlss5-bridge](https://github.com/NIGos/dlss5-bridge)
- [RankFTW/rhi-repo](https://github.com/RankFTW/rhi-repo)
- [ReShade](https://reshade.me/)
