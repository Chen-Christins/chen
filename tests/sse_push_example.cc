/**
 * @file sse_push_example.cc
 * @brief SSE 典型场景示例：服务端实时广播推送
 * @details
 *   场景：一个简易的实时消息推送服务
 *   - 客户端连接后持续接收服务端广播的消息
 *   - 支持退订/重连（Last-Event-ID）
 *   - 心跳保活
 *   - 服务端可随时向所有在线客户端推送事件
 *
 *   使用：
 *   1. module 初始化时启动广播线程，往消息队列投递数据
 *   2. SSE servlet 在 onConnect 里从队列拉取消息并推送
 *   3. 客户端断开时自动清理
 *
 *   测试：
 *   curl -N http://127.0.0.1:8089/sse/push
 *
 * @author Christins
 * @date 2026-05-14
 */
#include "chen/http/http_server.h"
#include "chen/http/sse_servlet.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include <mutex>
#include <queue>

static chen::Logger::ptr logger = LOG_ROOT();

// ============================================================
// 消息总线：线程安全的生产者-消费者队列
// ============================================================
class MessageBus {
public:
    void publish(const std::string& id, const std::string& event, const std::string& data) {
        std::lock_guard lock(m_mutex);
        m_queue.push({id, event, data});
        if (m_queue.size() > 1024) {
            m_queue.pop();
        }
    }

    bool consume(std::string& id, std::string& event, std::string& data) {
        std::lock_guard lock(m_mutex);
        if (!m_queue.empty()) {
            auto& msg = m_queue.front();
            id = msg.id;
            event = msg.event;
            data = msg.data;
            m_queue.pop();
            return true;
        }
        return false;
    }

private:
    struct Message {
        std::string id;
        std::string event;
        std::string data;
    };
    std::queue<Message> m_queue;
    std::mutex m_mutex;
};

static MessageBus s_bus;

// ============================================================
// SSE 事件推送回调
// ============================================================
static int32_t on_sse_connect(chen::http::HttpRequest::ptr req, chen::http::SSESession::ptr session) {
    INFO(logger) << "[SSE] 客户端连接: " << session->getRemoteAddressString() << " 路径=" << req->getPath();

    std::string lastId = req->getHeader("Last-Event-Id");
    if (!lastId.empty()) {
        INFO(logger) << "[SSE] 客户端断线重连，Last-Event-ID=" << lastId;
        session->sendComment("reconnect acknowledged, last-id=" + lastId);
    }

    session->sendEvent(R"({"status":"connected"})", "", "ready");
    INFO(logger) << "[SSE] 握手完成，开始推送";

    uint32_t heartbeat_counter = 0;

    while (true) {
        std::string id, event, data;
        if (s_bus.consume(id, event, data)) {
            int32_t ret = session->sendEvent(data, id, event);
            if (ret <= 0) {
                INFO(logger) << "[SSE] 发送失败，客户端已断连";
                break;
            }
            heartbeat_counter = 0;
        } else {
            usleep(200000);
        }

        ++heartbeat_counter;
        if (heartbeat_counter >= 75) {
            int32_t ret = session->sendComment("heartbeat");
            if (ret <= 0) {
                INFO(logger) << "[SSE] 心跳发送失败，客户端已断连";
                break;
            }
            INFO(logger) << "[SSE] heartbeat sent";
            heartbeat_counter = 0;
        }
    }

    return 0;
}

static int32_t on_sse_close(chen::http::HttpRequest::ptr req, chen::http::SSESession::ptr session) {
    INFO(logger) << "[SSE] 客户端断开: " << session->getRemoteAddressString();
    return 0;
}

// ============================================================
// 模拟数据源：定时向消息总线投递数据
// ============================================================
static void run_broadcast_demo() {
    uint64_t seq = 1;

    auto price_update = [&seq]() {
        double price = 100.0 + (rand() % 1000) / 100.0;
        std::string data = R"({"symbol":"APPL","price":)" + std::to_string(price) + "}";
        s_bus.publish(std::to_string(seq++), "price", data);
    };

    auto system_notify = [&seq]() {
        std::string data = R"({"level":"info","msg":"系统运行正常"})";
        s_bus.publish(std::to_string(seq++), "notification", data);
    };

    uint32_t tick = 0;
    while (true) {
        sleep(1);
        ++tick;
        if (tick % 3 == 0) {
            price_update();
        }
        if (tick % 8 == 0) {
            system_notify();
        }
    }
}

// ============================================================
// 入口
// ============================================================
void run() {
    chen::http::HttpServer::ptr server(new chen::http::HttpServer);
    chen::Address::ptr addr = chen::Address::LookupAnyIPAddress("0.0.0.0:8089");
    if (!addr) {
        ERROR(logger) << "get address error";
        return;
    }

    auto dispatch = server->getServletDispatch();

    dispatch->addServlet("/sse/push",
        std::make_shared<chen::http::FunctionSSEServlet>(on_sse_connect, on_sse_close));

    dispatch->addGlobServlet("/sse/*",
        std::make_shared<chen::http::FunctionSSEServlet>(on_sse_connect, on_sse_close));

    while (!server->bind(addr)) {
        ERROR(logger) << "bind " << *addr << " fail";
        sleep(1);
    }
    INFO(logger) << "SSE 广播服务已启动: " << *addr;
    INFO(logger) << "测试命令: curl -N http://127.0.0.1:8089/sse/push";
    server->start();
}

int main(int argc, char** argv) {
    chen::IOManager::ptr iom(new chen::IOManager(2, true, "sse_demo"));

    iom->schedule(run_broadcast_demo);

    iom->schedule(run);

    iom->addTimer(3000, []() {}, true);
    iom->stop();
    return 0;
}
