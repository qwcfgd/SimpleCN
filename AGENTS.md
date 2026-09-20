# 构建与产物目录

按用户最新全局要求，以原工程 `C:/Documents/0_Qt/Qt-GeneralController` 为基准：

- Qt 5 / Qt 6 的程序本体与必要运行依赖分别放在 `../build/Qt-GeneralController-qt5`、`../build/Qt-GeneralController-qt6`。
- CMake 配置、缓存、编译中间文件、测试程序、调试工具、测试素材及日志统一放入 `../build/qttemp`，按工程和 Qt 版本隔离。
- 使用本仓库 CMakePresets.json；不在源码目录创建 build，不把测试或调试文件部署到程序目录。
- 固定路径不随 worktree 改变。以上更新替代此前将 CMake 构建树与程序放在同一目录的约定。
