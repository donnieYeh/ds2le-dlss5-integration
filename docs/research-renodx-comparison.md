# RenoDX 与 DS2LE-DLSS5-Integration 对照研究

## 研究范围与基线

本记录只使用官方仓库、官方源码和官方 GitHub Actions 作为证据：

- RenoDX：[`clshortfuse/renodx`](https://github.com/clshortfuse/renodx)，审计提交
  [`cd32113a98608e63027d40910cfe296a14dfe228`](https://github.com/clshortfuse/renodx/tree/cd32113a98608e63027d40910cfe296a14dfe228)
  （2026-09-05）。
- 本项目：[`donnieYeh/ds2le-dlss5-integration`](https://github.com/donnieYeh/ds2le-dlss5-integration)，审计提交
  [`82b93d7f0d53781cff675a201b3f83a36b85069d`](https://github.com/donnieYeh/ds2le-dlss5-integration/tree/82b93d7f0d53781cff675a201b3f83a36b85069d)。

目标不是比较两个项目的画质调校，而是判断 RenoDX 能否直接替代当前项目为
《Dark Souls II: Scholar of the First Sin》（SotFS）的 DS2LightingEngine（DS2LE）
接入 DLSS5/DLSS Neural Rendering（DLSSNR）所做的宿主兼容、运行时装载和玩家发布工作。

## 结论

**RenoDX 不能直接实现当前目标；仍需要针对 DS2LE/SotFS 自行扩展，或者保留当前
DINPUT8 代理。**

RenoDX 可以复用为一个很有价值的“通用 DirectX/ReShade addon 开发框架”，并且已经
包含通用 DLSS/Streamline hook 工具，但官方提交中没有 SotFS/DS2LE 适配、DLSS5
Bridge 装载、`nvngx_dlssnr.dll` 路径，或针对 DS2LE 精简 ReShade 宿主的兼容层。当前
项目的关键工作正是补上这些缺口，而不是重新实现 RenoDX 的 shader/HDR 框架。

换句话说：

- **想要继续使用当前已验证的 DS2LE + RenoDX DLSS5 4.60 + DLSS5 Bridge + DLSSNR
  组合：** 继续使用当前项目的代理最稳妥。
- **想把代码并入 RenoDX：** 可以 fork/上游贡献一个 DS2LE/SotFS game addon 和一层
  DS2LE host compatibility，但这仍是开发项目，不是下载 RenoDX 后即可使用。
- **想做面向玩家的发布包：** RenoDX 的 Actions 可以参考，但仍需单独处理 SotFS
  安装布局、第三方文件边界、版本锁定、回滚和懒人包。

## 2026-09-08 实测：完整 ReShade 宿主与 direct D3D11 addon

为区分“direct addon 本身有问题”和“DS2LE 宿主不兼容”，我从
`clshortfuse/renodx` 的开发产物/工具链中取了完整 ReShade 宿主，并在同一台
RTX 4070 SUPER 上做了隔离启动：

1. 用官方 ReShade 6.8 full-add-on 宿主替换 DS2LE 的 `dxgi.dll`；
2. 只加载 `renodx-dlss.addon64` SF 0.53；
3. 再加载 RenoDX DevKit addon，观察 D3D11 设备、pipeline layout 和 shader dump。

结果是 direct addon 可以注册（ReShade API version 18）、完成 `init_device`、
`init_swapchain`、`ResizeBuffers` 和连续 `Present`，在观察窗口内没有之前 DS2LE
宿主下的访问冲突。DevKit 也成功枚举 D3D11 设备并启动 MCP named pipe；它尝试建立
HDR proxy swapchain 时因窗口/权限返回 `0x80070005`，但不影响主渲染继续运行。

这把问题进一步定位为：**DS2LE 自带的精简 ReShade 宿主/设备包装与 direct addon
期望的完整 ReShade ABI 不兼容。** 不过完整替换 `dxgi.dll` 同时绕过了 DS2LE
LightingEngine，本次实验不能当作保留光追功能的玩家方案；并且该次启动没有加载
`nvngx_dlss.dll`，所以它证明的是 host/addon 兼容性，不是已经证明 DLSS SR 在游戏
内完成评估。

## RenoDX 已经提供的能力

### 1. 通用 DirectX/ReShade addon 框架

RenoDX README 将自身定位为 DirectX 游戏 mod 工具集，列出的现成能力包括替换
shader、注入 buffer、overlay、swapchain/texture 升级和写入用户设置；它选择
ReShade addon API 来完成 DirectX 接入。见官方
[`README.md`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/README.md#L196-L212)。

这部分适合承载未来的游戏画面调校、HDR、shader 替换和 UI 设置，但它解决的是
“如何在正常 ReShade addon 宿主中开发 mod”，不是“如何修复 DS2LE 自带的特殊宿主”。

### 2. 正常 ReShade addon 的编译体系

官方开发文档明确说 ReShade Addon API 是 RenoDX 的核心依赖，并要求常规的
ReShade 环境；同一文档还说明 `src/games/<game>/addon.cpp` 会被识别为游戏 mod，
输出为 `renodx-<target>.addon64` 或 `.addon32`。见
[`docs/CONTRIBUTING.md`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/docs/CONTRIBUTING.md#L197-L210)
和
[`CMakeLists.txt`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/CMakeLists.txt#L2795-L2875)。

因此，RenoDX 的默认产物是 addon 模块，不是本项目所需的 `DINPUT8.dll` 输入代理，
也不会自动接管 `dinput8_orig.dll` 的原始导出。

### 3. 通用的 DLSS/Streamline 修复 addon

RenoDX 当前有 `src/addons/dlssfix/addon.cpp`。它读取 `DLSSPath` 和
`StreamlinePath`，默认值分别是 `nvngx_dlss.dll` 与 `sl.interposer.dll`，然后调用
通用的 `dlss_hook`。见官方
[`dlssfix/addon.cpp`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/addons/dlssfix/addon.cpp#L794-L820)。

该 hook 实现对模块中的标准 NGX 初始化、创建/评估 feature 等导出进行 vtable hook，
同时处理 Streamline interposer；见
[`dlss_hook.hpp`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/utils/dlss_hook.hpp#L781-L838)
和
[`nvngx.hpp`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/utils/dlss/nvngx.hpp#L796-L821)。

这对研究 DLSS 调用链很有帮助，但要注意边界：审计提交的源码以
`nvngx_dlss.dll`/标准 `NVSDK_NGX_*` 为目标，没有显式的 `nvngx_dlssnr.dll`、DLSS5
Bridge 或 `renodx-dlss5.addon64` 适配。仅凭文件名配置，不能证明 DLSSNR 运行时与
现有 hook 完全 ABI/API 兼容；若要依赖它，必须另做运行时验证或扩展 hook。

### 4. 已有的游戏 mod 示例与构建流水线

RenoDX 有很多 `src/games/<game>` 目录，这证明它适合扩展成一个具体游戏 addon。
不过官方 `darksouls` 条目是 **Dark Souls: Remastered**，metadata 中的 Steam AppID
是 `570940`，并非 DS2 SotFS；见
[`src/games/darksouls/metadata.json`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/games/darksouls/metadata.json#L236-L267)。

仓库中另有 `darksiders2`，其 addon 自报名称是 **Darksiders 2**，也不是
Dark Souls II；见
[`src/games/darksiders2/addon.cpp`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/games/darksiders2/addon.cpp#L449-L450)。

我在上述固定提交的 README、CMake、`src/addons`、`src/utils` 和 `src/games` 中检索
了 `DLSS5`、`DLSSNR`、`nvngx_dlssnr`、`dlss5-bridge`、`DS2LE`、`Scholar of the
First Sin` 等关键词；没有发现针对 SotFS/DS2LE/DLSS5 Bridge 的现成实现。这是对该
提交源码快照的审计结果，不代表 RenoDX 未来版本不会增加相关功能。

## 当前项目实际补上的缺口

### 1. 保留原始输入代理并装载 addon

当前项目的代理源码说明并实现了两件 RenoDX addon 本身不会完成的工作：

1. 将原版输入代理转发到 `dinput8_orig.dll`；
2. 以普通 DLL 方式加载 `dlss5-bridge.addon64` 和 `renodx-dlss5.addon64`，避免再创建
   一个 ReShade 实例。

见本项目 [`src/proxy/dinput8.c`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L1-L12)
和
[`loader_thread`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L1380-L1431)。

### 2. 修复 DS2LE 宿主不完整的 ReShade API

DS2LE 的 `dxgi.dll` 宿主不是 RenoDX 文档假定的完整 ReShade addon 宿主：当前项目
需要在内存中扩展导出表，补上 13 个 ReShade API 导出，并把 6 个原本是 stub 或
无法工作的导出重定向到代理实现。代理还维护多回调 fan-out 和 shutdown/watchdog
逻辑，以适配 LE 的单槽事件派发和初始化时清空行为。

这些是对特定 DS2LE host 布局的兼容工作，见本项目
[`dinput8.c` 的 ReShade shim 注释和实现](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L106-L191)、
[`g_shims/g_redir`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L1124-L1188)
和
[`watchdog_thread`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L1124-L1152)。

RenoDX 的官方源码没有同等的 DS2LE host shim；直接把 RenoDX 自己编出来的
`renodx-*.addon64` 放进当前 LE 环境，不能替代这些处理。

### 3. 针对已验证 RenoDX DLSS5 版本的适配

当前项目的配置 poke 表保存了 `renodx-dlss5.addon64` v4.6 的版本绑定 RVA，并且
按该 addon 的实际全局变量布局实时更新 `RenoDX.DLSS5` 配置；见
[`g_poke`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L535-L576)
和项目 README 对已验证组合与版本锁定的说明：
[`README.md`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d#L34-L45)。

这类 RVA 绑定不是通用 RenoDX 功能。即便未来将代码改成 RenoDX addon，也需要为
每个支持的 DLSS5 addon 版本重新验证地址、配置时机和 shutdown 行为。

### 4. 面向玩家的发布边界

当前项目刻意只发布自己的代理和文档，不重新分发游戏、DS2LE、ReShade、Bridge、
RenoDX DLSS5 addon 或 NVIDIA DLSSNR runtime；见
[`NOTICE.md`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/NOTICE.md#L1-L17)。
它的 Actions 则专门编译代理、生成玩家 ZIP 和 SHA-256 清单，并在 tag 上创建 release；
见
[`build-release.yml`](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/.github/workflows/build-release.yml#L1-L121)。

RenoDX 的 `snapshot` workflow 可以作为 CI 参考：它分别编译 x64/x86 addon，复制
所有 `.addon64`/`.addon32` 到 release 目录，并通过 automatic release action 发布
snapshot；见官方
[`snapshot.yml`](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/.github/workflows/snapshot.yml#L487-L662)。
但这套流程面向 RenoDX 全仓库的 snapshot，不会自动生成 DS2LE SotFS 的安装包、
第三方运行时清单、备份/回滚逻辑或当前项目的懒人 7z 包。

## 能力矩阵

| 当前目标 | RenoDX 现成程度 | 仍需做的工作 | 判断 |
| --- | --- | --- | --- |
| DirectX/ReShade addon 基础设施 | 高 | 只需按 RenoDX 规范写 addon | 可复用 |
| DS2LE/SotFS 的游戏 mod | 无现成条目 | 新增 `src/games/darksouls2` 或独立适配，并确认 DS2LE 版本 | 需要开发 |
| DS2LE `dxgi.dll` 的缺失导出/API | 无证据支持 | 保留/移植当前 EAT shim、导出重定向和事件 fan-out，或修复宿主本身 | 需要开发 |
| 游戏 `DINPUT8.dll` 代理 | RenoDX addon 不是代理 | 继续维护当前 proxy，或另写兼容宿主 | 需要开发 |
| 标准 NGX/Streamline hook | 有 `dlssfix` 和通用 hook | 验证它是否适合 DS2LE 的设备代理与调用时序 | 可作为实验基础 |
| `nvngx_dlssnr.dll` + DLSS5 Bridge | 当前提交无现成实现 | 研究 DLSSNR ABI、Bridge 回调、加载顺序和事件安全性 | 需要开发/验证 |
| RenoDX DLSS5 v4.6 配置实时 poke | 无现成通用实现 | 保留当前版本绑定或上游设计稳定 API | 需要开发 |
| 玩家 ZIP/懒人 7z、安装指导、回滚 | 有通用 snapshot CI 参考 | 维护 SotFS 目录结构、第三方边界、hash、安装/回滚文档 | 需要项目自有发布逻辑 |

## 建议的技术路线

### 推荐：保持当前项目为独立桥接层

当前最小、风险最低的架构仍是：

```text
游戏
  └─ DINPUT8.dll（本项目 proxy）
       ├─ 转发原版 dinput8_orig.dll
       ├─ 修复 DS2LE dxgi.dll 的 ReShade API/事件宿主
       ├─ 加载 dlss5-bridge.addon64
       └─ 加载 renodx-dlss5.addon64
            └─ nvngx_dlssnr.dll（玩家另行安装）
```

RenoDX 作为上游运行时/addon 的依赖或协作项目即可，不需要把整个 RenoDX 源码树
复制进当前仓库。这样可以继续保持当前项目的第三方二进制不重新分发边界，也可以
对 DLSS5 版本和 DS2LE host 布局进行明确锁定。

### 可选：向 RenoDX 贡献 DS2LE/SotFS 支持

如果目标转为长期维护的通用方案，建议拆成三个明确层次：

1. **RenoDX game addon：** 只放 SotFS shader/HDR/设置逻辑，遵守其
   `src/games/<game>/addon.cpp` 构建约定；
2. **DS2LE host compatibility addon/proxy：** 继续解决外部 addon 装载、API 导出、
   事件多播和 shutdown 顺序，不假设完整 ReShade；
3. **DLSS5/DLSSNR bridge adapter：** 明确支持的 Bridge、RenoDX DLSS5 和
   `nvngx_dlssnr.dll` 版本，并用运行时测试锁定 ABI。

只有第 1 层属于 RenoDX 常规 game mod；第 2、3 层仍是当前项目的核心开发工作。

### 发布建议

- 不要把 RenoDX 的 snapshot 当作 SotFS 玩家包；另建项目专属 release staging。
- 在每个 release 中记录 DS2LE、RenoDX DLSS5、Bridge、DLSSNR 和驱动的已验证组合。
- 对 `DINPUT8.dll` 做 PE/导出/启动冒烟检查，并在实际 DS2LE 环境验证加载顺序。
- 继续不打包第三方运行时，提供官方上游链接、许可证边界、备份和回滚说明。
- 如果上游 RenoDX 未来提供稳定的 DLSSNR/DLSS5 API 或 DS2LE host 支持，再评估
  是否减少当前 proxy 的版本特定代码；在此之前不建议贸然替换已验证路径。

## English summary

### Verdict

RenoDX cannot directly replace this project for the current goal: connecting the
DS2LightingEngine Path Tracing build of *Dark Souls II: Scholar of the First Sin*
to the DLSS5/DLSSNR stack and shipping a player-ready package. The audited RenoDX
commit is a general DirectX/ReShade addon framework. It has a generic `dlssfix`
addon for `nvngx_dlss.dll` and `sl.interposer.dll`, but no audited source for
SotFS, DS2LE host compatibility, `nvngx_dlssnr.dll`, the DLSS5 Bridge, or the
current project's `DINPUT8.dll` proxy.

### What can be reused

- RenoDX's shader, buffer, swapchain, texture, overlay, settings, and ReShade
  addon infrastructure ([official README](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/README.md#L196-L212)).
- Its CMake conventions for building `renodx-*.addon64` game modules
  ([official CMake](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/CMakeLists.txt#L2795-L2875)).
- Its standard NGX/Streamline hook code as an investigation starting point
  ([DLSS hook](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/utils/dlss_hook.hpp#L781-L838)).
- Its x64/x86 build and snapshot-release workflow as CI inspiration
  ([official snapshot workflow](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/.github/workflows/snapshot.yml#L487-L662)).

### What still requires custom development

- A SotFS/DS2LE-specific game addon; RenoDX's `darksouls` entry is Dark Souls:
  Remastered (Steam AppID 570940), while `darksiders2` is Darksiders 2.
- A host compatibility layer for DS2LE's incomplete ReShade export/API and
  single-slot event dispatch. The current proxy extends the in-memory EAT,
  redirects six native exports, fans out callbacks, and restores slots after LE's
  initialization behavior ([current proxy](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c#L106-L191)).
- Explicit DLSSNR/Bridge loading, ABI validation, and version-specific DLSS5
  configuration handling.
- A dedicated player package, third-party license boundary, rollback guide, and
  lazy 7z archive.

### 2026-09-08 empirical host test

To separate a direct add-on fault from a DS2LE-host fault, I ran an isolated
test with the official ReShade 6.8 full add-on host and the SF 0.53
`renodx-dlss.addon64` binary. The add-on registered as ReShade API version 18,
completed device/swapchain initialization, survived `ResizeBuffers`, and kept
presenting without the access violation seen with the DS2LE host. The RenoDX
DevKit add-on also enumerated the D3D11 device, logged pipeline layouts, dumped
shaders, and opened its MCP named pipe (its optional HDR proxy swapchain failed
with `0x80070005` because of the test window/permission setup).

This narrows the failure to the **incomplete DS2LE ReShade host/device-wrapper
ABI**, rather than the direct add-on or NVIDIA runtime. Replacing `dxgi.dll`
does, however, bypass DS2LE's LightingEngine, so this is a compatibility proof,
not a player-ready replacement. The test session also did not load
`nvngx_dlss.dll`, so it does not by itself prove in-game DLSS SR evaluation.

The recommended approach is to keep the current proxy as a narrow DS2LE bridge,
reuse RenoDX where it is genuinely useful, and only migrate pieces after the
upstream project exposes stable APIs for this exact runtime path.

## 证据索引

所有 RenoDX 链接均固定到审计提交 `cd32113a98608e63027d40910cfe296a14dfe228`，以便
未来上游变化后重新比较：

1. [RenoDX README](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/README.md)
2. [RenoDX contributing/build setup](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/docs/CONTRIBUTING.md)
3. [RenoDX CMake addon discovery](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/CMakeLists.txt)
4. [RenoDX DLSS Fix addon](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/addons/dlssfix/addon.cpp)
5. [RenoDX DLSS hook utility](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/utils/dlss_hook.hpp)
6. [RenoDX NGX hook list](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/utils/dlss/nvngx.hpp)
7. [RenoDX Dark Souls metadata](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/games/darksouls/metadata.json)
8. [RenoDX Darksiders 2 addon](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/src/games/darksiders2/addon.cpp)
9. [RenoDX snapshot Actions](https://github.com/clshortfuse/renodx/blob/cd32113a98608e63027d40910cfe296a14dfe228/.github/workflows/snapshot.yml)
10. [本项目 proxy](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/src/proxy/dinput8.c)
11. [本项目运行时边界](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/NOTICE.md)
12. [本项目发布 Actions](https://github.com/donnieYeh/ds2le-dlss5-integration/blob/82b93d7f0d53781cff675a201b3f83a36b85069d/.github/workflows/build-release.yml)
