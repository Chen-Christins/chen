/**
 * @file Redis.hpp
 * @brief Redis 客户端封装
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <sys/time.h>

#include <hiredis-vip/adapters/libevent.h>
#include <hiredis-vip/hircluster.h>
#include <hiredis-vip/hiredis.h>

#include "../Schedule/Schedule.hpp"
#include "../Util/Singleton.hpp"
#include "FoxThread.hpp"

namespace chen {

typedef std::shared_ptr<redisReply> ReplyPtr;

class IRedis {
public:
    enum Type {
        REDIS = 1,
        REDIS_CLUSTER = 2,
        FOX_REDIS = 3,
        FOX_REDIS_CLUSTER = 4
    };
    typedef std::shared_ptr<IRedis> ptr;
    IRedis();
    virtual ~IRedis();

    virtual ReplyPtr cmd(const char* fmt, ...) = 0;
    virtual ReplyPtr cmd(const char* fmt, va_list ap) = 0;
    virtual ReplyPtr cmd(const std::vector<std::string>& argv) = 0;

    const std::string& getName() const { return name_; }
    void setName(const std::string& v) { name_ = v; }

    const std::string& getPasswd() const { return passwd_; }
    void setPasswd(const std::string& v) { passwd_ = v; }

    Type getType() const { return type_; }

protected:
    std::string name_;
    std::string passwd_;
    Type type_;
    bool log_enable_;
};

class ISyncRedis : public IRedis {
public:
    typedef std::shared_ptr<ISyncRedis> ptr;
    virtual ~ISyncRedis();

    virtual bool reconnect() = 0;
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0) = 0;
    virtual bool connect() = 0;
    virtual bool setTimeout(uint64_t ms) = 0;

    virtual int appendCmd(const char* fmt, ...) = 0;
    virtual int appendCmd(const char* fmt, va_list ap) = 0;
    virtual int appendCmd(const std::vector<std::string>& argv) = 0;

    virtual ReplyPtr getReply() = 0;

    uint64_t getLastActiveTime() const { return last_active_time_; }
    void setLastActiveTime(uint64_t v) { last_active_time_ = v; }

protected:
    uint64_t last_active_time_ = 0;
};

class Redis : public ISyncRedis {
public:
    typedef std::shared_ptr<Redis> ptr;
    Redis();
    Redis(const std::map<std::string, std::string>& conf);

    virtual bool reconnect();
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0);
    virtual bool connect();
    virtual bool setTimeout(uint64_t ms);

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    virtual int appendCmd(const char* fmt, ...);
    virtual int appendCmd(const char* fmt, va_list ap);
    virtual int appendCmd(const std::vector<std::string>& argv);

    virtual ReplyPtr getReply();

private:
    std::string host_;
    uint32_t port_;
    uint32_t connect_ms_;
    struct timeval cmd_timeout_;
    std::shared_ptr<redisContext> context_;
};

class RedisCluster : public ISyncRedis {
public:
    typedef std::shared_ptr<RedisCluster> ptr;
    RedisCluster();
    RedisCluster(const std::map<std::string, std::string>& conf);

    virtual bool reconnect();
    virtual bool connect(const std::string& ip, int port, uint64_t ms = 0);
    virtual bool connect();
    virtual bool setTimeout(uint64_t ms);

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    virtual int appendCmd(const char* fmt, ...);
    virtual int appendCmd(const char* fmt, va_list ap);
    virtual int appendCmd(const std::vector<std::string>& argv);

    virtual ReplyPtr getReply();

private:
    std::string host_;
    uint32_t port_;
    uint32_t connect_ms_;
    struct timeval cmd_timeout_;
    std::shared_ptr<redisClusterContext> context_;
};

class FoxRedis : public IRedis {
public:
    typedef std::shared_ptr<FoxRedis> ptr;
    enum STATUS {
        UNCONNECTED = 0,
        CONNECTING = 1,
        CONNECTED = 2
    };
    enum RESULT {
        OK = 0,
        TIME_OUT = 1,
        CONNECT_ERR = 2,
        CMD_ERR = 3,
        REPLY_NULL = 4,
        REPLY_ERR = 5,
        INIT_ERR = 6
    };

    FoxRedis(IFoxThread::ptr owner, FoxThread* thr, const std::map<std::string, std::string>& conf);
    ~FoxRedis();

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    bool init();
    int getCtxCount() const { return ctx_count_; }

private:
    static void OnAuthCb(redisAsyncContext* c, void* rp, void* priv);

private:
    struct FCtx {
        std::string cmd;
        Scheduler* scheduler;
        Fiber::ptr fiber;
        ReplyPtr rpy;
    };

    struct Ctx {
        typedef std::shared_ptr<Ctx> ptr;

        event* ev;
        bool timeout;
        FoxRedis* rds;
        std::string cmd;
        FCtx* fctx;
        FoxThread* thread;

        Ctx(FoxRedis* rds);
        ~Ctx();
        bool init();
        void cancelEvent();
        static void EventCb(int fd, short event, void* d);
    };

private:
    virtual void pcmd(FCtx* ctx);
    bool pinit();
    void delayDelete(redisAsyncContext* c);

private:
    static void ConnectCb(const redisAsyncContext* c, int status);
    static void DisconnectCb(const redisAsyncContext* c, int status);
    static void CmdCb(redisAsyncContext* c, void* r, void* privdata);
    static void TimeCb(int fd, short event, void* d);

private:
    /// 拥有 thread_ 的线程/线程池，保证其生命周期不早于本连接
    IFoxThread::ptr thread_owner_;
    FoxThread* thread_;
    std::shared_ptr<redisAsyncContext> context_;
    std::string host_;
    uint16_t port_;
    STATUS status_;
    int ctx_count_;

    struct timeval cmd_timeout_;
    std::string err_;
    struct event* event_;
};

class FoxRedisCluster : public IRedis {
public:
    typedef std::shared_ptr<FoxRedisCluster> ptr;
    enum STATUS {
        UNCONNECTED = 0,
        CONNECTING = 1,
        CONNECTED = 2
    };
    enum RESULT {
        OK = 0,
        TIME_OUT = 1,
        CONNECT_ERR = 2,
        CMD_ERR = 3,
        REPLY_NULL = 4,
        REPLY_ERR = 5,
        INIT_ERR = 6
    };

    FoxRedisCluster(IFoxThread::ptr owner, FoxThread* thr, const std::map<std::string, std::string>& conf);
    ~FoxRedisCluster();

    virtual ReplyPtr cmd(const char* fmt, ...);
    virtual ReplyPtr cmd(const char* fmt, va_list ap);
    virtual ReplyPtr cmd(const std::vector<std::string>& argv);

    int getCtxCount() const { return ctx_count_; }

    bool init();

private:
    struct FCtx {
        std::string cmd;
        Scheduler* scheduler;
        Fiber::ptr fiber;
        ReplyPtr rpy;
    };
    struct Ctx {
        typedef std::shared_ptr<Ctx> ptr;

        event* ev;
        bool timeout;
        FoxRedisCluster* rds;
        FCtx* fctx;
        std::string cmd;
        FoxThread* thread;
        void cancelEvent();

        Ctx(FoxRedisCluster* rds);
        ~Ctx();
        bool init();
        static void EventCb(int fd, short event, void* d);
    };

private:
    virtual void pcmd(FCtx* ctx);
    bool pinit();
    void delayDelete(redisAsyncContext* c);
    static void OnAuthCb(redisClusterAsyncContext* c, void* rp, void* priv);

private:
    static void ConnectCb(const redisAsyncContext* c, int status);
    static void DisconnectCb(const redisAsyncContext* c, int status);
    static void CmdCb(redisClusterAsyncContext* c, void* r, void* privdata);
    static void TimeCb(int fd, short event, void* d);

private:
    /// 拥有 thread_ 的线程/线程池，保证其生命周期不早于本连接
    IFoxThread::ptr thread_owner_;
    FoxThread* thread_;
    std::shared_ptr<redisClusterAsyncContext> context_;
    std::string host_;
    STATUS status_;
    int ctx_count_;
    timeval cmd_timeout_;
    std::string err_;
    event* event_;
};

class RedisManager {
public:
    void init();

    IRedis::ptr get(const std::string& name);

    std::ostream& dump(std::ostream& os);

    void freeAll();

private:
    void freeRedis(IRedis* r);

private:
    /// 保护 datas_ 的读写锁
    std::shared_mutex mutex_;
    /// 保护初始化状态的互斥锁
    std::mutex init_mutex_;
    /// 是否已初始化（进程内仅一次，避免热重载销毁在用连接）
    bool inited_ = false;
    std::map<std::string, std::list<IRedis*>> datas_;
    std::map<std::string, std::map<std::string, std::string>> config_;
};

typedef Singleton<RedisManager> RedisMgr;

class RedisUtil {
public:
    static ReplyPtr Cmd(const std::string& name, const char* fmt, ...);
    static ReplyPtr Cmd(const std::string& name, const char* fmt, va_list ap);
    static ReplyPtr Cmd(const std::string& name, const std::vector<std::string>& args);

    static ReplyPtr TryCmd(const std::string& name, uint32_t count, const char* fmt, ...);
    static ReplyPtr TryCmd(const std::string& name, uint32_t count, const std::vector<std::string>& args);
};

} // namespace chen
