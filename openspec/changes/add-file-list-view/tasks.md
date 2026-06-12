## 1. 项目配置与依赖

- [ ] 1.1 在 `Resource.h` 中添加 `IDC_MAIN_LISTVIEW` 控件 ID 宏定义（值为 1001）
- [ ] 1.2 在 `framework.h` 中添加 `#include <commctrl.h>` 头文件
- [ ] 1.3 在 `framework.h` 中添加 `#pragma comment(lib, "Comctl32.lib")` 链接指令

## 2. Common Controls 初始化

- [ ] 2.1 在 `WindowsProject1.cpp` 的 `wWinMain` 函数中，在 `MyRegisterClass` 调用之前添加 `INITCOMMONCONTROLSEX` 结构体初始化并调用 `InitCommonControlsEx`，标志设置为 `ICC_LISTVIEW_CLASSES`

## 3. ListView 控件创建

- [ ] 3.1 提取 ListView 创建逻辑为独立函数 `HWND CreateListView(HWND hWndParent)`，在该函数中调用 `CreateWindowExW` 创建 `SysListView32` 子窗口，样式为 `WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS`
- [ ] 3.2 在 `CreateListView` 函数中，使用 `LVCOLUMNW` 结构体和 `ListView_InsertColumn` 宏依次插入 4 列：「路径」（初始宽度 300px）、「类型」（初始宽度 120px）、「大小」（初始宽度 120px）、「标签」（初始宽度 200px）
- [ ] 3.3 在 `InitInstance` 函数中，在 `ShowWindow` 调用之前调用 `CreateListView` 创建 ListView，并将返回的 HWND 保存到全局变量 `g_hwndListView`

## 4. 窗口消息处理

- [ ] 4.1 在 `WndProc` 中添加 `WM_SIZE` 消息处理分支：当 `wParam != SIZE_MINIMIZED` 时，获取客户区尺寸并调用 `MoveWindow(g_hwndListView, 0, 0, cx, cy, TRUE)` 使 ListView 铺满客户区
- [ ] 4.2 将全局变量 `g_hwndListView` 声明为 `HWND` 类型，初始值为 `nullptr`

## 5. 构建验证

- [ ] 5.1 编译项目，确认无编译错误和链接错误
- [ ] 5.2 运行程序，确认 ListView 控件可见、列头名称正确（路径/类型/大小/标签）、且调整窗口大小时 ListView 自动跟随调整
