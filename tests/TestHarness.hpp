/**
 * @file TestHarness.hpp
 * @brief 极简测试框架：测试注册、断言、过滤与结果统计
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details 头文件实现，无第三方依赖。现有 tests/ 下的 .cc 保持原样，新测试统一用它：
 *
 *          CHEN_TEST(bytearray, fixed_int_roundtrip)
 *          {
 *              CHEN_CHECK_EQ(ba.readFint32(), 42);
 *          }
 *
 *          CHEN_TEST_MAIN()
 *
 *          运行参数：
 *            --list            列出所有用例（suite.name）
 *            --filter=<substr> 只跑名字包含 substr 的用例（也可用环境变量 CHEN_TEST_FILTER）
 *            --verbose / -v    当前无额外输出，保留给后续扩展
 *            --help / -h       帮助
 *
 *          断言失败抛 chen::test::Failure，由 RunAll 捕获并标记该用例失败；
 *          单个用例失败不影响其他用例执行。
 */
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace chen::test {

/**
 * @brief 断言失败异常
 * @details 只在测试进程内使用；框架代码不应依赖它
 */
class Failure : public std::exception {
public:
    explicit Failure(std::string message)
        : message_(std::move(message))
    {
    }

    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

using TestFn = void (*)();

/**
 * @brief 注册的测试用例
 */
struct TestCase {
    const char* suite;
    const char* name;
    TestFn fn;
};

/**
 * @brief 全局用例表
 * @details 函数内 static，保证跨编译单元唯一（inline 函数 + 局部 static 的 C++ 保证）
 */
inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

/**
 * @brief 静态注册器：CHEN_TEST 展开时构造，把用例塞进 Registry
 */
class Registrar {
public:
    Registrar(const char* suite, const char* name, TestFn fn) { Registry().push_back(TestCase{suite, name, fn}); }
};

/**
 * @brief 尽力把断言值转成字符串；不可流式输出的类型退化为占位符
 */
template <class T>
std::string ToString(const T& value)
{
    if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    } else {
        return "<unprintable>";
    }
}

inline std::string Describe(const char* file, int line, const std::string& detail)
{
    std::ostringstream oss;
    oss << file << ':' << line << ": " << detail;
    return oss.str();
}

[[noreturn]] inline void Fail(const char* file, int line, const std::string& detail)
{
    throw Failure(Describe(file, line, detail));
}

template <class A, class B>
void CheckEq(const A& lhs, const B& rhs, const char* lhs_expr, const char* rhs_expr, const char* file, int line)
{
    if (!(lhs == rhs)) {
        Fail(file, line,
            std::string(lhs_expr) + " == " + rhs_expr + " (left=" + ToString(lhs) + ", right=" + ToString(rhs) + ")");
    }
}

template <class A, class B>
void CheckNe(const A& lhs, const B& rhs, const char* lhs_expr, const char* rhs_expr, const char* file, int line)
{
    if (!(lhs != rhs)) {
        Fail(file, line, std::string(lhs_expr) + " != " + rhs_expr + " (both=" + ToString(lhs) + ")");
    }
}

/**
 * @brief 运行入口
 * @param argc/argv main 原样透传
 * @return 0 全部通过；1 有用例失败
 */
inline int RunAll(int argc, char** argv)
{
    std::string filter;
    bool list_only = false;
    bool verbose = false; ///< 预留，目前仅影响是否打印耗时以外的信息

    if (const char* env = std::getenv("CHEN_TEST_FILTER"); env != nullptr) {
        filter = env;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            list_only = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: %s [--list] [--filter=<substr>] [--verbose]\n", argv[0]);
            return 0;
        } else if (arg.rfind("--filter=", 0) == 0) {
            filter = arg.substr(9);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            std::printf("usage: %s [--list] [--filter=<substr>] [--verbose]\n", argv[0]);
            return 2;
        }
    }

    auto& tests = Registry();

    if (list_only) {
        for (const auto& test : tests) {
            std::printf("%s.%s\n", test.suite, test.name);
        }
        return 0;
    }

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const auto& test : tests) {
        const std::string full = std::string(test.suite) + "." + test.name;
        if (!filter.empty() && full.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }

        std::printf("[ RUN      ] %s\n", full.c_str());
        std::fflush(stdout);

        const auto start = std::chrono::steady_clock::now();
        try {
            test.fn();
            const auto cost =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            ++passed;
            std::printf("[       OK ] %s (%lld ms)\n", full.c_str(), static_cast<long long>(cost));
        } catch (const Failure& e) {
            ++failed;
            std::printf("[  FAILED  ] %s\n             %s\n", full.c_str(), e.what());
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[  FAILED  ] %s\n             unexpected std::exception: %s\n", full.c_str(), e.what());
        } catch (...) {
            ++failed;
            std::printf("[  FAILED  ] %s\n             unexpected unknown exception\n", full.c_str());
        }
        std::fflush(stdout);
    }

    if (verbose) {
        std::printf("\n[==========] filter='%s'\n", filter.c_str());
    }
    std::printf("\n[==========] %zu tests: %d passed, %d failed, %d skipped\n", tests.size(), passed, failed, skipped);

    return failed == 0 ? 0 : 1;
}

} // namespace chen::test

