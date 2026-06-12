## ADDED Requirements

### Requirement: ListView 控件创建
系统 SHALL 在主窗口创建完成后，于客户区内创建一个 Report 模式的 `SysListView32` 子窗口控件。该控件 SHALL 使用 `WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS` 样式。

#### Scenario: 主窗口启动后 ListView 可见
- **WHEN** 应用程序启动并显示主窗口
- **THEN** 主窗口客户区内显示一个 ListView 控件，控件具有可见的列头区域

### Requirement: ListView 列定义
ListView SHALL 包含且仅包含以下 4 列，列顺序从左到右依次为：
1. 「路径」— 显示文件/目录路径
2. 「类型」— 显示文件或文件夹类型
3. 「大小」— 显示文件大小
4. 「标签」— 显示用户自定义标签

每列 SHALL 具有合理的初始宽度，所有列的总宽度大致等于 ListView 客户区宽度。

#### Scenario: 列头名称正确显示
- **WHEN** ListView 控件创建完成
- **THEN** 列头从左到右依次显示为「路径」「类型」「大小」「标签」

### Requirement: ListView 占满整个客户区
ListView 控件 SHALL 始终占据主窗口客户区的全部可用区域，无上下左右边距。

#### Scenario: 窗口初始显示时 ListView 铺满客户区
- **WHEN** 主窗口以初始大小显示
- **THEN** ListView 控件的边界与主窗口客户区边界重合

#### Scenario: 用户调整窗口大小时 ListView 跟随调整
- **WHEN** 用户拖动窗口边框改变主窗口大小
- **THEN** ListView 控件的宽度和高度立即更新，始终铺满整个客户区

### Requirement: Common Controls 初始化
应用程序 SHALL 在创建任何窗口之前调用 `InitCommonControlsEx`，并传入 `ICC_LISTVIEW_CLASSES` 标志，以确保 ListView 控件类已注册可用。

#### Scenario: Common Controls 在窗口创建前初始化
- **WHEN** 应用程序启动，进入 `wWinMain`
- **THEN** 在调用 `MyRegisterClass` 之前，`InitCommonControlsEx` 已成功执行

### Requirement: Comctl32 库链接
项目 SHALL 链接 `Comctl32.lib`，以提供 ListView 相关的 API 函数（如 `ListView_InsertColumn`、`ListView_SetColumnWidth` 等宏/函数）。

#### Scenario: 项目构建时无链接错误
- **WHEN** 项目编译构建
- **THEN** 不存在与 Common Controls API 相关的未解析外部符号错误
