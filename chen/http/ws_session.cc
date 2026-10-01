#include "ws_session.h"

#include <string.h>

#include "../log/log.h"
#include "../util/endian.h"
#include "../util/random_util.h"
#include "../util/string_util.h"
#include "../util/util.h" // IWYU pragma: keep

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

ConfigVar<uint32_t>::ptr websocket_message_max_size =
    Config::Lookup("websocket.message.max_size", (uint32_t)1024 * 1024 * 32, "websocket message max size");

std::string WSFrameHead::toString() const {
    std::stringstream ss;
    ss << "[WSFrameHead fin=" << fin
       << " rsv1=" << rsv1
       << " rsv2=" << rsv2
       << " rsv3=" << rsv3
       << " opcode=" << opcode
       << " payload=" << payload
       << " mask=" << mask
       << "]";
    return ss.str();
}

WSFrameMessage::WSFrameMessage(int opcode, const std::string& data)
    : m_opcode(opcode)
    , m_data(data) {
}

WSSession::WSSession(Socket::ptr sock, bool owner)
    :HttpSession(sock, owner) {
}

HttpRequest::ptr WSSession::handleShake() {
    HttpRequest::ptr req;
    do {
        req = recvRequest();
        if (!req) {
            WARN(logger) << "invalid http request";
            break;
        }
        if (strcasecmp(req->getHeader("Upgrade").c_str(), "websocket")) {
            WARN(logger) << "http header Upgrade != websocket";
            break;
        }
        if (strcasecmp(req->getHeader("Connection").c_str(), "Upgrade")) {
            WARN(logger) << "http header Connection != Upgrade";
            break;
        }
        if (req->getHeaderAs<int>("Sec-WebSocket-Version") != 13) {
            WARN(logger) << "http header Sec-WebSocket-Version != 13";
            break;
        }
        std::string key = req->getHeader("Sec-WebSocket-Key");
        if (key.empty()) {
            WARN(logger) << "http header Sec-WebSocket-Key = null";
            break;
        }

        // SHA1 返回 hex 字符串（40 字符），需 HexDecode 为原始 20 字节再 Base64，
        // 否则 Sec-WebSocket-Accept 算错，浏览器会立刻断开
        std::string v = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        v = StringUtil::Base64Encode(StringUtil::HexDecode(EncryptorUtil::SHA1(v)));
        req->setWebsocket(true);

        auto rsp = req->createResponse();
        rsp->setStatus(HttpStatus::SWITCHING_PROTOCOLS);
        rsp->setWebsocket(true);
        rsp->setReason("Web Socket Protocol Handshake");
        rsp->setHeader("Upgrade", "websocket");
        rsp->setHeader("Connection", "Upgrade");
        rsp->setHeader("Sec-WebSocket-Accept", v);

        sendResponse(rsp);
        DEBUG(logger) << *req;
        DEBUG(logger) << *rsp;

        return req;
    } while (false);
    if (req) {
        WARN(logger) << *req;
    }
    return nullptr;
}

WSFrameMessage::ptr WSSession::recvMessage() {
    return WSRecvMessage(this, false);
}

int32_t WSSession::sendMessage(WSFrameMessage::ptr msg, bool fin) {
    return WSSendMessage(this, msg, false, fin);
}

int32_t WSSession::sendMessage(const std::string& msg, int32_t opcode, bool fin) {
    return WSSendMessage(this, std::make_shared<WSFrameMessage>(opcode, msg), false, fin);
}

int32_t WSSession::ping() {
    return WSPing(this);
}

int32_t WSSession::pong() {
    return WSPong(this);
}

