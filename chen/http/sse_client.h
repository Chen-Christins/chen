/**
 * @file sse_client.h
 * @brief SSE (Server-Sent Events) 客户端解析器
 * @author Christins
 * @date 2026-06-10
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace chen::http {

/**
 * @brief SSE 客户端事件解析器
 * @details 用于解析服务端发送的 text/event-stream 格式数据，
 *          将原始字节流解析为结构化的 SSE 事件。
 *
 *          使用方式：
 *          1. 创建 SSEClient 实例
 *          2. 设置回调 setCallback()
 *          3. 将收到的原始数据喂入 feed()
 *          4. 每解析出一个完整事件，回调被触发
 */
class SSEClient {
public:
    typedef std::shared_ptr<SSEClient> ptr;

    /**
     * @brief SSE 事件结构
     */
    struct SSEvent {
        /// 事件数据（多行 data 字段以 \n 连接）
        std::string data;
        /// 事件类型（可选）
        std::string event;
        /// 事件 ID（可选）
        std::string id;

        /**
         * @brief 判断事件是否为空
         */
        bool empty() const { return data.empty() && event.empty() && id.empty(); }
    };

    /**
     * @brief SSE 事件回调类型
     * @param event 解析出的 SSE 事件
     * @return true 继续解析 | false 停止解析
     */
    using callback_type = std::function<bool(const SSEvent& event)>;

    SSEClient();

    /**
     * @brief 设置事件回调
     * @param cb 回调函数
     */
    void setCallback(callback_type cb) { m_callback = std::move(cb); }

    /**
     * @brief 获取当前回调
     */
    callback_type getCallback() const { return m_callback; }

    /**
     * @brief 喂入原始数据
     * @param data 数据指针
     * @param len 数据长度
     * @return size_t 已消费的字节数
     * @details 内部缓冲不完整的事件，解析出完整事件后触发回调。
     *          如果回调返回 false，停止解析并返回已消费字节数。
     */
    size_t feed(const char* data, size_t len);

    /**
     * @brief 重置内部状态
     */
    void reset();

private:
    /// 数据缓冲区（存放未完成的事件片段）
    std::string m_buffer;
    /// 事件回调
    callback_type m_callback;
};

} // namespace chen::http
