# PDF view

[下载 Windows 64 位安装包（Setup.exe）](downloads/PDF-view-Setup-0.3.0-x64.exe?raw=true)

0.3.0：圆形颜色面板、实时笔迹预览和连续粗细滑块；修复滚动时工具栏重复擦除重绘，以及撤销／重做按钮反复启用和禁用导致的闪烁。

面向 Windows 10 的本地 PDF 阅读与批注工具：Edge 风格工具栏、目录与缩略图、多标签、绘制开关、鼠标与触控笔迹、双指移动缩放、整笔橡皮、撤销重做、搜索及自动保存。无打印功能，原始 PDF 不变。

采用 C++20、Win32、Direct2D、PDFium。文档模型、坐标变换、手势、存储、后台渲染调度和缓存由独立对象负责。使用按需分块渲染、过时任务取消和有界缓存。批注存储在本机原子替换文件中。

## 构建

Linux 需要 Python 3、MinGW-w64 x64 POSIX 编译器和 NSIS。

```sh
python3 scripts/fetch_pdfium.py
python3 scripts/build_windows.py
```

输出 `artifacts/PDF-view-Setup-0.3.0-x64.exe`。非系统安装的工具链可传 `--toolchain-root`。

核心测试：`cmake -S . -B build-native && cmake --build build-native && ctest --test-dir build-native --output-on-failure`。

测试样例生成脚本需要 reportlab、pypdf、Pillow。Windows 程序可通过 `PaperView.exe --self-test sample.pdf output-directory` 验证实际 PDF 引擎。GUI 冒烟脚本依赖测试环境中的 Wine、Xvfb 和 xdotool；先将 `tests/windows_ui_probe.cpp` 用 MinGW 编译为 `build-windows/windows_ui_probe.exe`，以原生窗口 API 验证窄窗口布局。

见 [使用说明](docs/USER_GUIDE.zh-CN.md)。真实 Win10 触控和目标硬件性能仍需实机验收。
