/**
 * @file module.h
 * @brief module模块
 * @author Christins
 * @date 2025-03-30
 * @copyright GPL-3.0
 */
#pragma once

#include <map>
#include <shared_mutex>
#include <vector>

#include "../game/game_server.h"
#include "../http/http_server.h"
#include "../http/ws_server.h"
#include "../rock/rock_stream.h"
#include "../rpc/rpc_server.h"
#include "../socket/socket_stream.h"
#include "../util/singleton.h"

namespace chen {

class Module {
public:
    typedef std::shared_ptr<Module> ptr;

    enum Type {
        MODULE = 0,
        ROCK = 1,
    };

    /**
     * @brief 构造函数
     * @param name 模块名称
     * @param version 模块版本
     * @param filename 模块文件名
     * @param type 模块类型
     */
    Module(const std::string& name, const std::string& version, const std::string& filename, uint32_t type = MODULE);

    /**
     * @brief 析构函数
     */
    virtual ~Module() {}

    /**
     * @brief 在参数解析之前调用
     * @param argc 参数个数
     * @param argv 参数值
     */
    virtual void onBeforeArgsParse(int argc, char** argv);

    /**
     * @brief 在参数解析之后调用
     * @param argc 参数个数
     * @param argv 参数值
     */
    virtual void onAfterArgsParse(int argc, char** argv);

    /**
     * @brief 模块加载时调用
     * @return bool 
     */
    virtual bool onLoad();

    /**
     * @brief 模块卸载时调用
     * @return bool
     */
    virtual bool onUnload();

    /**
     * @brief 模块激活：新模块接管流量时调用（热重载）
     * @details 在所有 server 的 dispatch 切换后调用。模块应在此方法中
     *          注册新的 servlet/handler。默认实现调用 onServerReady()。
     * @return bool
     */
    virtual bool onActivate();

    /**
     * @brief 模块停用：旧模块被替换时调用（热重载）
     * @details 新模块已接管，旧模块停止接收新请求。
     *          用于关闭 WebSocket 连接等长连接。默认实现返回 true。
     * @return bool
     */
    virtual bool onDeactivate();

    /**
     * @brief 连接时调用
     * @param stream 连接流
     * @return bool 
     */
    virtual bool onConnect(Stream::ptr stream);

    /**
     * @brief 断开连接时调用
     * @param stream 连接流
     * @return bool 
     */
    virtual bool onDisconnect(Stream::ptr stream);

    /**
     * @brief 服务器准备就绪时调用
     * @return bool 
     */
    virtual bool onServerReady();

    /**
     * @brief 服务器启动时调用
     * @return bool 
     */
    virtual bool onServerUp();

    /**
     * @brief 模块定时 tick 回调，由框架按 getTickIntervalMs() 的间隔周期性调用
     * @details 默认实现为空，子类按需覆写
     */
    virtual void onTick();

    /**
     * @brief 获取模块期望的 tick 间隔（毫秒）
     * @return 返回 0 表示不需要 tick，大于 0 表示每隔该毫秒数调用一次 onTick()
     */
    virtual uint64_t getTickIntervalMs();

    /**
     * @brief 获取模块状态字符串
     * @return std::string 
     */
    virtual std::string statusString();

    /**
     * @brief 获取模块信息
     */
    const std::string& getName() const { return m_name; }
    
    /**
     * @brief 获取模块版本
     */
    const std::string& getVersion() const { return m_version; }

    /**
     * @brief 获取模块文件名
     */
    const std::string& getFilename() const { return m_filename; }

    /**
     * @brief 获取模块ID
     */
    const std::string& getId() const { return m_id; }

    /**
     * @brief 获取模块类型
     */
    uint32_t getType() const { return m_type; }

    /**
     * @brief 设置文件名称
     * @param v 文件名称
     */
    void setFilename(const std::string& v) { m_filename = v; }

protected:
    /**
     * @brief 获取所有 HTTP 服务器
     * @param[out] servers 接收 HTTP 服务器指针的向量
     */
    void getAllHttpServer(std::vector<std::shared_ptr<http::HttpServer>>& servers);

    /**
     * @brief 获取所有 WebSocket 服务器
     * @param[out] servers 接收 WebSocket 服务器指针的向量
     */
    void getAllWSServer(std::vector<std::shared_ptr<http::WSServer>>& servers);

    /**
     * @brief 获取所有 RPC 服务器
     * @param[out] servers 接收 RPC 服务器指针的向量
     */
    void getAllRpcServer(std::vector<std::shared_ptr<rpc::RpcServer>>& servers);

