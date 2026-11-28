/**
 * @file TestConfig.cpp
 * @brief Config 类型转换、注册表、监听器与 YAML 加载单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/Config/Config.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

using chen::Config;
using chen::LexicalCast;

// 断言宏按顶层逗号切分参数，含模板逗号的转换器先固化成别名，避免 LexicalCast<A, B> 被拆开
using StringToInt = LexicalCast<std::string, int>;
using IntToString = LexicalCast<int, std::string>;
using StringToBool = LexicalCast<std::string, bool>;
using BoolToString = LexicalCast<bool, std::string>;
using VecIntToString = LexicalCast<std::vector<int>, std::string>;
using StringToVecInt = LexicalCast<std::string, std::vector<int>>;
using MapToString = LexicalCast<std::map<std::string, int>, std::string>;
using StringToMap = LexicalCast<std::string, std::map<std::string, int>>;

CHEN_TEST(config, lexical_cast_scalar)
{
    CHEN_CHECK_EQ(StringToInt()("42"), 42);
    CHEN_CHECK_EQ(IntToString()(42), std::string("42"));

    CHEN_CHECK_EQ(StringToBool()("true"), true);
    CHEN_CHECK_EQ(StringToBool()("1"), true);
    CHEN_CHECK_EQ(StringToBool()("false"), false);
    CHEN_CHECK_EQ(StringToBool()("0"), false);
    CHEN_CHECK_EQ(BoolToString()(true), std::string("true"));
    CHEN_CHECK_EQ(BoolToString()(false), std::string("false"));

    CHEN_CHECK_THROW(StringToBool()("maybe"), std::invalid_argument);
    CHEN_CHECK_THROW(StringToInt()("not-a-number"), std::exception);
}

CHEN_TEST(config, lexical_cast_container)
{
    const std::vector<int> vec = {1, 2, 3};
    const std::string vec_text = VecIntToString()(vec);
    CHEN_CHECK_EQ(StringToVecInt()(vec_text), vec);

    const std::map<std::string, int> map_value = {{"a", 1}, {"b", 2}};
    const std::string map_text = MapToString()(map_value);
    CHEN_CHECK_EQ(StringToMap()(map_text), map_value);
}

CHEN_TEST(config, lookup_is_idempotent)
{
    auto first = Config::Lookup<int>("unit_test.config.port", 8080, "unit test port");
    auto second = Config::Lookup<int>("unit_test.config.port", 1, "another description");

    CHEN_CHECK(first != nullptr);
    CHEN_CHECK(first == second);
    CHEN_CHECK_EQ(second->getValue(), 8080);
}

CHEN_TEST(config, set_value_notifies_listener)
{
    auto var = Config::Lookup<int>("unit_test.config.listener", 10, "unit test listener");
    var->setValue(10); ///< 归位，避免用例重复执行时受旧值影响

    int old_value = 0;
    int new_value = 0;
    int calls = 0;
    const uint64_t listener_id = var->addListener([&](const int& old_v, const int& new_v) {
        old_value = old_v;
        new_value = new_v;
        ++calls;
    });

    var->setValue(20);
    CHEN_CHECK_EQ(calls, 1);
    CHEN_CHECK_EQ(old_value, 10);
    CHEN_CHECK_EQ(new_value, 20);

    // 值没变化不触发回调
    var->setValue(20);
    CHEN_CHECK_EQ(calls, 1);

    var->delListener(listener_id);
    var->setValue(30);
    CHEN_CHECK_EQ(calls, 1);
    CHEN_CHECK_EQ(var->getValue(), 30);
}

CHEN_TEST(config, load_from_yaml_updates_var)
{
    auto var = Config::Lookup<int>("unit_test.config.yaml_value", 1, "unit test yaml");
    var->setValue(1);

    YAML::Node root = YAML::Load("unit_test:\n  config:\n    yaml_value: 99\n");
    Config::LoadFromYaml(root);
    CHEN_CHECK_EQ(var->getValue(), 99);
}

CHEN_TEST(config, load_from_yaml_ignores_unknown)
{
    YAML::Node root = YAML::Load("unit_test:\n  config:\n    not_registered_key: 1\n");
    CHEN_CHECK_NO_THROW(Config::LoadFromYaml(root));
}

CHEN_TEST(config, invalid_value_keeps_old)
{
    auto var = Config::Lookup<int>("unit_test.config.invalid", 5, "unit test invalid");
    var->setValue(5);

    CHEN_CHECK(!var->fromString("not-a-number"));
    CHEN_CHECK_EQ(var->getValue(), 5);

    CHEN_CHECK(var->fromString("6"));
    CHEN_CHECK_EQ(var->getValue(), 6);
}

CHEN_TEST(config, to_string_roundtrip)
{
    auto var = Config::Lookup<int>("unit_test.config.to_string", 0, "unit test to string");
    var->setValue(123);

    CHEN_CHECK_EQ(var->toString(), std::string("123"));
    CHEN_CHECK(var->fromString(var->toString()));
    CHEN_CHECK_EQ(var->getValue(), 123);
}

CHEN_TEST_MAIN()
