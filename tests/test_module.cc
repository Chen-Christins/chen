#include "chen/module.h"
#include "chen/log/log.h"
#include "chen/db/redis.h"
#include <iostream>

static chen::Logger::ptr logger = LOG_ROOT();

class A {
public:
	A() {
		std::cout << "A::A() ===== " << this << std::endl;
	}

	~A() {
		std::cout << "A::~A() ====== " << this << std::endl;
	}
};

class MyModule : public chen::Module {
public:
	MyModule() : chen::Module("hello", "1.0", "") {}

	bool onLoad() override {
		chen::Singleton<A>::GetInstance();
		std::cout << " ---------- onLoad ---------- " << std::endl;
		return true;
	}

	bool onUnload() override {
		chen::Singleton<A>::GetInstance();
		std::cout << " ---------- onUnload ---------- " << std::endl;
		return true;
	}

	virtual bool onServerReady() override {
		auto rpy = chen::RedisUtil::Cmd("local", "get abc");
		if (!rpy) {
			ERROR(logger) << "redis cmd get abc error";
		} else {
			ERROR(logger) << "redis get abc:" << (rpy->str ? rpy->str : "null");
		}
		return true;
	}
};

extern "C" {

chen::Module* CreateModule() {
    chen::Singleton<A>::GetInstance();
    std::cout << "=============CreateModule=================" << std::endl;
    return new MyModule;
}

void DestroyModule(chen::Module* ptr) {
    std::cout << "=============DestroyModule=================" << std::endl;
    delete ptr;
}

}