/**
 * @file http_client.h
 * @brief HTTP 异步客户端工具
 * @author chen
 * @date 2026-10-25
 * @copyright GPL-3.0
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "../schedule/schedule.h"
#include "../util/future.h"
#include "http.h"
#include "http_connection.h"

namespace chen::http {

/**
 * @class HttpClient
 * @brief HTTP 异步请求门面
 * @details 把同步的 HttpConnection::DoRequest 提交到调度器执行：
 *          - 在 fiber 里调用时，HTTP 的 connect/read/write 由 hook 转成事件等待，
 *            提交后立即返回，不阻塞当前线程
 *          - 需要结果时用 Future 形式，get() 只挂起当前协程
 * @note DNS 解析（getaddrinfo）本身不在 hook 范围内，域名请求会在
 *       调度线程上短暂阻塞；高频场景建议使用 IP 或结合连接池。
 */
class HttpClient {
public:
    /**
     * @brief 完成回调
     * @param result 请求结果；异常场景下为空指针
     */
    typedef std::function<void(HttpResult::ptr)> Callback;

    /**
     * @brief 提交异步请求（回调式）
     * @param method 请求方法
     * @param url 完整 URL
     * @param timeout_ms 接收超时毫秒数
     * @param cb 完成回调，为空时只打日志（失败打 WARN）
     * @param headers 请求头
     * @param body 请求体
     * @param scheduler 指定调度器，为空时自动解析（IOManager → Scheduler）
     * @return bool false 表示没有可用调度器，任务未提交
     */
    static bool submit(
        HttpMethod method,
        const std::string& url,
        uint64_t timeout_ms,
        Callback cb = nullptr,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief 提交异步请求（Future 式）
     * @return Future<HttpResult::ptr> 没有可用调度器时返回已完成且值为 nullptr 的 Future
     */
    static Future<HttpResult::ptr> request(
        HttpMethod method,
        const std::string& url,
        uint64_t timeout_ms,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief GET 请求
     */
    static Future<HttpResult::ptr> get(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief POST 请求
     */
    static Future<HttpResult::ptr> post(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief PUT 请求
     */
    static Future<HttpResult::ptr> put(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief PATCH 请求
     */
    static Future<HttpResult::ptr> patch(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief DELETE 请求
     */
    static Future<HttpResult::ptr> del(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief HEAD 请求
     */
    static Future<HttpResult::ptr> head(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);

    /**
     * @brief OPTIONS 请求
     */
    static Future<HttpResult::ptr> options(
        const std::string& url,
        uint64_t timeout_ms = 3000,
        const std::map<std::string, std::string>& headers = {},
        const std::string& body = "",
        Scheduler* scheduler = nullptr);
};

} // namespace chen::http
