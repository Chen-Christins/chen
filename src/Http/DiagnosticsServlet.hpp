/**
 * @file DiagnosticsServlet.hpp
 * @brief 运行时诊断路由注册辅助
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>

namespace chen::http {

class HttpServer;

/**
 * @brief 把诊断路由注册到指定 HttpServer
 * @param server 目标 HTTP Server（建议只挂内网/admin 端口）
 * @param prefix 路由前缀，默认 /debug
 * @return bool 是否注册成功
 * @details 当前提供：
 *          - GET {prefix}/fibers：所有调度线程的快照（JSON），字段见
 *            `StallDetector::dumpJson()`（tid / name / idle / last_ping_ms / fiber / op / fd）
 *
 * @note 这些端点会暴露内部线程与协程状态，不要挂到公网入口。
 */
bool RegisterDiagnosticsServlets(const std::shared_ptr<HttpServer>& server, const std::string& prefix = "/debug");

} // namespace chen::http