/**
 * @brief 定义一个测试用例
 * @param suite 用例分组（标识符）
 * @param name  用例名（标识符）
 */
#define CHEN_TEST(suite, name)                                                                                         \
    static void suite##_##name##_chen_test_body();                                                                     \
    static ::chen::test::Registrar suite##_##name##_chen_test_registrar(                                               \
        #suite, #name, &suite##_##name##_chen_test_body);                                                              \
    static void suite##_##name##_chen_test_body()

/**
 * @brief 无条件断言
 */
#define CHEN_CHECK(condition)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ::chen::test::Fail(__FILE__, __LINE__, "CHEN_CHECK failed: " #condition);                                  \
        }                                                                                                              \
    } while (0)

/**
 * @brief 相等断言，失败时打印两侧取值
 * @note 宏参数按顶层逗号切分：`LexicalCast<std::string, int>()("42")` 这类含模板逗号的表达式
 *       必须先用类型别名，或用额外括号包起来写成 `(LexicalCast<std::string, int>()("42"))`，
 *       否则预处理器会把它拆成多个参数。
 */
#define CHEN_CHECK_EQ(lhs, rhs)                                                                                        \
    do {                                                                                                               \
        auto&& chen_test_lhs = (lhs);                                                                                  \
        auto&& chen_test_rhs = (rhs);                                                                                  \
        ::chen::test::CheckEq(chen_test_lhs, chen_test_rhs, #lhs, #rhs, __FILE__, __LINE__);                           \
    } while (0)

/**
 * @brief 不等断言
 */
#define CHEN_CHECK_NE(lhs, rhs)                                                                                        \
    do {                                                                                                               \
        auto&& chen_test_lhs = (lhs);                                                                                  \
        auto&& chen_test_rhs = (rhs);                                                                                  \
        ::chen::test::CheckNe(chen_test_lhs, chen_test_rhs, #lhs, #rhs, __FILE__, __LINE__);                           \
    } while (0)

/**
 * @brief 大小比较断言，op 传 < / <= / > / >=
 */
#define CHEN_CHECK_CMP(lhs, rhs, op)                                                                                   \
    do {                                                                                                               \
        auto&& chen_test_lhs = (lhs);                                                                                  \
        auto&& chen_test_rhs = (rhs);                                                                                  \
        if (!(chen_test_lhs op chen_test_rhs)) {                                                                       \
            ::chen::test::Fail(__FILE__, __LINE__,                                                                     \
                std::string(#lhs " " #op " " #rhs " (left=") + ::chen::test::ToString(chen_test_lhs)                   \
                    + ", right=" + ::chen::test::ToString(chen_test_rhs) + ")");                                       \
        }                                                                                                              \
    } while (0)

/**
 * @brief 期望抛出指定异常
 */
#define CHEN_CHECK_THROW(expr, exception_type)                                                                         \
    do {                                                                                                               \
        bool chen_test_thrown = false;                                                                                 \
        try {                                                                                                          \
            (void)(expr);                                                                                              \
        } catch (const exception_type&) {                                                                              \
            chen_test_thrown = true;                                                                                   \
        } catch (...) {                                                                                                \
            ::chen::test::Fail(__FILE__, __LINE__, "expected " #exception_type " but another exception was thrown");   \
        }                                                                                                              \
        if (!chen_test_thrown) {                                                                                       \
            ::chen::test::Fail(__FILE__, __LINE__, "expected " #exception_type " to be thrown by: " #expr);            \
        }                                                                                                              \
    } while (0)

/**
 * @brief 期望不抛异常
 */
#define CHEN_CHECK_NO_THROW(expr)                                                                                      \
    do {                                                                                                               \
        try {                                                                                                          \
            (void)(expr);                                                                                              \
        } catch (const std::exception& e) {                                                                            \
            ::chen::test::Fail(__FILE__, __LINE__, "unexpected exception: " + std::string(e.what()));                  \
        } catch (...) {                                                                                                \
            ::chen::test::Fail(__FILE__, __LINE__, "unexpected unknown exception");                                    \
        }                                                                                                              \
    } while (0)

/**
 * @brief 浮点近似断言
 */
#define CHEN_CHECK_NEAR(lhs, rhs, eps)                                                                                 \
    do {                                                                                                               \
        auto&& chen_test_lhs = (lhs);                                                                                  \
        auto&& chen_test_rhs = (rhs);                                                                                  \
        const double chen_test_diff = static_cast<double>(chen_test_lhs) - static_cast<double>(chen_test_rhs);         \
        if (chen_test_diff > (eps) || chen_test_diff < -(eps)) {                                                       \
            ::chen::test::Fail(__FILE__, __LINE__,                                                                     \
                std::string(#lhs " ~= " #rhs " (left=") + ::chen::test::ToString(chen_test_lhs)                        \
                    + ", right=" + ::chen::test::ToString(chen_test_rhs) + ")");                                       \
        }                                                                                                              \
    } while (0)

/**
 * @brief 生成测试程序入口
 */
#define CHEN_TEST_MAIN()                                                                                               \
    int main(int argc, char** argv)                                                                                    \
    {                                                                                                                  \
        return ::chen::test::RunAll(argc, argv);                                                                       \
    }
