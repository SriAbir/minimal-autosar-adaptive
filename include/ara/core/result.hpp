#pragma once
#include <variant>
#include <string>
#include <system_error>
#include <string_view>
#include <vector>

namespace ara::core {

// Minimal error domain for persistency
enum class PersistencyErrc {
    kSuccess = 0,
    kNotFound,
    kQuotaExceeded,
    kCorruption,
    kPermissionDenied,
    kUnknown
};

class ErrorCode {
public:
    PersistencyErrc value;
    ErrorCode(PersistencyErrc v) : value(v) {}
    operator bool() const { return value != PersistencyErrc::kSuccess; }
};

template<typename T>
class Result {
    std::variant<T, ErrorCode> data_;
public:
    Result(const T& v) : data_(v) {}
    Result(T&& v) : data_(std::move(v)) {}
    Result(ErrorCode e) : data_(e) {}
    bool HasValue() const { return std::holds_alternative<T>(data_); }
    T& Value() { return std::get<T>(data_); }
    const T& Value() const { return std::get<T>(data_); }
    ErrorCode Error() const { return std::get<ErrorCode>(data_); }

    //Helpers for easier testing
    explicit operator bool() const { return HasValue(); }

    //Helper so that * result gives value
    T& operator*() { return Value(); }
    const T& operator*() const { return Value(); }
    // T&& operator*() const { return std::move(Value()); } //Removing for now
};

using String = std::string;
using StringView = std::string_view;
template<typename T> using Vector = std::vector<T>;


template<>
class Result<void> {
    bool ok_;
    ErrorCode err_;
public:
    Result() : ok_(true), err_(PersistencyErrc::kSuccess) {}
    Result(ErrorCode e) : ok_(false), err_(e) {}

    bool HasValue() const { return ok_; }
    void Value() const {}  // no-op
    ErrorCode Error() const { return err_; }

    explicit operator bool() const { return ok_; }
};

} // namespace ara::core
