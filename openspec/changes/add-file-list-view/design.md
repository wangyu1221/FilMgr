## Context

当前项目是一个 Win32 C++ 桌面应用，使用原始 Win32 API（非 MFC），主窗口通过 `CreateWindowW` 创建，客户区目前为空白，仅处理 `WM_PAINT` 消息。

需要添加一个 ListView 控件作为主界面的核心数据展示组件，用于显示文件路径、类型、大小和标签信息。

## Goals / Non-Goals

**Goals:**
- 在主窗口客户区创建 Report 模式的 ListView 控件，铺满整个客户区
- ListView 包含四列：路径、类型、大小、标签
- 窗口大小变化时 ListView 自动调整尺寸
- 初始化 Common Controls 库以支持 ListView

**Non-Goals:**
- 不涉及实际文件数据的加载（本次只搭建 UI 框架）
- 不涉及列排序功能
- 不涉及右键菜单或文件操作功能
- 不涉及标签编辑功能

## Decisions

### 1. 使用 SysListView32（Report 模式）而非 ListBox
- **理由**：需要展示多列数据（路径、类型、大小、标签），Report 模式的 ListView 天然支持多列表格展示，是 Win32 中实现此类需求的标准方案
- **替代方案**：自绘控件（过于复杂）、第三方 UI 库（引入额外依赖）

### 2. 使用 `InitCommonControlsEx` 初始化 Common Controls v6
- **理由**：ListView 属于 Common Controls 的一部分，必须先初始化才能创建。使用 `ICC_LISTVIEW_CLASSES` 标志即可
- **替代方案**：`InitCommonControls()`（已过时，不推荐）

### 3. ListView 作为子窗口，通过 `WM_SIZE` 消息调整尺寸
- **理由**：将 ListView 作为主窗口的子控件，在 `WM_SIZE` 消息处理中使用 `MoveWindow` 调整其大小为客户区尺寸。这是 Win32 中最简洁的全屏子控件实现方式
- **替代方案**：使用 `WS_EX_CLIENTEDGE` 样式让 ListView 有凹陷边框，视觉上更清晰

### 4. 列宽初始分配策略
- **理由**：四列初始宽度按比例分配，路径列占 40%、类型列占 15%、大小列占 15%、标签列占 30%，后续可随窗口大小调整（本次先实现固定宽度，后续可扩展为自动调整）

## Risks / Trade-offs

- **[风险] Common Controls v6 需要 manifest** → 在 `framework.h` 中通过 `#pragma comment` 链接 Comctl32.lib，并使用 `InitCommonControlsEx`。如果运行时未启用 v6 样式，ListView 仍可使用但外观为旧版样式，不影响功能
- **[风险] 窗口缩小时 ListView 可能出现滚动条** → 通过 `LVS_REPORT | LVS_NOCOLUMNHEADER` 或合适的样式组合避免不必要的滚动条，或接受滚动条作为正常行为
- **[取舍] 本次不实现列宽自动调整** → 先以固定初始宽度完成基础功能，列宽自适应可作为后续增强

## Open Questions

- 无（本次需求明确，技术方案成熟）
