/**
 * @file EMail.hpp
 * @brief 邮件消息封装
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace chen {

class EMailEntity {
public:
    typedef std::shared_ptr<EMailEntity> ptr;
    static EMailEntity::ptr CreateAttach(const std::string& filename);

    void addHeader(const std::string& key, const std::string& val);
    std::string getHeader(const std::string& key, const std::string& def = "");

    const std::string& getContent() const { return content_; }
    void setContent(const std::string& v) { content_ = v; }

    std::string toString() const;

private:
    std::map<std::string, std::string> headers_;
    std::string content_;
};

class EMail {
public:
    typedef std::shared_ptr<EMail> ptr;
    static EMail::ptr Create(
        const std::string& from_address,
        const std::string& from_passwd,
        const std::string& title,
        const std::string& body,
        const std::vector<std::string>& to_address,
        const std::vector<std::string>& cc_address = {},
        const std::vector<std::string>& bcc_address = {});

    const std::string& getFromEMailAddress() const { return from_email_address_; }
    const std::string& getFromEMailPasswd() const { return from_email_passwd_; }
    const std::string& getTitle() const { return title_; }
    const std::string& getBody() const { return body_; }

    void setFromEMailAddress(const std::string& v) { from_email_address_ = v; }
    void setFromEMailPasswd(const std::string& v) { from_email_passwd_ = v; }
    void setTitle(const std::string& v) { title_ = v; }
    void setBody(const std::string& v) { body_ = v; }

    const std::vector<std::string>& getToEMailAddress() const { return to_email_address_; }
    const std::vector<std::string>& getCcEMailAddress() const { return cc_email_address_; }
    const std::vector<std::string>& getBccEMailAddress() const { return bcc_email_address_; }

    void setToEMailAddress(const std::vector<std::string>& v) { to_email_address_ = v; }
    void setCcEMailAddress(const std::vector<std::string>& v) { cc_email_address_ = v; }
    void setBccEMailAddress(const std::vector<std::string>& v) { bcc_email_address_ = v; }

    void addEntity(EMailEntity::ptr entity);
    const std::vector<EMailEntity::ptr>& getEntitys() const { return entitys_; }

private:
    std::string from_email_address_;
    std::string from_email_passwd_;
    std::string title_;
    std::string body_;
    std::vector<std::string> to_email_address_;
    std::vector<std::string> cc_email_address_;
    std::vector<std::string> bcc_email_address_;
    std::vector<EMailEntity::ptr> entitys_;
};

} // namespace chen
