#include "chen/config/config.h"
#include "chen/log/log.h"
#include <iostream>
// #include "chen/gateway/http/config.h"

// static chen::ConfigVar<chen::httpGateway::HttpGatewayConfig>::ptr g_gateway_config =
//     chen::Config::Lookup("system.gateway", chen::httpGateway::HttpGatewayConfig(), "system gateway config");

#if 1

static chen::Logger::ptr logger = LOG_ROOT();

chen::ConfigVar<int>::ptr int_value_config = 
    chen::Config::Lookup("system.port", (int)8080, "system port");

chen::ConfigVar<float>::ptr float_value_config = 
    chen::Config::Lookup("system.value", (float)10.2f, "system float");

chen::ConfigVar<std::vector<int>>::ptr vec_value_config = 
    chen::Config::Lookup("system.vec", std::vector<int> {1, 2, 3}, "system int vec");

chen::ConfigVar<std::list<int>>::ptr list_value_config = 
    chen::Config::Lookup("system.list", std::list<int> {4, 5, 6}, "system int list");

chen::ConfigVar<std::set<int>>::ptr set_value_config = 
    chen::Config::Lookup("system.set", std::set<int> {91, 51, 61}, "system int set");

chen::ConfigVar<std::unordered_set<int>>::ptr uset_value_config = 
    chen::Config::Lookup("system.uset", std::unordered_set<int> {91, 101, 61}, "system int uset");

chen::ConfigVar<std::map<std::string, int>>::ptr map_value_config = 
    chen::Config::Lookup("system.map", std::map<std::string, int> {{"5", 9}, {"1", 3}, {"4", 5}}, "system map");

chen::ConfigVar<std::unordered_map<std::string, int>>::ptr umap_value_config =
    chen::Config::Lookup("system.umap", std::unordered_map<std::string, int> {{"4", 3}, {"1", 5}, {"5", 2}}, "system umap");

void test_config() {
    INFO(logger) << "before: " << int_value_config->getValue();
    INFO(logger) << "before: " << float_value_config->toString();

#define XX(var, name, prefix) \
    { \
        auto& v = var->getValue(); \
        for (auto& i : v) { \
            INFO(logger) << #prefix " " #name ": " << i; \
        } \
        INFO(logger) << #prefix " " #name " yaml: " << var->toString(); \
    }

#define XX_M(var, name, prefix) \
    { \
        auto& v = var->getValue(); \
        for (auto& [x, y] : v) { \
            INFO(logger) << #prefix " " #name ": {" << x << " - " << y << "}"; \
        } \
        INFO(logger) << #prefix " " #name " yaml: " << var->toString(); \
    }

    XX(vec_value_config, int_vec, before);
    XX(list_value_config, int_list, before);
    XX(set_value_config, int_set, before);
    XX(uset_value_config, int_uset, before);
    XX_M(map_value_config, int_map, begin);
    XX_M(umap_value_config, int_umap, begin);

    YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/log.yml");
    chen::Config::LoadFromYaml(root);

    XX(vec_value_config, int_vec, after);
    XX(list_value_config, int_list, after);
    XX(set_value_config, int_set, after);
    XX(uset_value_config, int_uset, after);
    XX_M(map_value_config, int_map, after);
    XX_M(umap_value_config, int_umap, after);

    INFO(logger) << "after: " << int_value_config->getValue();
    INFO(logger) << "after: " << float_value_config->toString();
}

#endif

void print_yaml(const YAML::Node& node, int level) {
    if (node.IsScalar()) {
        INFO(logger) << std::string(level * 4, ' ')
            << node.Scalar() << " - " << node.Type() << " - " << level;
    } else if (node.IsNull()) {
        INFO(logger) << std::string(level * 4, ' ')
            << "NULL - " << node.Type() << " - " << level;
    } else if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            INFO(logger) << std::string(level * 4, ' ')
                << it->first << " - " << it->second.Type() << " - " << level;
            print_yaml(it->second, level + 1);
        }
    } else if (node.IsSequence()) {
        for (size_t i = 0; i < node.size(); ++i) {
            INFO(logger) << std::string(level * 4, ' ')
                << i << " - " << node[i].Type() << " - " << level;
            print_yaml(node[i], level);
        }
    }
}

