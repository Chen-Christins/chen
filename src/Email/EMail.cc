#include "EMail.hpp"

#include <fstream>
#include <sstream>

#include "../Util/FSUtil.hpp"
#include "../Util/StringUtil.hpp"

namespace chen {

EMailEntity::ptr EMailEntity::CreateAttach(const std::string& filename)
{
    std::ifstream ifs(filename, std::ios::binary);
    std::string buf;
    buf.resize(1024);
    EMailEntity::ptr entity(new EMailEntity);
    while (!ifs.eof()) {
        ifs.read(&buf[0], buf.size());
        entity->content_.append(buf.c_str(), ifs.gcount());
    }
    entity->content_ = StringUtil::Base64Encode(entity->content_);
    entity->addHeader("Content-Transfer-Encoding", "base64");
    entity->addHeader("Content-Disposition", "attachment");
    entity->addHeader("Content-Type", "application/octet-stream;name=" + FSUtil::Basename(filename));
    return entity;
}

void EMailEntity::addHeader(const std::string& key, const std::string& val)
{
    headers_[key] = val;
}

std::string EMailEntity::getHeader(const std::string& key, const std::string& def)
{
    auto it = headers_.find(key);
    return it == headers_.end() ? def : it->second;
}

std::string EMailEntity::toString() const
{
    std::stringstream ss;
    for (auto& i : headers_) {
        ss << i.first << ": " << i.second << "\r\n";
    }
    ss << content_ << "\r\n";
    return ss.str();
}

EMail::ptr EMail::Create(
    const std::string& from_address,
    const std::string& from_passwd,
    const std::string& title,
    const std::string& body,
    const std::vector<std::string>& to_address,
    const std::vector<std::string>& cc_address,
    const std::vector<std::string>& bcc_address)
{
    EMail::ptr email(new EMail);
    email->setFromEMailAddress(from_address);
    email->setFromEMailPasswd(from_passwd);
    email->setTitle(title);
    email->setBody(body);
    email->setToEMailAddress(to_address);
    email->setCcEMailAddress(cc_address);
    email->setBccEMailAddress(bcc_address);
    return email;
}

void EMail::addEntity(EMailEntity::ptr entity)
{
    entitys_.push_back(entity);
}

} // namespace chen
