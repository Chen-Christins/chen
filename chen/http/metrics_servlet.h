/**
 * @file metrics_servlet.h
 * @brief 指标采集路由注册辅助
 * @author Christins
 * @date 2026-10-18
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>

#include "../metrics/metrics.h"

namespace chen::http {

class HttpServer;

/**
 * @brief 将指标采集路由注册到指定 HttpServer
 * @param server 目标 HTTP Server（建议使用 admin/内网 server）
 * @param path 路由路径，默认 /metrics
 * @param format 渲染格式，默认 Prometheus
 * @return bool 是否注册成功
 */
bool RegisterMetricsServlet(
    const std::shared_ptr<HttpServer>& server,
    const std::string& path = "/metrics",
    metrics::RenderFormat format = metrics::RenderFormat::PROMETHEUS);

} // namespace chen::http
