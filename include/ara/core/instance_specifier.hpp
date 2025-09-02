
#pragma once
#include <string>
#include <string_view>

namespace ara::core {

class InstanceSpecifier {
    std::string id_;
public:
    explicit InstanceSpecifier(const std::string& id) : id_(id) {}
    const std::string& ToString() const { return id_; }
};

} // namespace ara::core