WSFrameMessage::ptr WSRecvMessage(Stream* stream, bool client) {
    int opcode = 0;
    std::string data;
    int cur_len = 0;
    do {
        WSFrameHead ws_head;
        // 如果读取失败
        if (stream->readFixSize(&ws_head, sizeof(ws_head)) <= 0) {
            break;
        }
        DEBUG(logger) << "WSFrameHead=" << ws_head.toString();

        if (ws_head.opcode == WSFrameHead::PING) {
            TRACE(logger) << "PING";
            if (WSPong(stream) <= 0) {
                break;
            }
        } else if (ws_head.opcode == WSFrameHead::PONG) {
        } else if (ws_head.opcode == WSFrameHead::CLOSE) {
            TRACE(logger) << "CLOSE frame received";
            // 读取 CLOSE 帧的 body（可能包含状态码 + 原因，<= 125 字节）
            uint64_t body_len = ws_head.payload;
            std::string close_body;
            if (body_len > 0) {
                char cmask[4] = {0};
                if (ws_head.mask) {
                    if (stream->readFixSize(cmask, sizeof(cmask)) <= 0) break;
                }
                close_body.resize(body_len);
                if (stream->readFixSize(&close_body[0], body_len) <= 0) break;
                if (ws_head.mask) {
                    for (uint64_t i = 0; i < body_len; ++i) close_body[i] ^= cmask[i % 4];
                }
            }
            // 回 CLOSE 帧作为确认，然后关闭连接
            WSFrameHead close_head;
            memset(&close_head, 0, sizeof(close_head));
            close_head.fin = 1;
            close_head.opcode = WSFrameHead::CLOSE;
            stream->writeFixSize(&close_head, sizeof(close_head));
            stream->close();
            return nullptr;
        } else if (ws_head.opcode == WSFrameHead::CONTINUE || ws_head.opcode == WSFrameHead::TEXT_FRAME ||
                   ws_head.opcode == WSFrameHead::BIN_FRAME) {
            if (!client && !ws_head.mask) {
                INFO(logger) << "WSFrameHead Mask != 1";
                break;
            }
            uint64_t length = 0;
            if (ws_head.payload == 126) {
                uint16_t len = 0;
                if (stream->readFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
                length = byteswapOnLittleEndian(len);
            } else if (ws_head.payload == 127) {
                uint64_t len = 0;
                if (stream->readFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
                length = byteswapOnLittleEndian(len);
            } else {
                length = ws_head.payload;
            }

            if ((cur_len + length) >= websocket_message_max_size->getValue()) {
                WARN(logger) << "WSFrameMessage length > " << websocket_message_max_size->getValue() << " ("
                             << (cur_len + length) << ")";
                break;
            }

            char mask[4] = {0};
            if (ws_head.mask) {
                if (stream->readFixSize(mask, sizeof(mask)) <= 0) {
                    break;
                }
            }
            data.resize(cur_len + length);
            if (stream->readFixSize(&data[cur_len], length) <= 0) {
                break;
            }
            if (ws_head.mask) {
                for (int i = 0; i < (int)length; ++i) {
                    data[cur_len + i] ^= mask[i % 4];
                }
            }
            cur_len += length;

            if (!opcode && ws_head.opcode != WSFrameHead::CONTINUE) {
                opcode = ws_head.opcode;
            }

            if (ws_head.fin) {
                DEBUG(logger) << data;
                return WSFrameMessage::ptr(std::make_shared<WSFrameMessage>(opcode, std::move(data)));
            }
        } else {
            DEBUG(logger) << "invalid opcode=" << ws_head.opcode;
        }

    } while (true);
    stream->close();
    return nullptr;
}

int32_t WSSendMessage(Stream* stream, WSFrameMessage::ptr msg, bool client, bool fin) {
    do {
        WSFrameHead ws_head;
        memset(&ws_head, 0, sizeof(ws_head));
        ws_head.fin = fin;
        ws_head.opcode = msg->getOpcode();
        ws_head.mask = client;
        uint64_t size = msg->getData().size();

        if (size < 126) {
            ws_head.payload = size;
        } else if (size < 65536) {
            ws_head.payload = 126;
        } else {
            ws_head.payload = 127;
        }

        if (stream->writeFixSize(&ws_head, sizeof(ws_head)) <= 0) {
            break;
        }
        if (ws_head.payload == 126) {
            /**
             * 出现问题版本
             * uint16_t len = byteswap(size); 这么写会出现一个问题
             * 实际发生：
             * uint32_t temp = byteswap(0x12345678); // temp = 0x78563412
             * len = (uint16_t)temp; // len = 0x3412 (截断高位后)
             *
             * 当前版本
             * uint16_t len = size; // len = 0x5678 (截断后)
             * len = byteswap(len); // 交换后 len = 0x7856
             */
            uint16_t len = byteswapOnLittleEndian(static_cast<uint16_t>(size));
            if (stream->writeFixSize(&len, sizeof(len)) <= 0) {
                break;
            }
        } else if (ws_head.payload == 127) {
            uint64_t len = byteswapOnLittleEndian(size);
            if (stream->writeFixSize(&len, sizeof(len)) <= 0) {
                break;
            }
        }
        if (client) {
            char mask[4];
            uint32_t rand_value = RandomUtil::RandUint(0, UINT32_MAX);
            memcpy(mask, &rand_value, sizeof(mask));

            // 拷贝一份再 mask — 不污染调用者的原始数据（避免重发 / 广播出错）
            std::string masked_data = msg->getData();
            for (size_t i = 0; i < masked_data.size(); ++i) {
                masked_data[i] ^= mask[i % 4];
            }

            if (stream->writeFixSize(mask, sizeof(mask)) <= 0) {
                break;
            }
            if (stream->writeFixSize(masked_data.c_str(), masked_data.size()) <= 0) {
                break;
            }
        } else {
            if (stream->writeFixSize(msg->getData().c_str(), msg->getData().size()) <= 0) {
                break;
            }
        }
        return size + sizeof(ws_head);
    } while (0);
    stream->close();
    return -1;
}

int32_t WSPing(Stream* stream) {
    WSFrameHead ws_head;
    memset(&ws_head, 0, sizeof(ws_head));
    ws_head.fin = 1;
    ws_head.opcode = WSFrameHead::PING;
    int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
    if (v <= 0) {
        stream->close();
    }
    return v;
}

int32_t WSPong(Stream* stream) {
    WSFrameHead ws_head;
    memset(&ws_head, 0, sizeof(ws_head));
    ws_head.fin = 1;
    ws_head.opcode = WSFrameHead::PONG;
    int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
    if (v <= 0) {
        stream->close();
    }
    return v;
}

} // namespace chen::http