void test_yaml() {
    YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/log.yml");
    print_yaml(root, 0);
}

class Person {
public:
    Person() {}
    std::string m_name;
    int m_age = 0;
    bool m_sex = 0;

    std::string toString() const {
        std::stringstream ss;
        ss << "[Person name=" << m_name
           << " age=" << m_age
           << " sex=" << m_sex
           << "]";
        return ss.str();
    }

    bool operator==(const Person& oth) const {
        return m_name == oth.m_name
            && m_age == oth.m_age
            && m_sex == oth.m_sex;
    }
};

namespace chen {
/* 全特化 */
template <>
class LexicalCast<std::string, Person> {
public:
    Person operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        Person p;
        p.m_name = node["name"].as<std::string>();
        p.m_age = node["age"].as<int>();
        p.m_sex = node["sex"].as<bool>();
        return p;
    }
};

template <>
class LexicalCast<Person, std::string> {
public:
    std::string operator()(const Person& v) {
        YAML::Node node;
        node["name"] = v.m_name;
        node["age"] = v.m_age;
        node["sex"] = v.m_sex;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

}

chen::ConfigVar<Person>::ptr person = 
    chen::Config::Lookup("class.person", Person(), "system person");

chen::ConfigVar<std::map<std::string, Person>>::ptr map_person = 
    chen::Config::Lookup("class.map", std::map<std::string, Person>(), "system person");

chen::ConfigVar<std::map<std::string, std::vector<Person>>>::ptr map_vec_person =
    chen::Config::Lookup("class.vec_map", std::map<std::string, std::vector<Person>>(), "system person");

void test_class() {
    INFO(logger) << "before: " << person->getValue().toString() << " - " << person->toString();
#define XX_PM(var, prefix) \
    { \
        auto m = var->getValue(); \
        for (auto& [x, y] : m) { \
            INFO(logger) << prefix << ": " << x << " - " << y.toString(); \
        } \
        INFO(logger) << prefix << ": size=" << m.size(); \
    }
    XX_PM(map_person, "class.map before");
    INFO(logger) << "before: " << map_vec_person->toString();

    YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/log.yml");
    chen::Config::LoadFromYaml(root);
    // print_yaml(root, 0);

    INFO(logger) << "after: " << person->getValue().toString() << " - " << person->toString();
    XX_PM(map_person, "class.map after");
    INFO(logger) << "after: " << map_vec_person->toString();
}

void test_log() {
    static chen::Logger::ptr system_log = LOG_NAME("system");
    INFO(system_log) << "hello system" << std::endl;
    std::cout << chen::LoggerMgr::GetInstance()->toYamlString() << std::endl;
    YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/log.yml");
    chen::Config::LoadFromYaml(root);
    std::cout << "==============" << std::endl;
    std::cout << chen::LoggerMgr::GetInstance()->toYamlString() << std::endl;
    std::cout << "==============" << std::endl;
    std::cout << root << std::endl;
    INFO(system_log) << "hello system" << std::endl;

    system_log->setFormatter("%d - %m%n");
    INFO(system_log) << "hello system" << std::endl;
}

void test_loadconf() {
    chen::Config::LoadFromConfDir("conf");
}

int main(int argc, char** argv) {
    // test_config();
    // test_yaml();
    // test_class();
    // test_log();
    // chen::EnvMgr::GetInstance()->init(argc, argv);
    // test_loadconf();

    // INFO(logger) << "before load - http gateway config: " << g_gateway_config->toString();
    // YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/gateway.yml");
    // chen::Config::LoadFromYaml(root);
    // INFO(logger) << "after load - http gateway config:\n" << chen::LexicalCast<chen::httpGateway::HttpGatewayConfig, std::string>()(g_gateway_config->getValue());

    return 0;
}