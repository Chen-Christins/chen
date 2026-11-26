/**
 * @file TestConfigXml.cpp
 * @brief 配置模块 XML 兼容性单元测试
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/Config/Config.hpp"

#include <tinyxml2.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

using chen::Config;

namespace {

chen::ConfigVar<int>::ptr g_port = Config::Lookup("xml_unit.port", 0, "xml unit port");
chen::ConfigVar<std::string>::ptr g_name = Config::Lookup("xml_unit.name", std::string(), "xml unit name");
chen::ConfigVar<bool>::ptr g_enable = Config::Lookup("xml_unit.enable", false, "xml unit enable");
chen::ConfigVar<std::vector<int>>::ptr g_vec = Config::Lookup("xml_unit.vec", std::vector<int>(), "xml unit vec");
chen::ConfigVar<std::list<int>>::ptr g_list = Config::Lookup("xml_unit.list", std::list<int>(), "xml unit list");
chen::ConfigVar<std::set<int>>::ptr g_set = Config::Lookup("xml_unit.set", std::set<int>(), "xml unit set");
chen::ConfigVar<std::map<std::string, int>>::ptr g_map =
    Config::Lookup("xml_unit.map", std::map<std::string, int>(), "xml unit map");
chen::ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_nested =
    Config::Lookup("xml_unit.nested", std::map<std::string, std::map<std::string, std::string>>(), "xml unit nested");
chen::ConfigVar<std::vector<std::map<std::string, std::string>>>::ptr g_servers =
    Config::Lookup("xml_unit.servers", std::vector<std::map<std::string, std::string>>(), "xml unit servers");
chen::ConfigVar<std::vector<std::string>>::ptr g_tokens =
    Config::Lookup("xml_unit.tokens", std::vector<std::string>(), "xml unit single item list");

const char* kXml = R"(<config>
    <xml_unit>
        <port>8080</port>
        <name>hello</name>
        <enable>true</enable>
        <vec>
            <item>1</item>
            <item>2</item>
            <item>3</item>
        </vec>
        <list>
            <item>4</item>
            <item>5</item>
        </list>
        <set>
            <item>7</item>
            <item>8</item>
        </set>
        <map>
            <a>10</a>
            <b>20</b>
        </map>
        <nested>
            <blog>
                <host>127.0.0.1</host>
                <port>6379</port>
            </blog>
        </nested>
        <servers>
            <server>
                <id>1</id>
                <type>http</type>
            </server>
            <server>
                <id>2</id>
                <type>rpc</type>
            </server>
        </servers>
        <tokens>
            <item>only-one</item>
        </tokens>
    </xml_unit>
</config>)";

} // namespace

CHEN_TEST(config_xml, load_from_xml_all_types)
{
    tinyxml2::XMLDocument doc;
    CHEN_CHECK(doc.Parse(kXml) == tinyxml2::XML_SUCCESS);
    CHEN_CHECK(doc.RootElement() != nullptr);
    Config::LoadFromXml(*doc.RootElement());

    // 标量
    CHEN_CHECK_EQ(g_port->getValue(), 8080);
    CHEN_CHECK_EQ(g_name->getValue(), std::string("hello"));
    CHEN_CHECK(g_enable->getValue());

    // 序列
    CHEN_CHECK(g_vec->getValue() == (std::vector<int>{1, 2, 3}));
    CHEN_CHECK(g_list->getValue() == (std::list<int>{4, 5}));
    CHEN_CHECK(g_set->getValue() == (std::set<int>{7, 8}));

    // map
    auto m = g_map->getValue();
    CHEN_CHECK_EQ(m.size(), static_cast<size_t>(2));
    CHEN_CHECK_EQ(m["a"], 10);
    CHEN_CHECK_EQ(m["b"], 20);

    // 嵌套 map
    auto nested = g_nested->getValue();
    CHEN_CHECK_EQ(nested.size(), static_cast<size_t>(1));
    CHEN_CHECK_EQ(nested["blog"]["host"], std::string("127.0.0.1"));
    CHEN_CHECK_EQ(nested["blog"]["port"], std::string("6379"));

    // 对象序列（重复同名标签）
    auto servers = g_servers->getValue();
    CHEN_CHECK_EQ(servers.size(), static_cast<size_t>(2));
    CHEN_CHECK_EQ(servers[0]["type"], std::string("http"));
    CHEN_CHECK_EQ(servers[1]["id"], std::string("2"));

    // 单元素序列（保留 item 标记）
    auto tokens = g_tokens->getValue();
    CHEN_CHECK_EQ(tokens.size(), static_cast<size_t>(1));
    CHEN_CHECK_EQ(tokens[0], std::string("only-one"));
}

CHEN_TEST(config_xml, load_from_conf_dir)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("chen_unit_xml_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    {
        std::ofstream ofs(dir / "unit.xml");
        CHEN_CHECK(static_cast<bool>(ofs));
        ofs << R"(<config><xml_unit><port>9090</port><name>from-dir</name></xml_unit></config>)";
    }

    Config::LoadFromConfDir(dir.string());
    CHEN_CHECK_EQ(g_port->getValue(), 9090);
    CHEN_CHECK_EQ(g_name->getValue(), std::string("from-dir"));

    fs::remove_all(dir);
}

CHEN_TEST(config_xml, unknown_keys_are_ignored)
{
    tinyxml2::XMLDocument doc;
    CHEN_CHECK(doc.Parse(R"(<config><xml_unit><port>1234</port><not_registered>1</not_registered></xml_unit></config>)")
        == tinyxml2::XML_SUCCESS);
    Config::LoadFromXml(*doc.RootElement());
    CHEN_CHECK_EQ(g_port->getValue(), 1234);
}

CHEN_TEST_MAIN()
