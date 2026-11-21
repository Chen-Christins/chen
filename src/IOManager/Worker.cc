#include "Worker.hpp"

#include "../Config/Config.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_worker_config =
    Config::Lookup("workers", std::map<std::string, std::map<std::string, std::string>>(), "woker config");

WorkerGroup::WorkerGroup(uint32_t batch_size, Scheduler* s)
    : batch_size_(batch_size)
    , finish_(false)
    , scheduler_(s)
    , sem_(batch_size)
{
}

WorkerGroup::~WorkerGroup()
{
    waitAll();
}

void WorkerGroup::schedule(std::function<void()> cb, int thread)
{
    sem_.wait();
    scheduler_->schedule(std::bind(&WorkerGroup::doWork, shared_from_this(), cb), thread);
}

void WorkerGroup::waitAll()
{
    if (!finish_) {
        finish_ = true;
        for (uint32_t i = 0; i < batch_size_; ++i) {
            sem_.wait();
        }
    }
}

void WorkerGroup::doWork(std::function<void()> cb)
{
    cb();
    sem_.notify();
}

WorkerManager::WorkerManager()
    : stop_(false)
{
}

void WorkerManager::add(Scheduler::ptr s)
{
    datas_[s->getName()].push_back(s);
}

Scheduler::ptr WorkerManager::get(const std::string& name)
{
    auto it = datas_.find(name);
    if (it == datas_.end()) {
        return nullptr;
    }
    if (it->second.size() == 1) {
        return it->second[0];
    }
    return it->second[rand() % it->second.size()];
}

IOManager::ptr WorkerManager::getAsIOManager(const std::string& name)
{
    return std::dynamic_pointer_cast<IOManager>(get(name));
}

bool WorkerManager::init(const std::map<std::string, std::map<std::string, std::string>>& v)
{
    for (auto& [name, p] : v) {
        int32_t thread_num = GetParamValue(p, "thread_num", 1);
        int32_t worker_num = GetParamValue(p, "worker_num", 1);

        for (int32_t x = 0; x < worker_num; ++x) {
            Scheduler::ptr s;
            if (!x) {
                s = std::make_shared<IOManager>(thread_num, false, name);
            } else {
                s = std::make_shared<IOManager>(thread_num, false, name + "-" + std::to_string(x));
            }
            add(s);
        }
    }
    stop_ = datas_.empty();
    return true;
}

bool WorkerManager::init()
{
    auto workers = g_worker_config->getValue();
    return init(workers);
}

void WorkerManager::stop()
{
    if (stop_) {
        return;
    }
    for (auto& i : datas_) {
        for (auto& x : i.second) {
            // 取消 worker 上的残留定时器（如 do_io 的条件超时器），
            // 确保 IOManager::stopping() 中 getNextTimer() 返回 ~0ull
            auto iom = std::dynamic_pointer_cast<IOManager>(x);
            if (iom) {
                iom->cancelAllTimers();
            }
            x->schedule([]() {
            });
            x->stop();
        }
    }
    datas_.clear();
    stop_ = true;
}

std::ostream& WorkerManager::dump(std::ostream& os)
{
    for (auto& i : datas_) {
        for (auto& x : i.second) {
            x->dump(os) << std::endl;
        }
    }
    return os;
}

uint32_t WorkerManager::getCount()
{
    return datas_.size();
}

} // namespace chen
