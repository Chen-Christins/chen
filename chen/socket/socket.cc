#include "socket.h"

#include <netinet/tcp.h>

#include "../hook/fd_manager.h"
#include "../hook/hook.h"
#include "../iomanager/iomanager.h"
#include "../log/log.h"
#include "../util/macro.h"

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

} // namespace

SSLSocket::ptr SSLSocket::CreateTCP(Address::ptr address)
{
    SSLSocket::ptr sock(new SSLSocket(address->getFamily(), TCP, 0));
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
    sock->m_ctx = m_ctx;
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

    m_ctx.reset(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    if (!m_ctx) {
        ERROR(logger) << "SSL_CTX_new(TLS_client_method) failed, addr=" << addr->toString();
        return false;
    }

    // 校验策略要在 SSL_new 之前设置到 ctx 上
    if (m_verifyPeer) {
        SSL_CTX_set_verify(m_ctx.get(), SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(m_ctx.get()) != 1) {
            WARN(logger) << "SSL_CTX_set_default_verify_paths failed, 可用 SSL_CERT_FILE/SSL_CERT_DIR 指定 CA";
        }
    } else {
        SSL_CTX_set_verify(m_ctx.get(), SSL_VERIFY_NONE, nullptr);
    }

    m_ssl.reset(SSL_new(m_ctx.get()), SSL_free);
    if (!m_ssl) {
        ERROR(logger) << "SSL_new failed, addr=" << addr->toString();
        return false;
    }
    SSL_set_fd(m_ssl.get(), m_sock);

    if (!m_serverName.empty()) {
        // 虚拟主机 / CDN 必须带 SNI，否则服务端可能返回默认站点证书
        SSL_set_tlsext_host_name(m_ssl.get(), m_serverName.c_str());
        // 用同一个 host 校验证书主机名
        if (m_verifyPeer && SSL_set1_host(m_ssl.get(), m_serverName.c_str()) != 1) {
            WARN(logger) << "SSL_set1_host failed, host=" << m_serverName << " fd=" << m_sock;
        }
    } else if (m_verifyPeer) {
        WARN(logger) << "SSL client has no server name, skip hostname verification, addr=" << addr->toString();
    }

    return sslHandshake(false);
}

bool SSLSocket::listen(int backlog)
{
    return Socket::listen(backlog);
}

bool SSLSocket::close()
{
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
            if (!sslWaitIO(err)) {
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
            if (!sslWaitIO(err)) {
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
// 证书加载
bool SSLSocket::loadCertificates(const std::string& cert_file, const std::string& key_file)
{
    m_ctx.reset(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    if (SSL_CTX_use_certificate_chain_file(m_ctx.get(), cert_file.c_str()) != 1) {
        ERROR(logger) << "SSL_CTX_use_certificate_chain_file(" << cert_file << ") error";
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(m_ctx.get(), key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        ERROR(logger) << "SSL_CTX_use_PrivateKey_file(" << key_file << ") error";
        return false;
    }
    if (SSL_CTX_check_private_key(m_ctx.get()) != 1) {
        ERROR(logger) << "SSL_CTX_check_private_key cert_file=" << cert_file << ", key_file=" << key_file;
        return false;
    }
    return true;
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
    os << "]";
    return os;
}

bool SSLSocket::init(int sock)
{
    bool v = Socket::init(sock);
    if (v) {
        // 这里进行https的握手
        m_ssl.reset(SSL_new(m_ctx.get()), SSL_free);
        SSL_set_fd(m_ssl.get(), m_sock);
        v = sslHandshake(true);
    }
    return v;
}

bool SSLSocket::sslWaitIO(int sslError)
{
    IOManager* iom = IOManager::GetThis();
    if (!iom) {
        return false;
    }

    static constexpr uint64_t kWaitTimeoutMs = 30000;
    IOManager::Event event = (sslError == SSL_ERROR_WANT_READ) ? IOManager::READ : IOManager::WRITE;

    struct WaitTimerInfo {
        int cancelled = 0;
    };
    auto info = std::make_shared<WaitTimerInfo>();
    std::weak_ptr<WaitTimerInfo> winfo(info);

    Timer::ptr timer = iom->addConditionTimer(
        kWaitTimeoutMs,
        [winfo, fd = m_sock, iom, event]() {
            auto t = winfo.lock();
            if (!t || t->cancelled) {
                return;
            }
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, event);
        },
        winfo);

    if (UNLIKELY(iom->addEvent(m_sock, event) != 0)) {
        ERROR(logger) << "sslWaitIO addEvent failed for fd=" << m_sock;
        timer->cancel();
        return false;
    }

    Fiber::YieldToHold();

    if (timer) {
        timer->cancel();
    }
    if (info->cancelled) {
        WARN(logger) << "ssl io timed out on fd=" << m_sock;
        return false;
    }
    return true;
}

bool SSLSocket::sslHandshake(bool server)
{
    const char* op = server ? "SSL_accept" : "SSL_connect";

    IOManager* iom = IOManager::GetThis();
    if (!iom) {
        if (server) {
            ERROR(logger) << "SSL_accept requires IOManager, fd=" << m_sock;
            return false;
        }
        // 非调度器线程上 fd 通常是阻塞的，直接同步握手
        return SSL_connect(m_ssl.get()) == 1;
    }

    // SSL 握手超时 30 秒
    static constexpr uint64_t kHandshakeTimeoutMs = 30000;
    struct HandshakeTimerInfo {
        int cancelled = 0;
    };
    auto tinfo = std::make_shared<HandshakeTimerInfo>();
    std::weak_ptr<HandshakeTimerInfo> winfo(tinfo);

    // 非阻塞 socket 上的 SSL 握手需要多次往返
    while (true) {
        int ret = server ? SSL_accept(m_ssl.get()) : SSL_connect(m_ssl.get());
        if (ret == 1) {
            return true;
        }
        int err = SSL_get_error(m_ssl.get(), ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            IOManager::Event event = (err == SSL_ERROR_WANT_READ) ? IOManager::READ : IOManager::WRITE;

            // 添加超时定时器
            Timer::ptr timer = iom->addConditionTimer(
                kHandshakeTimeoutMs,
                [winfo, fd = m_sock, iom, event]() {
                    auto t = winfo.lock();
                    if (!t || t->cancelled) {
                        return;
                    }
                    t->cancelled = ETIMEDOUT;
                    iom->cancelEvent(fd, event);
                },
                winfo);

            if (UNLIKELY(iom->addEvent(m_sock, event) != 0)) {
                ERROR(logger) << op << " addEvent failed for fd=" << m_sock;
                timer->cancel();
                return false;
            }

            Fiber::YieldToHold();

            if (timer) {
                timer->cancel();
            }

            // 超时被唤醒
            if (tinfo->cancelled) {
                WARN(logger) << "SSL handshake timed out on fd=" << m_sock;
                return false;
            }
        } else if (err == SSL_ERROR_ZERO_RETURN) {
            // 对端在握手完成前关闭了连接
            DEBUG(logger) << op << ": peer closed during handshake, fd=" << m_sock;
            return false;
        } else {
            // 真正的错误（证书问题、协议不匹配等）
            unsigned long ssl_err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));
            if (!server) {
                long verify_result = SSL_get_verify_result(m_ssl.get());
                if (verify_result != X509_V_OK) {
                    WARN(logger) << "certificate verify failed: " << X509_verify_cert_error_string(verify_result) << " fd=" << m_sock;
                }
            }
            WARN(logger) << op << " failed: " << err_buf << " (ssl_err=" << err << ") fd=" << m_sock;
            return false;
        }
    }
}

std::ostream& operator<<(std::ostream& os, const Socket& sock)
{
    return sock.dump(os);
}

} // namespace chen
