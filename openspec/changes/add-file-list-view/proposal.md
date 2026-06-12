## Why

主窗口目前为空白的 Win32 窗口，没有任何数据展示控件。需要添加一个全屏 ListView 控件，用于以列表形式展示文件/路径信息（路径、类型、大小、标签），为后续文件管理功能提供基础 UI。

## What Changes

- 在主窗口客户区创建一个 `SysListView32`（Report 模式）子控件，随窗口大小调整自动铺满整个客户区
- ListView 包含 4 列：「路径」「类型」「大小」「标签」
- 在 `WM_SIZE` 消息中处理 ListView 的尺寸调整，使其始终占满客户区
- 初始化 Common Controls（`InitCommonControlsEx`）以支持 ListView 控件
- 新增 `IDC_MAIN_LISTVIEW` 资源 ID 用于标识该控件

## Capabilities

### New Capabilities
- `file-list-view`: 主窗口全屏 ListView 控件，展示路径/类型/大小/标签四列数据

### Modified Capabilities
<!-- 无现有 spec 需要修改 -->

## Impact

- **代码**: `WindowsProject1.cpp` — 修改 `InitInstance`（创建 ListView）、`WndProc`（处理 `WM_SIZE`）、添加 ListView 初始化逻辑
- **头文件**: `Resource.h` — 新增 `IDC_MAIN_LISTVIEW` 控件 ID
- **依赖**: 需要链接 `Comctl32.lib`（Common Controls 库），在 `framework.h` 中添加 `<commctrl.h>` 头文件
- **清单**: 可能需要启用 Common Controls v6 的 manifest 以支持现代样式
