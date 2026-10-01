# Config — 配置系统

基于 YAML / XML 的类型安全配置系统，支持热加载和变更回调。

## 核心类

- **Config** — 静态配置加载入口
- **ConfigVar<T>** — 类型安全的配置变量模板
- **ConfigVarBase** — 配置变量基类，管理变更回调

## 使用

```cpp
// 声明配置
static ConfigVar<int>::ptr g_port = Config::Lookup("server.port", 8080, "server port");

// 读取
int port = g_port->getValue();

// 设置
g_port->setValue(9090);

// 监听变更
g_port->addListener([](const int& old_val, const int& new_val) {
    INFO(logger) << "port changed from " << old_val << " to " << new_val;
});
```

## 配置加载

```cpp
Config::LoadFromConfDir("/path/to/conf");  // 加载目录下所有 .yml / .xml
Config::LoadFromConfDir(path, true);       // force 模式，触发所有 onChange 回调
Config::LoadFromFile("/path/to/conf/a.yml"); // 加载单个文件（内部按 mtime 去重）
```

## 热加载

框架通过 inotify 监听配置目录，`.yml` / `.xml` 文件变更后调用 `Config::LoadFromFile()`
只重新加载该文件，并触发对应 `ConfigVar` 的变更回调。业务模块如何声明配置项、注册回调
以及热重载时移除回调的注意事项，见 [`chen/watcher/README.md`](../watcher/README.md)。

## XML 配置

XML 在加载时会被转换为 YAML 节点树后复用同一套加载流程，`LexicalCast` 与自定义类型均无需改动。

映射规则：

- 无子元素的节点 → 标量（取元素文本内容）。
- 子元素标签名互不相同 → map（标签名作为 key）。
- 同名子元素重复出现（≥2）→ 序列（list）。
- 唯一子元素标签为保留标记 `item` / `entry` / `value` → 单元素序列。
- 属性（attribute）忽略。

等价于 `system.yml` 的 XML 示例：

```xml
<config>
  <server>
    <work_path>/srv/app</work_path>
    <pid_file>app.pid</pid_file>
  </server>
  <servers>
    <server>
      <address><item>0.0.0.0:8090</item></address>
      <keepalive>1</keepalive>
      <name>game_http_server</name>
      <type>http</type>
    </server>
  </servers>
  <redis>
    <config>
      <blog>
        <host>127.0.0.1:6379</host>
        <type>fox_redis</type>
      </blog>
    </config>
  </redis>
</config>
```

> 注意：单元素列表必须使用 `item` / `entry` / `value` 作为子元素标签，否则会被识别为 map。

## 支持类型

- 基础类型：`int`, `uint64_t`, `float`, `double`, `bool`, `std::string`
- 复合类型：`std::vector<T>`, `std::list<T>`, `std::set<T>`, `std::unordered_set<T>`
- 自定义类型：实现 `LexicalCast<T, std::string>` 即可
