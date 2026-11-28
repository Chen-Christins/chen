#include "Redis.hpp"

#include <atomic>
#include <chrono>
#include <future>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"
#include "../Util/Macro.hpp"
#include "../Util/Util.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");
static ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_redis =
    Config::Lookup("redis.config", std::map<std::string, std::map<std::string, std::string>>(), "redis config");

IRedis::~IRedis()
{
}

ISyncRedis::~ISyncRedis()
{
}

static std::string get_value(const std::map<std::string, std::string>& m, const std::string& key, const std::string& def = "")
{
    auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

redisReply* RedisReplyClone(redisReply* r)
{
    redisReply* c = (redisReply*)calloc(1, sizeof(*c));
    c->type = r->type;

    switch (r->type) {
    case REDIS_REPLY_INTEGER:
        c->integer = r->integer;
        break;
    case REDIS_REPLY_ARRAY:
        if (r->element != NULL && r->elements > 0) {
            c->element = (redisReply**)calloc(r->elements, sizeof(redisReply*));
            c->elements = r->elements;
            for (size_t i = 0; i < r->elements; ++i) {
                c->element[i] = RedisReplyClone(r->element[i]);
            }
        }
        break;
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_STRING:
        if (r->str == NULL) {
            c->str = NULL;
        } else {
            // c->str = strndup(r->str, r->len);
            c->str = (char*)malloc(r->len + 1);
            memcpy(c->str, r->str, r->len);
            c->str[r->len] = '\0';
        }
        c->len = r->len;
        break;
    default:
        break;
    }
    return c;
}

IRedis::IRedis()
    : type_()
    , log_enable_(true)
{
}

Redis::Redis()
    : port_(0)
    , connect_ms_(0)
    , cmd_timeout_()
{
    type_ = IRedis::REDIS;
}

Redis::Redis(const std::map<std::string, std::string>& conf)
    : connect_ms_(0)
{
    type_ = IRedis::REDIS;
    auto tmp = get_value(conf, "host");
    auto pos = tmp.find(":");
    host_ = tmp.substr(0, pos);
    port_ = TypeUtil::Atoi(tmp.substr(pos + 1));
    passwd_ = get_value(conf, "passwd");
    log_enable_ = TypeUtil::Atoi(get_value(conf, "log_enable", "1"));

    tmp = get_value(conf, "timeout_com");
    if (tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = TypeUtil::Atoi(tmp);

    cmd_timeout_.tv_sec = v / 1000;
    cmd_timeout_.tv_usec = v % 1000 * 1000;
}

bool Redis::reconnect()
{
    if (!context_) {
        ERROR(logger) << "Redis::reconnect m_context is null";
        return false;
    }
    return redisReconnect(context_.get());
}

bool Redis::connect()
{
    return connect(host_, port_, 50);
}

bool Redis::connect(const std::string& ip, int port, uint64_t ms)
{
    host_ = ip;
    port_ = port;
    connect_ms_ = ms;
    if (context_) {
        return true;
    }
    timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    auto c = redisConnectWithTimeout(ip.c_str(), port, tv);
    if (c) {
        if (cmd_timeout_.tv_sec || cmd_timeout_.tv_usec) {
            setTimeout(cmd_timeout_.tv_sec * 1000 + cmd_timeout_.tv_usec / 1000);
        }
        context_.reset(c, redisFree);

        if (!passwd_.empty()) {
            auto r = (redisReply*)redisCommand(c, "auth %s", passwd_.c_str());
            if (!r) {
                ERROR(logger) << "auth error:(" << host_ << ":" << port_ << ", " << name_ << ")";
                return false;
            }
            if (r->type != REDIS_REPLY_STATUS) {
                ERROR(logger) << "auth reply type error:" << r->type << "(" << host_ << ":" << port_ << ", " << name_
                              << ")";
                return false;
            }
            if (!r->str) {
                ERROR(logger) << "auth reply str error: NULL(" << host_ << ":" << port_ << ", " << name_ << ")";
                return false;
            }
            if (strcmp(r->str, "OK") == 0) {
                return true;
            } else {
                ERROR(logger) << "auth error: " << r->str << "(" << host_ << ":" << port_ << ", " << name_ << ")";
                return false;
            }
        }
        return true;
    }
    return false;
}

bool Redis::setTimeout(uint64_t v)
{
    cmd_timeout_.tv_sec = v / 1000;
    cmd_timeout_.tv_usec = v % 1000 * 1000;
    if (!context_) {
        ERROR(logger) << "Redis::setTimeout m_context is null";
        return false;
    }
    redisSetTimeout(context_.get(), cmd_timeout_);
    return true;
}

ReplyPtr Redis::cmd(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = cmd(fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr Redis::cmd(const char* fmt, va_list ap)
{
    if (!context_) {
        ERROR(logger) << "Redis::cmd m_context is null";
        return nullptr;
    }
    auto r = (redisReply*)redisvCommand(context_.get(), fmt, ap);
    if (!r) {
        if (log_enable_) {
            ERROR(logger) << "redisCommand error: (" << fmt << ")(" << host_ << ":" << port_ << ")(" << name_ << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if (r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if (log_enable_) {
        ERROR(logger) << "redisCommand error: (" << fmt << ")(" << host_ << ":" << port_ << ")(" << name_ << ")"
                      << ": " << r->str;
    }
    return nullptr;
}

ReplyPtr Redis::cmd(const std::vector<std::string>& argv)
{
    if (!context_) {
        ERROR(logger) << "Redis::cmd m_context is null";
        return nullptr;
    }
    std::vector<const char*> v;
    std::vector<size_t> l;
    for (auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }

    auto r = (redisReply*)redisCommandArgv(context_.get(), argv.size(), &v[0], &l[0]);
    if (!r) {
        if (log_enable_) {
            ERROR(logger) << "redisCommandArgv error: (" << host_ << ":" << port_ << ")(" << name_ << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if (r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if (log_enable_) {
        ERROR(logger) << "redisCommandArgv error: (" << host_ << ":" << port_ << ")(" << name_ << ")" << r->str;
    }
    return nullptr;
}

ReplyPtr Redis::getReply()
{
    if (!context_) {
        ERROR(logger) << "Redis::getReply m_context is null";
        return nullptr;
    }
    redisReply* r = nullptr;
    if (redisGetReply(context_.get(), (void**)&r) == REDIS_OK) {
        ReplyPtr rt(r, freeReplyObject);
        return rt;
    }
    if (log_enable_) {
        ERROR(logger) << "redisGetReply error: (" << host_ << ":" << port_ << ")(" << name_ << ")";
    }
    return nullptr;
}

int Redis::appendCmd(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rt = appendCmd(fmt, ap);
    va_end(ap);
    return rt;
}

int Redis::appendCmd(const char* fmt, va_list ap)
{
    if (!context_) {
        ERROR(logger) << "Redis::appendCmd m_context is null";
        return REDIS_ERR;
    }
    return redisvAppendCommand(context_.get(), fmt, ap);
}

int Redis::appendCmd(const std::vector<std::string>& argv)
{
    if (!context_) {
        ERROR(logger) << "Redis::appendCmd m_context is null";
        return REDIS_ERR;
    }
    std::vector<const char*> v;
    std::vector<size_t> l;
    for (auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }
    return redisAppendCommandArgv(context_.get(), argv.size(), &v[0], &l[0]);
}

RedisCluster::RedisCluster()
    : port_(0)
    , connect_ms_(0)
    , cmd_timeout_()
{
    type_ = IRedis::REDIS_CLUSTER;
}

RedisCluster::RedisCluster(const std::map<std::string, std::string>& conf)
    : port_(0)
    , connect_ms_(0)
{
    type_ = IRedis::REDIS_CLUSTER;
    host_ = get_value(conf, "host");
    passwd_ = get_value(conf, "passwd");
    log_enable_ = TypeUtil::Atoi(get_value(conf, "log_enable", "1"));
    auto tmp = get_value(conf, "timeout_com");
    if (tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = TypeUtil::Atoi(tmp);

    cmd_timeout_.tv_sec = v / 1000;
    cmd_timeout_.tv_usec = v % 1000 * 1000;
}

////RedisCluster
bool RedisCluster::reconnect()
{
    return true;
    // return redisReconnect(context_.get());
}

bool RedisCluster::connect()
{
    return connect(host_, port_, 50);
}

bool RedisCluster::connect(const std::string& ip, int port, uint64_t ms)
{
    host_ = ip;
    port_ = port;
    connect_ms_ = ms;
    if (context_) {
        return true;
    }
    timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    auto c = redisClusterConnectWithTimeout(ip.c_str(), tv, 0);
    if (c) {
        context_.reset(c, redisClusterFree);
        if (!passwd_.empty()) {
            auto r = (redisReply*)redisClusterCommand(c, "auth %s", passwd_.c_str());
            if (!r) {
                ERROR(logger) << "auth error:(" << host_ << ":" << port_ << ", " << name_ << ")";
                return false;
            }
            if (r->type != REDIS_REPLY_STATUS) {
                ERROR(logger) << "auth reply type error:" << r->type << "(" << host_ << ":" << port_ << ", " << name_
                              << ")";
                return false;
            }
            if (!r->str) {
                ERROR(logger) << "auth reply str error: NULL(" << host_ << ":" << port_ << ", " << name_ << ")";
                return false;
            }
            if (strcmp(r->str, "OK") == 0) {
                return true;
            } else {
                ERROR(logger) << "auth error: " << r->str << "(" << host_ << ":" << port_ << ", " << name_ << ")";
                return false;
            }
        }
        return true;
    }
    return false;
}

bool RedisCluster::setTimeout(uint64_t ms)
{
    // timeval tv = {(int)ms / 1000, (int)ms % 1000 * 1000};
    // redisSetTimeout(context_.get(), tv);
    return true;
}

ReplyPtr RedisCluster::cmd(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = cmd(fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr RedisCluster::cmd(const char* fmt, va_list ap)
{
    if (!context_) {
        ERROR(logger) << "RedisCluster::cmd m_context is null";
        return nullptr;
    }
    auto r = (redisReply*)redisClustervCommand(context_.get(), fmt, ap);
    if (!r) {
        if (log_enable_) {
            ERROR(logger) << "redisCommand error: (" << fmt << ")(" << host_ << ":" << port_ << ")(" << name_ << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if (r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if (log_enable_) {
        ERROR(logger) << "redisCommand error: (" << fmt << ")(" << host_ << ":" << port_ << ")(" << name_ << ")"
                      << ": " << r->str;
    }
    return nullptr;
}

ReplyPtr RedisCluster::cmd(const std::vector<std::string>& argv)
{
    if (!context_) {
        ERROR(logger) << "RedisCluster::cmd m_context is null";
        return nullptr;
    }
    std::vector<const char*> v;
    std::vector<size_t> l;
    for (auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }

    auto r = (redisReply*)redisClusterCommandArgv(context_.get(), argv.size(), &v[0], &l[0]);
    if (!r) {
        if (log_enable_) {
            ERROR(logger) << "redisCommandArgv error: (" << host_ << ":" << port_ << ")(" << name_ << ")";
        }
        return nullptr;
    }
    ReplyPtr rt(r, freeReplyObject);
    if (r->type != REDIS_REPLY_ERROR) {
        return rt;
    }
    if (log_enable_) {
        ERROR(logger) << "redisCommandArgv error: (" << host_ << ":" << port_ << ")(" << name_ << ")" << r->str;
    }
    return nullptr;
}

ReplyPtr RedisCluster::getReply()
{
    if (!context_) {
        ERROR(logger) << "RedisCluster::getReply m_context is null";
        return nullptr;
    }
    redisReply* r = nullptr;
    if (redisClusterGetReply(context_.get(), (void**)&r) == REDIS_OK) {
        ReplyPtr rt(r, freeReplyObject);
        return rt;
    }
    if (log_enable_) {
        ERROR(logger) << "redisGetReply error: (" << host_ << ":" << port_ << ")(" << name_ << ")";
    }
    return nullptr;
}

int RedisCluster::appendCmd(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rt = appendCmd(fmt, ap);
    va_end(ap);
    return rt;
}

int RedisCluster::appendCmd(const char* fmt, va_list ap)
{
    if (!context_) {
        ERROR(logger) << "RedisCluster::appendCmd m_context is null";
        return REDIS_ERR;
    }
    return redisClustervAppendCommand(context_.get(), fmt, ap);
}

int RedisCluster::appendCmd(const std::vector<std::string>& argv)
{
    if (!context_) {
        ERROR(logger) << "RedisCluster::appendCmd m_context is null";
        return REDIS_ERR;
    }
    std::vector<const char*> v;
    std::vector<size_t> l;
    for (auto& i : argv) {
        v.push_back(i.c_str());
        l.push_back(i.size());
    }
    return redisClusterAppendCommandArgv(context_.get(), argv.size(), &v[0], &l[0]);
}

FoxRedis::FoxRedis(IFoxThread::ptr owner, FoxThread* thr, const std::map<std::string, std::string>& conf)
    : thread_owner_(owner)
    , thread_(thr)
    , status_(UNCONNECTED)
    , event_(nullptr)
{
    type_ = IRedis::FOX_REDIS;
    auto tmp = get_value(conf, "host");
    auto pos = tmp.find(":");
    host_ = tmp.substr(0, pos);
    port_ = TypeUtil::Atoi(tmp.substr(pos + 1));
    passwd_ = get_value(conf, "passwd");
    ctx_count_ = 0;
    log_enable_ = TypeUtil::Atoi(get_value(conf, "log_enable", "1"));

    tmp = get_value(conf, "timeout_com");
    if (tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = TypeUtil::Atoi(tmp);

    cmd_timeout_.tv_sec = v / 1000;
    cmd_timeout_.tv_usec = v % 1000 * 1000;
}

void FoxRedis::OnAuthCb(redisAsyncContext* c, void* rp, void* priv)
{
    FoxRedis* fr = (FoxRedis*)priv;
    redisReply* r = (redisReply*)rp;
    if (!r) {
        ERROR(logger) << "auth error:(" << fr->host_ << ":" << fr->port_ << ", " << fr->name_ << ")";
        return;
    }
    if (r->type != REDIS_REPLY_STATUS) {
        ERROR(logger) << "auth reply type error:" << r->type << "(" << fr->host_ << ":" << fr->port_ << ", "
                      << fr->name_ << ")";
        return;
    }
    if (!r->str) {
        ERROR(logger) << "auth reply str error: NULL(" << fr->host_ << ":" << fr->port_ << ", " << fr->name_ << ")";
        return;
    }
    if (strcmp(r->str, "OK") == 0) {
        INFO(logger) << "auth ok: " << r->str << "(" << fr->host_ << ":" << fr->port_ << ", " << fr->name_ << ")";
    } else {
        ERROR(logger) << "auth error: " << r->str << "(" << fr->host_ << ":" << fr->port_ << ", " << fr->name_ << ")";
    }
}

void FoxRedis::ConnectCb(const redisAsyncContext* c, int status)
{
    FoxRedis* ar = static_cast<FoxRedis*>(c->data);
    if (!status) {
        INFO(logger) << "FoxRedis::ConnectCb " << c->c.tcp.host << ":" << c->c.tcp.port << " success";
        ar->status_ = CONNECTED;
        if (!ar->passwd_.empty()) {
            int rt = redisAsyncCommand(ar->context_.get(), FoxRedis::OnAuthCb, ar, "auth %s", ar->passwd_.c_str());
            if (rt) {
                ERROR(logger) << "FoxRedis Auth fail: " << rt;
            }
        }

    } else {
        ERROR(logger) << "FoxRedis::ConnectCb " << c->c.tcp.host << ":" << c->c.tcp.port << " fail, error:" << c->errstr;
        ar->status_ = UNCONNECTED;
    }
}

void FoxRedis::DisconnectCb(const redisAsyncContext* c, int status)
{
    INFO(logger) << "FoxRedis::DisconnectCb " << c->c.tcp.host << ":" << c->c.tcp.port << " status:" << status;
    FoxRedis* ar = static_cast<FoxRedis*>(c->data);
    ar->status_ = UNCONNECTED;
}

void FoxRedis::CmdCb(redisAsyncContext* ac, void* r, void* privdata)
{
    Ctx* ctx = static_cast<Ctx*>(privdata);
    if (!ctx) {
        return;
    }
    if (ctx->timeout) {
        delete ctx;
        // if(ctx && ctx->fiber) {
        //     ERROR(logger) << "redis cmd: '" << ctx->cmd << "' timeout("
        //                 << (ctx->rds->cmd_timeout_.tv_sec * 1000
        //                         + ctx->rds->cmd_timeout_.tv_usec / 1000)
        //                 << "ms)";
        //     ctx->scheduler->schedule(&ctx->fiber);
        //     ctx->cancelEvent();
        // }
        return;
    }

    auto log_enable_ = ctx->rds->log_enable_;

    redisReply* reply = (redisReply*)r;
    if (ac->err) {
        if (log_enable_) {
            StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
            ERROR(logger) << "redis cmd: '" << ctx->cmd << "' " << "(" << ac->err << ") " << ac->errstr;
        }
        if (ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if (!reply) {
        if (log_enable_) {
            StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
            ERROR(logger) << "redis cmd: '" << ctx->cmd << "' " << "reply: NULL";
        }
        if (ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if (reply->type == REDIS_REPLY_ERROR) {
        if (log_enable_) {
            StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
            ERROR(logger) << "redis cmd: '" << ctx->cmd << "' " << "reply: " << reply->str;
        }
        if (ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else {
        if (ctx->fctx->fiber) {
            ctx->fctx->rpy.reset(RedisReplyClone(reply), freeReplyObject);
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    }
    ctx->cancelEvent();
    delete ctx;
}

void FoxRedis::TimeCb(int fd, short event, void* d)
{
    FoxRedis* ar = static_cast<FoxRedis*>(d);
    redisAsyncCommand(ar->context_.get(), CmdCb, nullptr, "ping");
}

bool FoxRedis::init()
{
    if (thread_ == FoxThread::GetThis()) {
        return pinit();
    } else {
        thread_->dispatch(std::bind(&FoxRedis::pinit, this));
    }
    return true;
}

void FoxRedis::delayDelete(redisAsyncContext* c)
{
    // if(!c) {
    //     return;
    // }

    // Res* res = new Res();
    // res->ctx = c;
    // struct event* event = event_new(thread_->getBase(), -1, EV_TIMEOUT, DelayTimeCb, res);
    // res->event = event;
    //
    // struct timeval tv = {60, 0};
    // evtimer_add(event, &tv);
}

bool FoxRedis::pinit()
{
    // INFO(logger) << "pinit status_=" << status_;
    if (status_ != UNCONNECTED) {
        return true;
    }
    auto ctx = redisAsyncConnect(host_.c_str(), port_);
    if (!ctx) {
        ERROR(logger) << "redisAsyncConnect (" << host_ << ":" << port_ << ") null";
        return false;
    }
    if (ctx->err) {
        ERROR(logger) << "Error:(" << ctx->err << ")" << ctx->errstr;
        return false;
    }
    ctx->data = this;
    redisLibeventAttach(ctx, thread_->getBase());
    redisAsyncSetConnectCallback(ctx, ConnectCb);
    redisAsyncSetDisconnectCallback(ctx, DisconnectCb);
    status_ = CONNECTING;
    // context_.reset(ctx, redisAsyncFree);
    context_.reset(ctx, nop<redisAsyncContext>);
    // context_.reset(ctx, std::bind(&FoxRedis::delayDelete, this, std::placeholders::_1));
    if (event_ == nullptr) {
        event_ = event_new(thread_->getBase(), -1, EV_TIMEOUT | EV_PERSIST, TimeCb, this);
        struct timeval tv = {120, 0};
        evtimer_add(event_, &tv);
    }
    TimeCb(0, 0, this);
    return true;
}

ReplyPtr FoxRedis::cmd(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    auto r = cmd(fmt, ap);
    va_end(ap);
    return r;
}

ReplyPtr FoxRedis::cmd(const char* fmt, va_list ap)
{
    char* buf = nullptr;
    // int len = vasprintf(&buf, fmt, ap);
    int len = redisvFormatCommand(&buf, fmt, ap);
    if (len == -1) {
        ERROR(logger) << "redis fmt error: " << fmt;
        return nullptr;
    }
    // Ctx::ptr ctx(new Ctx(this));
    // if(buf) {
    //     ctx->cmd.append(buf, len);
    //     free(buf);
    // }
    // ctx->scheduler = Scheduler::GetThis();
    // ctx->fiber = Fiber::GetThis();
    // ctx->thread = thread_;

    FCtx fctx;
    fctx.cmd.append(buf, len);
    free(buf);
    fctx.scheduler = Scheduler::GetThis();
    fctx.fiber = Fiber::GetThis();

    thread_->dispatch(std::bind(&FoxRedis::pcmd, this, &fctx));
    Fiber::YieldToHold();
    return fctx.rpy;
}

ReplyPtr FoxRedis::cmd(const std::vector<std::string>& argv)
{
    // Ctx::ptr ctx(new Ctx(this));
    // ctx->parts = argv;
    FCtx fctx;
    do {
        std::vector<const char*> args;
        std::vector<size_t> args_len;
        for (auto& i : argv) {
            args.push_back(i.c_str());
            args_len.push_back(i.size());
        }
        char* buf = nullptr;
        int len = redisFormatCommandArgv(&buf, argv.size(), &(args[0]), &(args_len[0]));
        if (len == -1 || !buf) {
            ERROR(logger) << "redis fmt error";
            return nullptr;
        }
        fctx.cmd.append(buf, len);
        free(buf);
    } while (0);

    // ctx->scheduler = Scheduler::GetThis();
    // ctx->fiber = Fiber::GetThis();
    // ctx->thread = thread_;

    fctx.scheduler = Scheduler::GetThis();
    fctx.fiber = Fiber::GetThis();

    thread_->dispatch(std::bind(&FoxRedis::pcmd, this, &fctx));
    Fiber::YieldToHold();
    return fctx.rpy;
}

void FoxRedis::pcmd(FCtx* fctx)
{
    if (status_ == UNCONNECTED) {
        INFO(logger) << "redis (" << host_ << ":" << port_ << ") unconnected " << fctx->cmd;
        init();
        if (fctx->fiber) {
            fctx->scheduler->schedule(&fctx->fiber);
        }
        return;
    }
    Ctx* ctx(new Ctx(this));
    ctx->thread = thread_;
    ctx->init();
    ctx->fctx = fctx;
    ctx->cmd = fctx->cmd;

    if (!ctx->cmd.empty()) {
        // redisAsyncCommand(context_.get(), CmdCb, ctx.get(), ctx->cmd.c_str());
        redisAsyncFormattedCommand(context_.get(), CmdCb, ctx, ctx->cmd.c_str(), ctx->cmd.size());
        //} else if(!ctx->parts.empty()) {
        //    std::vector<const char*> argv;
        //    std::vector<size_t> argv_len;
        //    for(auto& i : ctx->parts) {
        //        argv.push_back(i.c_str());
        //        argv_len.push_back(i.size());
        //    }
        //    redisAsyncCommandArgv(context_.get(), CmdCb, ctx.get(), argv.size(),
        //            &(argv[0]), &(argv_len[0]));
    }
}

void FoxRedis::releaseOnThread()
{
    if (event_) {
        evtimer_del(event_);
        event_free(event_);
        event_ = nullptr;
    }
    // redisAsyncFree 会摘掉 libevent 事件、把未完成的命令回调补成 NULL 回调，
    // 必须在连接所属的 FoxThread 事件循环里执行；从其它线程调用会和正在派发的
    // 回调竞争，踩到 hiredis 的 redisProcessCallbacks 断言后直接 abort。
    if (context_) {
        redisAsyncFree(context_.get());
        context_.reset();
    }
}

FoxRedis::~FoxRedis()
{
    if (thread_ && FoxThread::GetThis() != thread_) {
        struct Token {
            std::promise<void> done;
            std::atomic<bool> abandoned{false};
        };
        auto token = std::make_shared<Token>();
        std::future<void> fut = token->done.get_future();
        bool dispatched = thread_->dispatch([this, token]() {
            if (!token->abandoned.load(std::memory_order_acquire)) {
                releaseOnThread();
            }
            token->done.set_value();
        });
        if (!dispatched) {
            // 派发失败说明事件循环已经不收任务了，这里同样不碰上下文：
            // 泄漏一个连接由进程退出回收，好过和残留的调度竞争
            token->abandoned.store(true, std::memory_order_release);
            WARN(logger) << "redis thread already stopped, skip releasing " << host_ << ":" << port_;
            return;
        }
        // 停服路径可以阻塞：等事件循环把上下文释放干净再继续
        if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
            return;
        }
        // 事件循环没响应（线程卡住），这里不能再碰上下文
        token->abandoned.store(true, std::memory_order_release);
        WARN(logger) << "redis thread did not release " << host_ << ":" << port_ << " in time, skip";
        return;
    }
    releaseOnThread();
}

FoxRedis::Ctx::Ctx(FoxRedis* r)
    : ev(nullptr)
    , timeout(false)
    , rds(r)
    , fctx(nullptr)
    , thread(nullptr)
{
    Atomic::addFetch(rds->ctx_count_, 1);
}

FoxRedis::Ctx::~Ctx()
{
    // cancelEvent();
    ASSERT(thread == FoxThread::GetThis());
    // ASSERT(destroy == 0);
    Atomic::subFetch(rds->ctx_count_, 1);
    //++destroy;
    // cancelEvent();
    if (ev) {
        evtimer_del(ev);
        event_free(ev);
        ev = nullptr;
    }
}

void FoxRedis::Ctx::cancelEvent()
{
    // if(ev) {
    //     if(thread == FoxThread::GetThis()) {
    //         evtimer_del(ev);
    //         event_free(ev);
    //     } else {
    //         auto e = ev;
    //         thread->dispatch([e](){
    //             evtimer_del(e);
    //             event_free(e);
    //         });
    //     }
    //     ev = nullptr;
    // }
    // ref = nullptr;
}

bool FoxRedis::Ctx::init()
{
    ev = evtimer_new(rds->thread_->getBase(), EventCb, this);
    evtimer_add(ev, &rds->cmd_timeout_);
    return true;
}

void FoxRedis::Ctx::EventCb(int fd, short event, void* d)
{
    Ctx* ctx = static_cast<Ctx*>(d);
    ctx->timeout = 1;
    if (ctx->rds->log_enable_) {
        StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
        INFO(logger) << "redis cmd: '" << ctx->cmd << "' reach timeout "
                     << (ctx->rds->cmd_timeout_.tv_sec * 1000 + ctx->rds->cmd_timeout_.tv_usec / 1000) << "ms";
    }
    if (ctx->fctx->fiber) {
        ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
    }
    ctx->cancelEvent();
    // ctx->ref = nullptr;
}

FoxRedisCluster::FoxRedisCluster(IFoxThread::ptr owner, FoxThread* thr, const std::map<std::string, std::string>& conf)
    : thread_owner_(owner)
    , thread_(thr)
    , status_(UNCONNECTED)
    , event_(nullptr)
{
    ctx_count_ = 0;

    type_ = IRedis::FOX_REDIS_CLUSTER;
    host_ = get_value(conf, "host");
    passwd_ = get_value(conf, "passwd");
    log_enable_ = TypeUtil::Atoi(get_value(conf, "log_enable", "1"));
    auto tmp = get_value(conf, "timeout_com");
    if (tmp.empty()) {
        tmp = get_value(conf, "timeout");
    }
    uint64_t v = TypeUtil::Atoi(tmp);

    cmd_timeout_.tv_sec = v / 1000;
    cmd_timeout_.tv_usec = v % 1000 * 1000;
}

void FoxRedisCluster::OnAuthCb(redisClusterAsyncContext* c, void* rp, void* priv)
{
    FoxRedisCluster* fr = (FoxRedisCluster*)priv;
    redisReply* r = (redisReply*)rp;
    if (!r) {
        ERROR(logger) << "auth error:(" << fr->host_ << ", " << fr->name_ << ")";
        return;
    }
    if (r->type != REDIS_REPLY_STATUS) {
        ERROR(logger) << "auth reply type error:" << r->type << "(" << fr->host_ << ", " << fr->name_ << ")";
        return;
    }
    if (!r->str) {
        ERROR(logger) << "auth reply str error: NULL(" << fr->host_ << ", " << fr->name_ << ")";
        return;
    }
    if (strcmp(r->str, "OK") == 0) {
        INFO(logger) << "auth ok: " << r->str << "(" << fr->host_ << ", " << fr->name_ << ")";
    } else {
        ERROR(logger) << "auth error: " << r->str << "(" << fr->host_ << ", " << fr->name_ << ")";
    }
}

void FoxRedisCluster::ConnectCb(const redisAsyncContext* c, int status)
{
    FoxRedisCluster* ar = static_cast<FoxRedisCluster*>(c->data);
    if (!status) {
        INFO(logger) << "FoxRedisCluster::ConnectCb " << c->c.tcp.host << ":" << c->c.tcp.port << " success";
        if (!ar->passwd_.empty()) {
            int rt = redisClusterAsyncCommand(
                ar->context_.get(), FoxRedisCluster::OnAuthCb, ar, "auth %s", ar->passwd_.c_str());
            if (rt) {
                ERROR(logger) << "FoxRedisCluster Auth fail: " << rt;
            }
        }
    } else {
        ERROR(logger) << "FoxRedisCluster::ConnectCb " << c->c.tcp.host << ":" << c->c.tcp.port << " fail, error:" << c->errstr;
    }
}

void FoxRedisCluster::DisconnectCb(const redisAsyncContext* c, int status)
{
    INFO(logger) << "FoxRedisCluster::DisconnectCb " << c->c.tcp.host << ":" << c->c.tcp.port << " status:" << status;
}

void FoxRedisCluster::CmdCb(redisClusterAsyncContext* ac, void* r, void* privdata)
{
    Ctx* ctx = static_cast<Ctx*>(privdata);
    if (ctx->timeout) {
        delete ctx;
        // if(ctx && ctx->fiber) {
        //     ERROR(logger) << "redis cmd: '" << ctx->cmd << "' timeout("
        //                 << (ctx->rds->cmd_timeout_.tv_sec * 1000
        //                         + ctx->rds->cmd_timeout_.tv_usec / 1000)
        //                 << "ms)";
        //     ctx->scheduler->schedule(&ctx->fiber);
        //     ctx->cancelEvent();
        // }
        return;
    }
    auto log_enable_ = ctx->rds->log_enable_;
    // ctx->cancelEvent();
    redisReply* reply = (redisReply*)r;
    //++ctx->callback_count;
    if (ac->err) {
        if (log_enable_) {
            StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
            ERROR(logger) << "redis cmd: '" << ctx->cmd << "' "
                          << "(" << ac->err << ") " << ac->errstr;
        }
        if (ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if (!reply) {
        if (log_enable_) {
            StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
            ERROR(logger) << "redis cmd: '" << ctx->cmd << "' "
                          << "reply: NULL";
        }
        if (ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else if (reply->type == REDIS_REPLY_ERROR) {
        if (log_enable_) {
            StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
            ERROR(logger) << "redis cmd: '" << ctx->cmd << "' "
                          << "reply: " << reply->str;
        }
        if (ctx->fctx->fiber) {
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    } else {
        if (ctx->fctx->fiber) {
            ctx->fctx->rpy.reset(RedisReplyClone(reply), freeReplyObject);
            ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
        }
    }
    // ctx->ref = nullptr;
    delete ctx;
    // ctx->tref = nullptr;
}

void FoxRedisCluster::TimeCb(int fd, short event, void* d)
{
    // FoxRedisCluster* ar = static_cast<FoxRedisCluster*>(d);
    // redisAsyncCommand(ar->context_.get(), CmdCb, nullptr, "ping");
}

bool FoxRedisCluster::init()
{
    if (thread_ == FoxThread::GetThis()) {
        return pinit();
    } else {
        thread_->dispatch(std::bind(&FoxRedisCluster::pinit, this));
    }
    return true;
}

void FoxRedisCluster::delayDelete(redisAsyncContext* c)
{
    // if(!c) {
    //     return;
    // }

    // Res* res = new Res();
    // res->ctx = c;
    // struct event* event = event_new(thread_->getBase(), -1, EV_TIMEOUT, DelayTimeCb, res);
    // res->event = event;
    //
    // struct timeval tv = {60, 0};
    // evtimer_add(event, &tv);
}

bool FoxRedisCluster::pinit()
{
    if (status_ != UNCONNECTED) {
        return true;
    }
    INFO(logger) << "FoxRedisCluster pinit:" << host_;
    auto ctx = redisClusterAsyncConnect(host_.c_str(), 0);
    if (!ctx) {
        ERROR(logger) << "redisClusterAsyncConnect (" << host_ << ") null";
        return false;
    }
    ctx->data = this;
    redisClusterLibeventAttach(ctx, thread_->getBase());
    redisClusterAsyncSetConnectCallback(ctx, ConnectCb);
    redisClusterAsyncSetDisconnectCallback(ctx, DisconnectCb);
    if (ctx->err) {
        ERROR(logger) << "Error:(" << ctx->err << ")" << ctx->errstr << " passwd=" << passwd_;
        return false;
    }
    status_ = CONNECTED;
    // context_.reset(ctx, redisAsyncFree);
    context_.reset(ctx, nop<redisClusterAsyncContext>);
    // context_.reset(ctx, std::bind(&FoxRedisCluster::delayDelete, this, std::placeholders::_1));
    if (event_ == nullptr) {
        event_ = event_new(thread_->getBase(), -1, EV_TIMEOUT | EV_PERSIST, TimeCb, this);
        struct timeval tv = {120, 0};
        evtimer_add(event_, &tv);
        TimeCb(0, 0, this);
    }
    return true;
}

ReplyPtr FoxRedisCluster::cmd(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    auto r = cmd(fmt, ap);
    va_end(ap);
    return r;
}

ReplyPtr FoxRedisCluster::cmd(const char* fmt, va_list ap)
{
    char* buf = nullptr;
    // int len = vasprintf(&buf, fmt, ap);
    int len = redisvFormatCommand(&buf, fmt, ap);
    if (len == -1 || !buf) {
        ERROR(logger) << "redis fmt error: " << fmt;
        return nullptr;
    }
    FCtx fctx;
    fctx.cmd.append(buf, len);
    free(buf);
    fctx.scheduler = Scheduler::GetThis();
    fctx.fiber = Fiber::GetThis();
    // Ctx::ptr ctx(new Ctx(this));
    // if(buf) {
    //     ctx->cmd.append(buf, len);
    //     free(buf);
    // }
    // ctx->scheduler = Scheduler::GetThis();
    // ctx->fiber = Fiber::GetThis();
    // ctx->thread = thread_;

    thread_->dispatch(std::bind(&FoxRedisCluster::pcmd, this, &fctx));
    Fiber::YieldToHold();
    return fctx.rpy;
}

ReplyPtr FoxRedisCluster::cmd(const std::vector<std::string>& argv)
{
    // Ctx::ptr ctx(new Ctx(this));
    // ctx->parts = argv;
    FCtx fctx;
    do {
        std::vector<const char*> args;
        std::vector<size_t> args_len;
        for (auto& i : argv) {
            args.push_back(i.c_str());
            args_len.push_back(i.size());
        }
        char* buf = nullptr;
        int len = redisFormatCommandArgv(&buf, argv.size(), &(args[0]), &(args_len[0]));
        if (len == -1 || !buf) {
            ERROR(logger) << "redis fmt error";
            return nullptr;
        }
        fctx.cmd.append(buf, len);
        free(buf);
    } while (0);

    fctx.scheduler = Scheduler::GetThis();
    fctx.fiber = Fiber::GetThis();

    thread_->dispatch(std::bind(&FoxRedisCluster::pcmd, this, &fctx));
    Fiber::YieldToHold();
    return fctx.rpy;
}

void FoxRedisCluster::pcmd(FCtx* fctx)
{
    if (status_ != CONNECTED) {
        INFO(logger) << "redis (" << host_ << ") unconnected " << fctx->cmd;
        init();
        if (fctx->fiber) {
            fctx->scheduler->schedule(&fctx->fiber);
        }
        return;
    }
    Ctx* ctx(new Ctx(this));
    ctx->thread = thread_;
    ctx->init();
    ctx->fctx = fctx;
    ctx->cmd = fctx->cmd;
    // ctx->ref = ctx;
    // ctx->tref = ctx;
    if (!ctx->cmd.empty()) {
        // redisClusterAsyncCommand(context_.get(), CmdCb, ctx.get(), ctx->cmd.c_str());
        redisClusterAsyncFormattedCommand(context_.get(), CmdCb, ctx, &ctx->cmd[0], ctx->cmd.size());
        //} else if(!ctx->parts.empty()) {
        //    std::vector<const char*> argv;
        //    std::vector<size_t> argv_len;
        //    for(auto& i : ctx->parts) {
        //        argv.push_back(i.c_str());
        //        argv_len.push_back(i.size());
        //    }
        //    redisClusterAsyncCommandArgv(context_.get(), CmdCb, ctx.get(), argv.size(),
        //            &(argv[0]), &(argv_len[0]));
    }
}

FoxRedisCluster::~FoxRedisCluster()
{
    if (event_) {
        evtimer_del(event_);
        event_free(event_);
    }
}

FoxRedisCluster::Ctx::Ctx(FoxRedisCluster* r)
    : ev(nullptr)
    , timeout(false)
    , rds(r)
    //,scheduler(nullptr)
    , thread(nullptr)
{
    //,cancel_count(0)
    //,destroy(0)
    //,callback_count(0) {
    fctx = nullptr;
    Atomic::addFetch(rds->ctx_count_, 1);
}

FoxRedisCluster::Ctx::~Ctx()
{
    ASSERT(thread == FoxThread::GetThis());
    // ASSERT(destroy == 0);
    Atomic::subFetch(rds->ctx_count_, 1);
    //++destroy;
    // cancelEvent();

    if (ev) {
        evtimer_del(ev);
        event_free(ev);
        ev = nullptr;
    }
}

void FoxRedisCluster::Ctx::cancelEvent()
{
    // INFO(logger) << "cancelEvent " << FoxThread::GetThis()
    //            << " - " << thread
    //            << " - " << IOManager::GetThis()
    //            << " - " << cancel_count;
    // if(thread != FoxThread::GetThis()) {
    //     INFO(logger) << "cancelEvent " << FoxThread::GetThis()
    //                << " - " << thread
    //                << " - " << IOManager::GetThis()
    //                << " - " << cancel_count;

    //    //INFO(logger) << "cancelEvent thread=" << thread << " " << thread->getId()
    //    //           << " this=" << FoxThread::GetThis();
    //    //ASSERT(thread == FoxThread::GetThis());
    //}
    // ASSERT(!IOManager::GetThis());
    ////if(Atomic::addFetch(cancel_count) > 1) {
    ////    return;
    ////}
    ////ASSERT(!Fiber::GetThis());
    ////std::unique_lock lock(mutex);
    // if(++cancel_count > 1) {
    //     return;
    // }
    // if(ev) {
    //     auto e = ev;
    //     ev = nullptr;
    //     //lock.unlock();
    //     //evtimer_del(e);
    //     //event_free(e);
    //     if(thread == FoxThread::GetThis()) {
    //         evtimer_del(e);
    //         event_free(e);
    //     } else {
    //         thread->dispatch([e](){
    //             evtimer_del(e);
    //             event_free(e);
    //         });
    //     }
    // }
    // ref = nullptr;
}

bool FoxRedisCluster::Ctx::init()
{
    ASSERT(thread == FoxThread::GetThis());
    ev = evtimer_new(rds->thread_->getBase(), EventCb, this);
    evtimer_add(ev, &rds->cmd_timeout_);
    return true;
}

void FoxRedisCluster::Ctx::EventCb(int fd, short event, void* d)
{
    Ctx* ctx = static_cast<Ctx*>(d);
    if (!ctx->ev) {
        return;
    }
    ctx->timeout = 1;
    if (ctx->rds->log_enable_) {
        StringUtil::Replace(ctx->cmd, "\r\n", "\\r\\n");
        INFO(logger) << "redis cmd: '" << ctx->cmd << "' reach timeout "
                     << (ctx->rds->cmd_timeout_.tv_sec * 1000 + ctx->rds->cmd_timeout_.tv_usec / 1000) << "ms";
    }
    ctx->cancelEvent();
    if (ctx->fctx->fiber) {
        ctx->fctx->scheduler->schedule(&ctx->fctx->fiber);
    }
    // ctx->ref = nullptr;
    // delete ctx;
    // ctx->tref = nullptr;
}

IRedis::ptr RedisManager::get(const std::string& name)
{
    std::unique_lock lock(mutex_);
    auto it = datas_.find(name);
    if (it == datas_.end()) {
        return nullptr;
    }
    if (it->second.empty()) {
        return nullptr;
    }
    auto r = it->second.front();
    it->second.pop_front();
    if (r->getType() == IRedis::FOX_REDIS || r->getType() == IRedis::FOX_REDIS_CLUSTER) {
        it->second.push_back(r);
        return std::shared_ptr<IRedis>(r, nop<IRedis>);
    }
    lock.unlock();
    auto rr = dynamic_cast<ISyncRedis*>(r);
    if ((time(0) - rr->getLastActiveTime()) > 30) {
        if (!rr->cmd("ping")) {
            if (!rr->reconnect()) {
                std::unique_lock lock1(mutex_);
                datas_[name].push_back(r);
                return nullptr;
            }
        }
    }
    rr->setLastActiveTime(time(0));
    return std::shared_ptr<IRedis>(r, std::bind(&RedisManager::freeRedis, this, std::placeholders::_1));
}

void RedisManager::freeRedis(IRedis* r)
{
    if (!r) {
        ERROR(logger) << "RedisManager::freeRedis r is null";
        return;
    }
    std::unique_lock lock(mutex_);
    datas_[r->getName()].push_back(r);
}

void RedisManager::freeAll()
{
    {
        std::unique_lock lock(mutex_);
        for (auto& [name, list] : datas_) {
            for (auto* r : list) {
                delete r;
            }
            list.clear();
        }
        datas_.clear();
    }
    // 释放后允许重新 init
    std::unique_lock init_lock(init_mutex_);
    inited_ = false;
}

void RedisManager::init()
{
    std::unique_lock init_lock(init_mutex_);
    if (inited_) {
        INFO(logger) << "RedisManager already initialized, skip";
        return;
    }

    config_ = g_redis->getValue();
    size_t done = 0;
    size_t total = 0;
    for (auto& i : config_) {
        auto type = get_value(i.second, "type");
        auto pool = TypeUtil::Atoi(get_value(i.second, "pool"));
        auto passwd = get_value(i.second, "passwd");
        total += pool;
        for (int n = 0; n < pool; ++n) {
            if (type == "redis") {
                Redis* rds(new Redis(i.second));
                rds->connect();
                rds->setLastActiveTime(time(0));
                std::unique_lock lock(mutex_);
                datas_[i.first].push_back(rds);
                Atomic::addFetch(done, 1);
            } else if (type == "redis_cluster") {
                RedisCluster* rds(new RedisCluster(i.second));
                rds->connect();
                rds->setLastActiveTime(time(0));
                std::unique_lock lock(mutex_);
                datas_[i.first].push_back(rds);
                Atomic::addFetch(done, 1);
            } else if (type == "fox_redis") {
                auto conf = i.second;
                auto name = i.first;
                auto owner = FoxThreadMgr::GetInstance()->get("redis");
                FoxThreadMgr::GetInstance()->dispatch("redis", [this, conf, name, owner, &done]() {
                    FoxRedis* rds(new FoxRedis(owner, FoxThread::GetThis(), conf));
                    rds->init();
                    rds->setName(name);

                    std::unique_lock lock(mutex_);
                    datas_[name].push_back(rds);
                    Atomic::addFetch(done, 1);
                });
            } else if (type == "fox_redis_cluster") {
                auto conf = i.second;
                auto name = i.first;
                auto owner = FoxThreadMgr::GetInstance()->get("redis");
                FoxThreadMgr::GetInstance()->dispatch("redis", [this, conf, name, owner, &done]() {
                    FoxRedisCluster* rds(new FoxRedisCluster(owner, FoxThread::GetThis(), conf));
                    rds->init();
                    rds->setName(name);

                    std::unique_lock lock(mutex_);
                    datas_[name].push_back(rds);
                    Atomic::addFetch(done, 1);
                });
            } else {
                Atomic::addFetch(done, 1);
            }
        }
    }

    while (done != total) {
        usleep(5000);
    }

    inited_ = true;
}

std::ostream& RedisManager::dump(std::ostream& os)
{
    os << "[RedisManager total=" << config_.size() << "]" << std::endl;
    for (auto& i : config_) {
        os << "    " << i.first << " :[";
        for (auto& n : i.second) {
            os << "{" << n.first << ":" << n.second << "}";
        }
        os << "]" << std::endl;
    }
    return os;
}

ReplyPtr RedisUtil::Cmd(const std::string& name, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ReplyPtr rt = Cmd(name, fmt, ap);
    va_end(ap);
    return rt;
}

ReplyPtr RedisUtil::Cmd(const std::string& name, const char* fmt, va_list ap)
{
    auto rds = RedisMgr::GetInstance()->get(name);
    if (!rds) {
        return nullptr;
    }
    return rds->cmd(fmt, ap);
}

ReplyPtr RedisUtil::Cmd(const std::string& name, const std::vector<std::string>& args)
{
    auto rds = RedisMgr::GetInstance()->get(name);
    if (!rds) {
        return nullptr;
    }
    return rds->cmd(args);
}

ReplyPtr RedisUtil::TryCmd(const std::string& name, uint32_t count, const char* fmt, ...)
{
    for (uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, fmt);
        ReplyPtr rt = Cmd(name, fmt, ap);
        va_end(ap);

        if (rt) {
            return rt;
        }
    }
    return nullptr;
}

ReplyPtr RedisUtil::TryCmd(const std::string& name, uint32_t count, const std::vector<std::string>& args)
{
    for (uint32_t i = 0; i < count; ++i) {
        ReplyPtr rt = Cmd(name, args);
        if (rt) {
            return rt;
        }
    }
    return nullptr;
}

} // namespace chen
