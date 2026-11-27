# PDF view

Windows x64 本地 PDF、Word 和图片阅读批注工具。

当前版本为 0.6.3。本地构建安装包位于 `artifacts/PDF-view-Setup-0.6.3-x64.exe`。 [使用说明](docs/USER_GUIDE.zh-CN.md)

DOC/DOCX 需要桌面版 Microsoft Word 或兼容 WPS。默认原生模式由 Office 排版，直接绘制页面，不生成中间 PDF；PDF 转换保留为显式可选模式。两种方式都在软件内打开，都能使用现有画笔、旧批注显示、橡皮、撤销重做和手动保存。原文档不变。

齿轮设置支持自动侧边栏、会话恢复、滚动虚化等级、滑动灵敏度、Word 打开方式、转换引擎与快捷键绑定。原生 Word 查找目前按匹配页定位。字体、旧公式组件依赖本机 Office，详见使用说明。

画笔与橡皮互斥，再次点击当前工具回到阅读。全局记忆颜色与粗细。开启屏幕线宽补偿时，当天手动选择优先，北京时间次日第一笔恢复为可自定义的每日重置线宽。相关设置位于“画笔与校准”页。

设置中的“功能教程”提供六页离线说明和数理化练习，预置可编辑的画笔与旧高亮示例，重新打开可继续练习。

实现采用 C++20、Win32、Direct2D、PDFium 与 Office 原生页面绘制接口；分块渲染、有界缓存和过时任务取消。Word 原生渲染、可选转换、设置与持久化分别由独立对象负责。

## 构建

Linux 需要 Python 3、MinGW-w64 x64 POSIX 与 NSIS：

```sh
python3 scripts/fetch_pdfium.py
python3 scripts/build_windows.py
```

输出 `artifacts/PDF-view-Setup-0.6.3-x64.exe`，可传 `--toolchain-root`。
Windows 可直接通过 CMake/MSVC 构建；GitHub Actions 在 Windows 上测试、打包，推送版本标签时发布带 setup 的 Release。

```sh
cmake -S . -B build-native
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

Windows 额外运行 `native_office_smoke` 和 `settings_ui_smoke`。原生渲染回归使用模拟 Office 自动化对象返回真实原生页面绘图数据，检查只读加载、分页、绘制、旋转、查找与批注存取，不替代真实 Word/WPS 的公式及触屏验收。