    /**
     * @brief 获取所有 Game 服务器
     * @param[out] servers 接收 Game 服务器指针的向量
     */
    void getAllGameServer(std::vector<std::shared_ptr<game::GenericProtocolServer>>& servers);

private:
    /// 模块名称
    std::string m_name;
    /// 模块版本
    std::string m_version;
    /// 模块文件名
    std::string m_filename;
    /// 模块ID
    std::string m_id;
    /// 模块类型
    uint32_t m_type;
};

class RockModule : public Module {
public:
    typedef std::shared_ptr<RockModule> ptr;
    RockModule(const std::string& name, const std::string& version, const std::string& filename);

    virtual bool handleRockRequest(RockRequest::ptr request, RockResponse::ptr response, RockStream::ptr stream) = 0;
    virtual bool handleRockNotify(RockNotify::ptr notify, RockStream::ptr stream) = 0;

    virtual bool handleRequest(Message::ptr req, Message::ptr rsp, Stream::ptr stream);
    virtual bool handleNotify(Message::ptr notify, Stream::ptr stream);
};

class ModuleManager {
public:
    typedef std::shared_ptr<ModuleManager> ptr;

    /**
     * @brief 构造函数
     */
    ModuleManager();

    /**
     * @brief 添加模块
     * @param mod 模块指针
     */
    void add(Module::ptr mod);

    /**
     * @brief 删除模块
     * @param name 模块名称
     */
    void del(const std::string& name);

    /**
     * @brief 删除所有模块
     */
    void delAll();

    /**
     * @brief 退休（retire）一个模块：仅保留引用，不在此处 dlclose
     * @param m 旧模块指针
     * @details 热重载时旧模块可能仍被在途 fiber 的局部变量、
     *          回调或全局单例引用。此处将旧模块挂入退休列表持有其
     *          `Module::ptr`（进而持有 dlopen 句柄），避免在旧 .so
     *          仍被引用时执行 dlclose 导致访问已卸载代码/已析构静态对象。
     *          退休列表在进程退出或 delAll() 时统一释放。
     */
    void retire(Module::ptr m);

    /**
     * @brief 初始化模块
     */
    void init();

    /**
     * @brief 获取模块
     * @param name 模块名称
     * @return Module::ptr 模块的指针
     */
    Module::ptr get(const std::string& name);

    /**
     * @brief 连接时调用
     * @param stream 连接流
     */
    void onConnect(Stream::ptr stream);

    /**
     * @brief 断开连接时调用
     * @param stream 连接流
     */
    void onDisconnect(Stream::ptr stream);

    /**
     * @brief 列出所有模块
     * @param ms 模块指针的向量
     */
    void listAll(std::vector<Module::ptr>& ms);
    /**
     * @brief 重新加载模块（从共享库路径加载）。仅加载新模块并插入 m_modules，
     *        不销毁旧模块，不调用 onServerReady。
     * @param path 模块共享库路径
     * @param[out] old_out 接收被替换的旧模块（可为 nullptr 表示不需要）
     * @return 成功返回新模块指针，失败返回 nullptr
     * @details 旧模块由调用方负责排空连接后销毁：
     *          old->onDeactivate() → 等待 fiber 完成 → old.reset() → dlclose
     */
    Module::ptr reloadModule(const std::string& path, Module::ptr* old_out = nullptr);

    /**
     * @brief 列出指定类型的模块
     * @param type 模块类型
     * @param ms 模块指针的向量
     */
    void listByType(uint32_t type, std::vector<Module::ptr>& ms);

    /**
     * @brief 遍历模块
     * @param type 模块类型
     * @param cb 回调函数，参数为模块指针
     */
    void foreach(uint32_t type, std::function<void(Module::ptr)> cb);
private:
    /**
     * @brief 初始化单个模块
     * @param path 模块路径
     */
    void initModule(const std::string& path);
private:
    /// 读写锁
    std::shared_mutex m_mutex;
    /// 模块集合
    std::map<std::string, Module::ptr> m_modules;
    /// 模块类型到模块集合的映射
    std::unordered_map<uint32_t, std::unordered_map<std::string, Module::ptr> > m_type2Modules;
    /// 退休模块集合：热重载替换下来的旧模块，持有到进程退出再释放，避免 dlclose 时仍被引用
    std::vector<Module::ptr> m_retired;
};

typedef Singleton<ModuleManager> ModuleMgr;

/**
 * @brief 获取模块目录（module.path 配置值）
 * @return std::string
 */
std::string GetModulePath();

}
