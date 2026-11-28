#include "RockServer.hpp"

#include <memory>

#include "../Log/Log.hpp"
#include "../Module/Module.hpp"
#include "../Socket/SocketStream.hpp"
#include "RockStream.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

RockServer::RockServer(const std::string& type, IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
{
    type_ = type;
}

void RockServer::handleClient(Socket::ptr client)
{
    DEBUG(logger) << "handleClient " << *client;
    RockSession::ptr session(new RockSession(client));

    session->setWorker(worker_);

    // 请求作用域计入 TcpServer 的在途计数：draining 时等回包之后再关闭连接
    session->setRequestScopeFactory([this, client]() -> std::shared_ptr<void> {
        return std::make_shared<RequestGuard>(this, client);
    });
    ModuleMgr::GetInstance()->foreach(Module::ROCK, [session](Module::ptr m) {
        m->onConnect(session);
    });

    session->setDisConnectCb([](AsyncSocketStream::ptr stream) {
        ModuleMgr::GetInstance()->foreach(Module::ROCK, [stream](Module::ptr m) {
            m->onDisconnect(stream);
        });
    });

    session->setRequestHandler([](RockRequest::ptr req, RockResponse::ptr rsp, RockStream::ptr conn) -> bool {
        bool rt = false;
        ModuleMgr::GetInstance()->foreach(Module::ROCK, [&rt, req, rsp, conn](Module::ptr m) {
            if (rt) {
                return;
            }
            rt = m->handleRequest(req, rsp, conn);
        });
        return rt;
    });

    session->setNotifyHandler([](RockNotify::ptr nty, RockStream::ptr conn) -> bool {
        INFO(logger) << "handleNty " << nty->toString() << " body=" << nty->getBody();
        bool rt = false;
        ModuleMgr::GetInstance()->foreach(Module::ROCK, [&rt, nty, conn](Module::ptr m) {
            if (rt) {
                return;
            }
            rt = m->handleNotify(nty, conn);
        });
        return rt;
    });

    session->start();
    // 连接存活期间保持 handleClient 不返回（fiber 上等待，不占线程），
    // 这样 active_clients_ 里的记录与连接同生命周期
    session->waitClose();
}

} // namespace chen
