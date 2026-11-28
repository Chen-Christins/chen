/**
 * @file ServerRegistry.hpp
 * @brief server 注册表：按配置创建/绑定 TcpServer，并提供统一的查找接口
 * @author Christins
 * @date 2026-02-06
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Tcp/TcpServer.hpp"

namespace chen {

/**
 * @brief 所有 TcpServer 的持有者
 * @details master 在 fork 前用它创建并绑定监听 socket，worker 继承后通过
 *          forEach/getAllServers 注入线程池并 start()。模块不应按配置里的
 *          type 字符串筛选 server，直接用 getAllServers() + dynamic_pointer_cast。
 */
class ServerRegistry {
public:
    /// 智能指针类型
    typedef std::shared_ptr<ServerRegistry> ptr;

    /**
     * @brief 注册框架内置的 server 类型（http/http2/ws/rpc/game/rock）
     * @details 内部用 std::once_flag，可重复调用
     */
    void registerBuiltInServers();

    /**
     * @brief 按 servers 配置创建并绑定所有 server
     * @details master 在 fork worker 之前调用：只 bind/listen，不注入线程池、
     *          不 start()，监听 fd 会随 fork 被 worker 继承
     * @param conf server 配置列表
     * @return bool 是否全部创建并绑定成功
     */
    bool createFromConf(const std::vector<TcpServerConf>& conf);

    /**
     * @brief 读取 `servers` 配置并创建/绑定所有 server
     * @details `servers` 这个 ConfigVar 必须在本编译单元里声明，才能保证
     *          `Config::LoadFromConfDir()` 加载配置时它已注册（否则 YAML 里的
     *          `servers:` 会被直接丢弃，之后 Lookup 只能拿到空默认值）
     * @return bool 是否全部创建并绑定成功
     */
    bool createFromConfigured();

    /**
     * @brief 按配置中的 type 取 server 列表
     * @param type 配置里的 server type，如 "http"
     * @param svrs 拿到的 server
     * @return bool 是否存在该 type
     */
    bool getServer(const std::string& type, std::vector<TcpServer::ptr>& svrs);

    /**
     * @brief 获取全部 server（不区分类型）
     * @param svrs 拿到的 server
     */
    void getAllServers(std::vector<TcpServer::ptr>& svrs);

    /**
     * @brief 遍历全部 server
     * @param fn 回调，参数为 type 和 server
     */
    void forEach(const std::function<void(const std::string&, const TcpServer::ptr&)>& fn) const;

    /**
     * @brief 配置中声明的 server 类型数量
     */
    size_t typeCount() const { return servers_.size(); }

private:
    /// type -> server 列表
    std::map<std::string, std::vector<TcpServer::ptr>> servers_;
};

} // namespace chen
