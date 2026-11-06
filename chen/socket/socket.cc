#include "socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>

#include <openssl/x509v3.h>

#include <atomic>

#include "../hook/fd_manager.h"
#include "../hook/hook.h"
#include "../iomanager/iomanager.h"
#include "../log/log.h"
#include "../util/macro.h"
#include "../util/time_util.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

Socket::ptr Socket::CreateTCP(Address::ptr address)
{
    Socket::ptr sock(new Socket(address->getFamily(), TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDP(Address::ptr address)
{
    Socket::ptr sock(new Socket(address->getFamily(), UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket()
{
    Socket::ptr sock(new Socket(IPv4, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDPSocket()
{
    Socket::ptr sock(new Socket(IPv4, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket6()
{
    Socket::ptr sock(new Socket(IPv6, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDPSocket6()
{
    Socket::ptr sock(new Socket(IPv6, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateUnixTCPSocket()
{
    Socket::ptr sock(new Socket(UNIX, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUnixUDPSocket()
{
    Socket::ptr sock(new Socket(UNIX, UDP, 0));
    return sock;
}

Socket::Socket(int family, int type, int protocol)
    : m_sock(-1)
    , m_family(family)
    , m_type(type)
    , m_protocol(protocol)
    , m_isConnected(false)
{
}

Socket::~Socket()
{
    close();
}

uint64_t Socket::getSendTimeout()
{
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if (ctx) {
        return ctx->getTimeout(SO_SNDTIMEO);
    }
    return -1;
}

void Socket::setSendTimeout(uint64_t v)
{
    /* 秒和微秒 */
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

uint64_t Socket::getRecvTimeout()
{
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if (ctx) {
        return ctx->getTimeout(SO_RCVTIMEO);
    }
    return -1;
}

void Socket::setRecvTimeout(int64_t v)
{
    DEBUG(logger) << "setRecvTimeout v=" << v;
    /* 秒和微秒 */
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Socket::getOption(int level, int option, void* result, socklen_t* len)
{
    int rt = getsockopt(m_sock, level, option, result, (socklen_t*)len);
    if (rt) {
        DEBUG(logger) << "getOption sock=" << m_sock << " level=" << level << " option=" << option << " errno=" << errno
                      << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::setOption(int level, int option, const void* result, socklen_t len)
{
    if (setsockopt(m_sock, level, option, result, (socklen_t)len)) {
        DEBUG(logger) << "setOption sock=" << m_sock << " level=" << level << " option=" << option << " errno=" << errno
                      << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

Socket::ptr Socket::accept()
{
    Socket::ptr sock(new Socket(m_family, m_type, m_protocol));
    /* 他这里把连接远端地址的操作给到了init函数，在init里面会获取本地地址和远端地址，同时设置端口复用
       accept 里面已经设置了将其插入到fdctx里，而且accept创建的问价描述符属于socket系列的，所以
       这里就间接创建了一个socket句柄
     */
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if (newsock == -1) {
        // EBADF 是 stop() 关闭监听 socket 时的预期行为，用 DEBUG 级别避免停服时噪声
        if (errno == EBADF) {
            DEBUG(logger) << "accept(" << m_sock << ") errno=" << errno << " errstr=" << strerror(errno) << " (socket closed)";
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 非阻塞 socket 上没有待处理的连接，正常行为
        } else {
            ERROR(logger) << "accept(" << m_sock << ") errno=" << errno << " errstr=" << strerror(errno);
        }
        return nullptr;
    }
    if (sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

bool Socket::init(int sock)
{
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(sock);
    if (ctx && ctx->isSocket() && !ctx->isClose()) {
        m_sock = sock;
        m_isConnected = true;
        initSock();
        getLocalAddress();
        getRemoteAddress();
        return true;
    }
    return false;
}

bool Socket::bind(const Address::ptr addr)
{
    if (!isValid()) {
        newSock();
        if (UNLIKELY(!isValid())) {
            return false;
        }
    }

    if (UNLIKELY(addr->getFamily() != m_family)) {
        ERROR(logger) << "bind sock.family(" << m_family << ") addr.family(" << addr->getFamily() << ") not equal, addr=" << addr->toString();
        return false;
    }

    UnixAddress::ptr uaddr = std::dynamic_pointer_cast<UnixAddress>(addr);
    if (uaddr) {
        Socket::ptr sock = Socket::CreateUnixTCPSocket();
        if (sock->connect(uaddr)) {
            return false;
        } else {
            FSUtil::Unlink(uaddr->getPath(), true);
        }
    }

    if (::bind(m_sock, addr->getAddr(), addr->getAddrLen())) {
        ERROR(logger) << "bind error errno=" << errno << " strerr=" << strerror(errno);
        return false;
    }
    getLocalAddress();
    return true;
}

bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms)
{
    if (!isValid()) {
        newSock();
        if (UNLIKELY(!isValid())) {
            return false;
        }
    }

    if (UNLIKELY(addr->getFamily() != m_family)) {
        ERROR(logger) << "connect sock.family(" << m_family << ") addr.family(" << addr->getFamily() << ") not equal, addr=" << addr->toString();
        return false;
    }

    if (timeout_ms == (uint64_t)-1) {
        /* -1失败 0成功 */
        if (::connect(m_sock, addr->getAddr(), addr->getAddrLen())) {
            ERROR(logger) << "sock=" << m_sock << " connect(" << addr->toString() << ") error errno=" << errno << ", strerr=" << strerror(errno);
            close();
            return false;
        }
    } else {
        if (::connect_with_timeout(m_sock, addr->getAddr(), addr->getAddrLen(), timeout_ms)) {
            ERROR(logger) << "sock=" << m_sock << "connect(" << addr->toString() << ") error errno=" << errno << ", strerr=" << strerror(errno);
            close();
            return false;
        }
    }
    m_isConnected = true;
    getRemoteAddress();
    getLocalAddress();

    return true;
}

bool Socket::reconnect(uint64_t timeout_ms)
{
    if (!m_remoteAddress) {
        ERROR(logger) << "reconnect m_remoteAddress is null";
        return false;
    }
    m_localAddress.reset();
    return connect(m_remoteAddress, timeout_ms);
}

bool Socket::listen(int backlog)
{
    if (!isValid()) {
        ERROR(logger) << "listen error sock=-1";
        return false;
    }
    if (::listen(m_sock, backlog)) {
        ERROR(logger) << "listen error sock=" << errno << " strerr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::close()
{
    if (!m_isConnected && m_sock == -1) {
        return true;
    }
    m_isConnected = false;
    if (m_sock != -1) {
        // hook::close 内部会调用 cancelAll 唤醒等待的 fiber，
        // 然后 FdMgr::del 清理 FdCtx，最后 ::close(fd)
        ::close(m_sock);
        m_sock = -1;
    }
    return false;
}

// write
int Socket::send(const void* buffer, size_t length, int flags)
{
    if (isConnected()) {
        return ::send(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::send(const iovec* buffers, size_t length, int flags)
{
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

// udp
int Socket::sendTo(const void* buffer, size_t length, const Address::ptr to, int flags)
{
    if (isConnected()) {
        return ::sendto(m_sock, buffer, length, flags, to->getAddr(), to->getAddrLen());
    }
    return -1;
}

int Socket::sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags)
{
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_name = to->getAddr();
        msg.msg_namelen = to->getAddrLen();
        msg.msg_iovlen = length;
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

// read
int Socket::recv(void* buffer, size_t length, int flags)
{
    if (isConnected()) {
        return ::recv(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::recv(iovec* buffers, size_t length, int flags)
{
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::recvFrom(void* buffer, size_t length, Address::ptr from, int flags)
{
    if (isConnected()) {
        socklen_t len = from->getAddrLen();
        return ::recvfrom(m_sock, buffer, length, flags, from->getAddr(), &len);
    }
    return -1;
}

int Socket::recvFrom(iovec* buffers, size_t length, Address::ptr from, int flags)
{
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = from->getAddr();
        msg.msg_namelen = from->getAddrLen();
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

Address::ptr Socket::getRemoteAddress()
{
    if (m_remoteAddress) {
        return m_remoteAddress;
    }

    Address::ptr result;
    switch (m_family) {
    case AF_INET:
        result.reset(new IPv4Address());
        break;
    case AF_INET6:
        result.reset(new IPv6Address());
        break;
    case AF_UNIX:
        result.reset(new UnixAddress());
        break;
    default:
        result.reset(new UnknownAddress(m_family));
        break;
    }
    socklen_t addrlen = result->getAddrLen();
    // 成功0，失败-1，结果的内存地址存放在 result->addr里，获取套接字的远程地址信息
    // 当connect连接上后，可以调用getsockname来获取远程的地址信息
    if (getpeername(m_sock, result->getAddr(), &addrlen)) {
        ERROR(logger) << "getpeername error sock=" << m_sock << " errno=" << errno << " errstr=" << strerror(errno);
        return Address::ptr(new UnknownAddress(m_family));
    }
    /* 如果是Unix的，这里需要改一下长度 */
    if (m_family == AF_UNIX) {
        UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->setAddrLen(addrlen);
    }
    m_remoteAddress = result;
    return m_remoteAddress;
}

Address::ptr Socket::getLocalAddress()
{
    if (m_localAddress) {
        return m_localAddress;
    }

    Address::ptr result;
    switch (m_family) {
    case AF_INET:
        result.reset(new IPv4Address());
        break;
    case AF_INET6:
        result.reset(new IPv6Address());
        break;
    case AF_UNIX:
        result.reset(new UnixAddress());
        break;
    default:
        result.reset(new UnknownAddress(m_family));
        break;
    }
    socklen_t addrlen = result->getAddrLen();
    // 成功0，失败-1，结果的内存地址存放在 result->addr里，获取套接字的本地地址信息
    // 当connect/bind连接上后，可以调用getsockname来获取本地的地址信息
    if (getsockname(m_sock, result->getAddr(), &addrlen)) {
        ERROR(logger) << "getsockname error sock=" << m_sock << " errno=" << errno << " errstr=" << strerror(errno);
        return Address::ptr(new UnknownAddress(m_family));
    }
    /* 如果是Unix的，这里需要改一下长度 */
    if (m_family == AF_UNIX) {
        UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->setAddrLen(addrlen);
    }
    m_localAddress = result;
    return m_localAddress;
}

bool Socket::isValid() const
{
    return m_sock != -1;
}

int Socket::getError()
{
    int error = 0;
    socklen_t len = sizeof(error);
    if (!getOption(SOL_SOCKET, SO_ERROR, &error, &len)) {
        error = errno;
    }
    return error;
}

std::ostream& Socket::dump(std::ostream& os) const
{
    os << "[Socket sock=" << m_sock << " is_connected=" << m_isConnected << " family=" << m_family << " type=" << m_type
       << " protocol=" << m_protocol;

    if (m_localAddress) {
        os << " local_address=" << m_localAddress->toString();
    }

    if (m_remoteAddress) {
        os << " remote_address=" << m_remoteAddress->toString();
    }
    os << "]";
    return os;
}

bool Socket::cancelRead()
{
    IOManager* iom = IOManager::GetThis();
    if (!iom || !isValid()) {
        return false;
    }
    return iom->cancelEvent(m_sock, IOManager::READ);
}

bool Socket::cancelWrite()
{
    IOManager* iom = IOManager::GetThis();
    if (!iom || !isValid()) {
        return false;
    }
    return iom->cancelEvent(m_sock, IOManager::WRITE);
}

bool Socket::cancelAccept()
{
    IOManager* iom = IOManager::GetThis();
    if (!iom || !isValid()) {
        return false;
    }
    return iom->cancelEvent(m_sock, IOManager::READ);
}

bool Socket::cancelAll()
{
    IOManager* iom = IOManager::GetThis();
    if (!iom || !isValid()) {
        return false;
    }
    return iom->cancelAll(m_sock);
}

void Socket::initSock()
{
    int val = 1;
    /* 这里这个level有不同的级别，它们对应不同的操作type */
    /* SO_REUSEADDR 允许重用本地地址和端口 */
    setOption(SOL_SOCKET, SO_REUSEADDR, val);
    if (m_type == SOCK_STREAM) {
        /* TCP_NODELAY，不使用Nagle算法 */
        setOption(IPPROTO_TCP, TCP_NODELAY, val);
    }
}

void Socket::newSock()
{
    m_sock = socket(m_family, m_type, m_protocol);
    if (LIKELY(m_sock != -1)) {
        FdMgr::GetInstance()->get(m_sock, true);
        initSock();
    } else {
        ERROR(logger) << "socket(" << m_family << ", " << m_type << ", " << m_protocol << ") errno=" << errno << " errstr=" << strerror(errno);
    }
}

std::string Socket::toString() const
{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

// 这里可以做到在main函数之前执行这个操作，达到初始化ssl的作用
namespace {

struct _SSLInit {
    _SSLInit()
    {
        // OpenSSL 1.1+ 使用统一的初始化入口，旧的 SSL_library_init / SSL_load_error_strings 已废弃
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    }
};

static _SSLInit s_init;

/**
 * @brief 判断字符串是否为 IPv4/IPv6 字面量
 * @details IP 不能作为 SNI 发送，证书校验要走 X509_VERIFY_PARAM_set1_ip_asc。
 */
bool IsIpLiteral(const std::string& host)
{
    if (host.empty()) {
        return false;
    }
    in_addr v4;
    in6_addr v6;
    return inet_pton(AF_INET, host.c_str(), &v4) == 1 || inet_pton(AF_INET6, host.c_str(), &v6) == 1;
}

/**
 * @brief 取并清空当前线程最近一次 OpenSSL 错误
 */
std::string LastSslError()
{
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
}

} // namespace

SSLSocket::ptr SSLSocket::CreateTCP(Address::ptr address)
{
    SSLSocket::ptr sock(new SSLSocket(address->getFamily(), TCP, 0));
    return sock;
}

SSLSocket::ptr SSLSocket::CreateTCP(Address::ptr address, const tls::TlsConfig& conf)
{
    SSLSocket::ptr sock(new SSLSocket(address->getFamily(), TCP, 0));
    if (!sock->setTlsConfig(conf)) {
        return nullptr;
    }
    return sock;
}

SSLSocket::ptr SSLSocket::CreateTCPSocket()
{
    SSLSocket::ptr sock(new SSLSocket(IPv4, TCP, 0));
    return sock;
}

SSLSocket::ptr SSLSocket::CreateTCPSocket6()
{
    SSLSocket::ptr sock(new SSLSocket(IPv6, TCP, 0));
    return sock;
}

SSLSocket::SSLSocket(int family, int type, int protocol)
    : Socket(family, type, protocol)
{
    m_tlsConfig = tls::TlsConfig::DefaultClient();
}

Socket::ptr SSLSocket::accept()
{
    SSLSocket::ptr sock(new SSLSocket(m_family, m_type, m_protocol));
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if (newsock == -1) {
        if (errno == EBADF) {
            DEBUG(logger) << "accept(" << m_sock << ") errno=" << errno << " errstr=" << strerror(errno) << " (socket closed)";
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 非阻塞 socket 上没有待处理的连接，正常行为
        } else {
            ERROR(logger) << "accept(" << m_sock << ") errno=" << errno << ", errstr=" << strerror(errno);
        }
        return nullptr;
    }

    // accept 出的连接继承监听 socket 的 TLS 上下文与配置
    sock->m_tlsContext = m_tlsContext;
    sock->m_tlsConfig = m_tlsConfig;
    sock->m_verifyPeer = m_verifyPeer;
    if (sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

bool SSLSocket::bind(const Address::ptr addr)
{
    return Socket::bind(addr);
}

bool SSLSocket::connect(const Address::ptr addr, uint64_t timeout_ms)
{
    if (!Socket::connect(addr, timeout_ms)) {
        return false;
    }

    if (!m_tlsContext) {
        tls::TlsConfig conf = m_tlsConfig;
        conf.enabled = true;
        conf.server = false;
        m_tlsContext = tls::TlsContext::CreateClient(conf);
        if (!m_tlsContext) {
            ERROR(logger) << "create client tls context failed, addr=" << addr->toString();
            return false;
        }
    }

    return handshake(m_tlsConfig.handshake_timeout_ms);
}

bool SSLSocket::listen(int backlog)
{
    return Socket::listen(backlog);
}

bool SSLSocket::close()
{
    if (m_ssl) {
        // 握手失败时不要发 close_notify，避免向已关闭的连接写数据触发 SIGPIPE
        if (m_handshakeStatus == HandshakeStatus::OK) {
            // 非阻塞 socket 上最多尝试一次，失败直接 close，避免拖住 drain
            SSL_shutdown(m_ssl.get());
        }
        m_ssl.reset();
    }
    m_alpnProtocol.clear();
    return Socket::close();
}

int SSLSocket::send(const void* buffer, size_t length, int flags)
{
    if (!m_ssl) {
        return -1;
    }
    // 非阻塞 socket 上 SSL_write 可能返回 WANT_*，等 fd 就绪后重试
    for (;;) {
        int rt = SSL_write(m_ssl.get(), buffer, length);
        if (rt > 0) {
            return rt;
        }
        int err = SSL_get_error(m_ssl.get(), rt);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            if (sslWaitIO(err, 30000) != WaitResult::OK) {
                return -1;
            }
            continue;
        }
        if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
            return 0;
        }
        return -1;
    }
}

int SSLSocket::send(const iovec* buffers, size_t length, int flags)
{
    if (!m_ssl) {
        return -1;
    }
    int total = 0;
    for (size_t i = 0; i < length; ++i) {
        int tmp = send(buffers[i].iov_base, buffers[i].iov_len, flags);
        if (tmp <= 0) {
            return tmp;
        }
        total += tmp;
        if (tmp != (int)buffers[i].iov_len) {
            break;
        }
    }
    return total;
}

int SSLSocket::sendTo(const void* buffer, size_t length, const Address::ptr to, int flags)
{
    ASSERT(false);
    return -1;
}

int SSLSocket::sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags)
{
    ASSERT(false);
    return -1;
}

int SSLSocket::recv(void* buffer, size_t length, int flags)
{
    if (!m_ssl) {
        return -1;
    }
    const bool peek = (flags & MSG_PEEK) != 0;
    // 非阻塞 socket 上 SSL_read 可能返回 WANT_*，等 fd 就绪后重试
    for (;;) {
        int rt = peek ? SSL_peek(m_ssl.get(), buffer, length) : SSL_read(m_ssl.get(), buffer, length);
        if (rt > 0) {
            return rt;
        }
        int err = SSL_get_error(m_ssl.get(), rt);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            if (sslWaitIO(err, 30000) != WaitResult::OK) {
                return -1;
            }
            continue;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        if (err == SSL_ERROR_SYSCALL && rt == 0) {
            // 对端未发 close_notify 直接关闭
            return 0;
        }
        return -1;
    }
}

int SSLSocket::recv(iovec* buffers, size_t length, int flags)
{
    if (!m_ssl) {
        return -1;
    }
    int total = 0;
    for (size_t i = 0; i < length; ++i) {
        int tmp = recv(buffers[i].iov_base, buffers[i].iov_len, flags);
        if (tmp <= 0) {
            return tmp;
        }
        total += tmp;
        if (tmp != (int)buffers[i].iov_len) {
            break;
        }
    }
    return total;
}

int SSLSocket::recvFrom(void* buffer, size_t length, Address::ptr from, int flags)
{
    ASSERT(false);
    return -1;
}

int SSLSocket::recvFrom(iovec* buffers, size_t length, Address::ptr from, int flags)
{
    ASSERT(false);
    return -1;
}

bool SSLSocket::loadCertificates(const std::string& cert_file, const std::string& key_file)
{
    tls::TlsConfig conf = tls::TlsConfig::DefaultServer();
    conf.cert_file = cert_file;
    conf.key_file = key_file;
    if (m_tlsConfig.handshake_timeout_ms > 0) {
        conf.handshake_timeout_ms = m_tlsConfig.handshake_timeout_ms;
    }
    return setTlsConfig(conf);
}

bool SSLSocket::setTlsConfig(const tls::TlsConfig& v)
{
    m_tlsConfig = v;
    m_verifyPeer = v.verify_peer;
    m_tlsContext.reset();

    if (!v.enabled || !v.server) {
        return true;
    }

    auto ctx = tls::TlsContext::CreateServer(v);
    if (!ctx) {
        ERROR(logger) << "create server tls context failed, conf=" << v.toString();
        return false;
    }
    m_tlsContext = ctx;
    return true;
}

bool SSLSocket::setTlsContext(tls::TlsContext::ptr ctx)
{
    if (!ctx) {
        return false;
    }
    m_tlsContext = ctx;
    m_tlsConfig = ctx->getConfig();
    m_verifyPeer = m_tlsConfig.verify_peer;
    return true;
}

bool SSLSocket::handshake(uint64_t timeout_ms)
{
    if (!m_tlsContext) {
        ERROR(logger) << "tls handshake without context, fd=" << m_sock;
        m_handshakeStatus = HandshakeStatus::ERROR;
        return false;
    }

    if (!m_ssl) {
        auto ctx = m_tlsContext->getShared();
        if (!ctx) {
            ERROR(logger) << "tls handshake context is empty, fd=" << m_sock;
            m_handshakeStatus = HandshakeStatus::ERROR;
            return false;
        }
        m_ssl.reset(SSL_new(ctx.get()), SSL_free);
        if (!m_ssl) {
            ERROR(logger) << "SSL_new failed, fd=" << m_sock << " err=" << LastSslError();
            m_handshakeStatus = HandshakeStatus::ERROR;
            return false;
        }
        if (SSL_set_fd(m_ssl.get(), m_sock) != 1) {
            ERROR(logger) << "SSL_set_fd failed, fd=" << m_sock;
            m_handshakeStatus = HandshakeStatus::ERROR;
            return false;
        }
        if (!m_tlsContext->getConfig().server && !setupClientSsl()) {
            m_handshakeStatus = HandshakeStatus::ERROR;
            return false;
        }
    }

    const bool server = m_tlsContext->getConfig().server;
    if (timeout_ms == 0) {
        timeout_ms = m_tlsConfig.handshake_timeout_ms;
    }
    if (timeout_ms == 0) {
        timeout_ms = 10000;
    }

    // TLS 的读写在用户态处理 WANT_*：把 fd 标记为“用户非阻塞”，让 hook 在 EAGAIN 时
    // 直接返回，而不是在 hook 内部挂起协程。否则 SSL_connect 会阻塞到对端响应/关闭，
    // 握手超时和 SSL_read/SSL_write 的 WANT 重试都无法生效。
    if (IOManager::GetThis()) {
        FdCtx::ptr fd_ctx = FdMgr::GetInstance()->get(m_sock);
        if (fd_ctx) {
            fd_ctx->setUserNonblock(true);
        }
        if (fcntl_f) {
            int flags = fcntl_f(m_sock, F_GETFL, 0);
            if (flags >= 0 && (flags & O_NONBLOCK) == 0 && fcntl_f(m_sock, F_SETFL, flags | O_NONBLOCK) != 0) {
                WARN(logger) << "set user non-blocking failed, fd=" << m_sock << " errno=" << errno << " " << strerror(errno);
            }
        }
    }
    return sslHandshake(server, timeout_ms);
}

const char* SSLSocket::HandshakeStatusString(HandshakeStatus status)
{
    switch (status) {
    case HandshakeStatus::NOT_STARTED:
        return "not_started";
    case HandshakeStatus::OK:
        return "ok";
    case HandshakeStatus::PEER_CLOSED:
        return "peer_closed";
    case HandshakeStatus::TIMEOUT:
        return "timeout";
    case HandshakeStatus::VERIFY_FAILED:
        return "verify_failed";
    case HandshakeStatus::ERROR:
    default:
        return "error";
    }
}

bool SSLSocket::setupClientSsl()
{
    if (!m_ssl) {
        return false;
    }

    const bool is_ip = IsIpLiteral(m_serverName);
    if (!m_serverName.empty() && !is_ip) {
        if (SSL_set_tlsext_host_name(m_ssl.get(), m_serverName.c_str()) != 1) {
            WARN(logger) << "SSL_set_tlsext_host_name failed, host=" << m_serverName << " fd=" << m_sock;
        }
    }

    if (m_verifyPeer) {
        if (m_serverName.empty()) {
            WARN(logger) << "SSL client has no server name, skip hostname verification, fd=" << m_sock;
        } else {
            X509_VERIFY_PARAM* param = SSL_get0_param(m_ssl.get());
            X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            int rt = is_ip ? X509_VERIFY_PARAM_set1_ip_asc(param, m_serverName.c_str()) : X509_VERIFY_PARAM_set1_host(param, m_serverName.c_str(), 0);
            if (rt != 1) {
                WARN(logger) << "set tls verify host failed, host=" << m_serverName << " fd=" << m_sock;
            }
        }
    }

    if (!m_tlsConfig.alpn.empty()) {
        std::string wire;
        wire.reserve(m_tlsConfig.alpn.size() * 8);
        for (const auto& proto : m_tlsConfig.alpn) {
            wire.push_back(static_cast<char>(proto.size()));
            wire.append(proto);
        }
        if (SSL_set_alpn_protos(m_ssl.get(), reinterpret_cast<const unsigned char*>(wire.data()), static_cast<unsigned int>(wire.size())) != 0) {
            WARN(logger) << "SSL_set_alpn_protos failed, fd=" << m_sock;
        }
    }

    return true;
}

bool SSLSocket::sslHandshake(bool server, uint64_t timeout_ms)
{
    const char* op = server ? "SSL_accept" : "SSL_connect";

    IOManager* iom = IOManager::GetThis();
    if (!iom) {
        if (server) {
            ERROR(logger) << "SSL_accept requires IOManager, fd=" << m_sock;
            m_handshakeStatus = HandshakeStatus::ERROR;
            return false;
        }
        // 非调度器线程上 fd 通常是阻塞的，直接同步握手
        int ret = SSL_connect(m_ssl.get());
        if (ret == 1) {
            m_handshakeStatus = HandshakeStatus::OK;
            m_alpnProtocol = tls::TlsContext::GetAlpn(m_ssl.get());
            return true;
        }

        int err = SSL_get_error(m_ssl.get(), ret);
        if (err == SSL_ERROR_ZERO_RETURN) {
            m_handshakeStatus = HandshakeStatus::PEER_CLOSED;
            return false;
        }
        long verify_result = SSL_get_verify_result(m_ssl.get());
        if (verify_result != X509_V_OK) {
            m_handshakeStatus = HandshakeStatus::VERIFY_FAILED;
            WARN(logger) << "certificate verify failed: " << X509_verify_cert_error_string(verify_result) << " fd=" << m_sock;
            return false;
        }
        m_handshakeStatus = HandshakeStatus::ERROR;
        WARN(logger) << op << " failed: " << LastSslError() << " fd=" << m_sock;
        return false;
    }

    // 非阻塞 socket 上的 SSL 握手需要多次往返
    const uint64_t deadline_ms = TimeUtil::GetCurrentMs() + timeout_ms;
    for (;;) {
        int ret = server ? SSL_accept(m_ssl.get()) : SSL_connect(m_ssl.get());
        if (ret == 1) {
            m_handshakeStatus = HandshakeStatus::OK;
            m_alpnProtocol = tls::TlsContext::GetAlpn(m_ssl.get());
            if (!m_alpnProtocol.empty()) {
                DEBUG(logger) << "tls handshake ok, fd=" << m_sock << " alpn=" << m_alpnProtocol;
            }
            return true;
        }

        int err = SSL_get_error(m_ssl.get(), ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            uint64_t now_ms = TimeUtil::GetCurrentMs();
            if (now_ms >= deadline_ms) {
                WARN(logger) << op << " timed out after " << timeout_ms << "ms, fd=" << m_sock;
                m_handshakeStatus = HandshakeStatus::TIMEOUT;
                return false;
            }
            WaitResult wait_result = sslWaitIO(err, deadline_ms - now_ms);
            if (wait_result == WaitResult::TIMEOUT) {
                WARN(logger) << op << " timed out after " << timeout_ms << "ms, fd=" << m_sock;
                m_handshakeStatus = HandshakeStatus::TIMEOUT;
                return false;
            }
            if (wait_result == WaitResult::ERROR) {
                WARN(logger) << op << " wait io failed, fd=" << m_sock;
                m_handshakeStatus = HandshakeStatus::ERROR;
                return false;
            }
            continue;
        }

        if (err == SSL_ERROR_ZERO_RETURN || (err == SSL_ERROR_SYSCALL && ret == 0)) {
            // 对端在握手完成前关闭了连接
            DEBUG(logger) << op << ": peer closed during handshake, fd=" << m_sock;
            m_handshakeStatus = HandshakeStatus::PEER_CLOSED;
            return false;
        }

        long verify_result = SSL_get_verify_result(m_ssl.get());
        if (verify_result != X509_V_OK) {
            WARN(logger) << "certificate verify failed: " << X509_verify_cert_error_string(verify_result) << " fd=" << m_sock;
            m_handshakeStatus = HandshakeStatus::VERIFY_FAILED;
            return false;
        }

        unsigned long ssl_err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));
        WARN(logger) << op << " failed: " << err_buf << " (ssl_err=" << err << ") fd=" << m_sock;
        m_handshakeStatus = HandshakeStatus::ERROR;
        return false;
    }
}

SSLSocket::WaitResult SSLSocket::sslWaitIO(int sslError, uint64_t timeout_ms)
{
    IOManager* iom = IOManager::GetThis();
    if (!iom) {
        return WaitResult::ERROR;
    }

    IOManager::Event event = (sslError == SSL_ERROR_WANT_READ) ? IOManager::READ : IOManager::WRITE;

    struct WaitTimerInfo {
        std::atomic<int> done{0};
        std::atomic<int> timed_out{0};
    };
    auto info = std::make_shared<WaitTimerInfo>();

    Timer::ptr timer = iom->addTimer(timeout_ms, [info, fd = m_sock, iom, event]() {
        if (info->done.load() != 0 || info->timed_out.exchange(1) != 0) {
            return;
        }
        iom->cancelEvent(fd, event);
    });

    if (UNLIKELY(iom->addEvent(m_sock, event) != 0)) {
        ERROR(logger) << "sslWaitIO addEvent failed for fd=" << m_sock;
        if (timer) {
            timer->cancel();
        }
        return WaitResult::ERROR;
    }

    Fiber::YieldToHold();

    // 标记等待已结束，避免定时器在 fd 被复用后再操作旧事件
    info->done.store(1);
    if (timer) {
        timer->cancel();
    }
    if (info->timed_out.load() != 0) {
        return WaitResult::TIMEOUT;
    }
    if (!isValid()) {
        return WaitResult::ERROR;
    }
    return WaitResult::OK;
}

std::ostream& SSLSocket::dump(std::ostream& os) const
{
    os << "[SSLSocket sock=" << m_sock << " is_connected=" << m_isConnected << " family=" << m_family << " type=" << m_type
       << " protocol=" << m_protocol;

    if (m_localAddress) {
        os << " local_address=" << m_localAddress->toString();
    }

    if (m_remoteAddress) {
        os << " remote_address=" << m_remoteAddress->toString();
    }

    if (!m_alpnProtocol.empty()) {
        os << " alpn=" << m_alpnProtocol;
    }
    os << " handshake=" << HandshakeStatusString(m_handshakeStatus);
    os << "]";
    return os;
}

bool SSLSocket::init(int sock)
{
    // accept 只建立 TCP 连接；TLS 握手由 TcpServer 在 io worker 中显式调用 handshake()
    return Socket::init(sock);
}

std::ostream& operator<<(std::ostream& os, const Socket& sock)
{
    return sock.dump(os);
}

} // namespace chen
