# Module Template — 模块模板

快速创建新模块的模板文件。

## 文件

| 文件 | 说明 |
|------|------|
| `src/my_module.h` | 模块头文件 |
| `src/my_module.cc` | 模块实现 |

## 使用

```bash
# 复制模板
cp -r template/src my_new_module/
cd my_new_module

# 重命名类和方法
# MyModule → YourModule
# my_module → your_module
```

## 回调说明

```cpp
struct MyModule : Module {
    // 构造函数：指定模块名、版本、文件名
    MyModule() : Module("my_module", "1.0.0", "my_module") {}

    bool onLoad() override;           // 模块加载
    bool onUnload() override;         // 模块卸载（做业务清理）
    bool onServerReady() override;    // 服务器就绪（注册 handler）
    bool onServerUp() override;       // 服务器启动完成
};
```

## 注意事项

- `onUnload` 中注销所有在 `onServerReady` 中注册的回调/handler，避免热重载时旧 `.so` 卸载后函数指针悬垂
- 全局 `extern "C"` 导出 `CreateModule` / `DestroyModule`
