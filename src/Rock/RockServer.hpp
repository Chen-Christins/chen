/**
 * @file RockServer.hpp
 * @brief RockServer
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-26
 * @copyright GPL-3.0
 */
#pragma once

#include "../Tcp/TcpServer.hpp"

namespace chen {

class RockServer : public TcpServer {
public:
    typedef std::shared_ptr<RockServer> ptr;
    RockServer(
        const std::string& type = "rock",
        IOManager* worker = IOManager::GetThis(),
        IOManager* io_worker = IOManager::GetThis(),
        IOManager* accept_worker = IOManager::GetThis());

    /**
     * @brief Rock 以「一个请求/通知的处理 + 回包」作为请求边界，支持协作式 drain
     */
    virtual bool supportsGracefulDrain() const override { return true; }

protected:
    virtual void handleClient(Socket::ptr client) override;
};

} // namespace chen
