#ifndef BMMQ_REG_BASE_HPP
#define BMMQ_REG_BASE_HPP

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../machine/RegisterId.hpp"

namespace BMMQ {

template<typename T>
struct CPU_Register {
    T value {};

    virtual ~CPU_Register() = default;
    virtual T operator()() const;
    virtual bool isPair() const noexcept
    {
        return false;
    }
    virtual std::unique_ptr<CPU_Register<T>> clone() const
    {
        return std::make_unique<CPU_Register<T>>(*this);
    }
};

template<typename T>
struct CPU_RegisterPair : public CPU_Register<T> {
    T operator()() const override;
    bool isPair() const noexcept override
    {
        return true;
    }
    std::unique_ptr<CPU_Register<T>> clone() const override
    {
        return std::make_unique<CPU_RegisterPair<T>>(*this);
    }
};

template<typename T>
struct RegisterEntry {
    std::string name;
    RegisterDescriptor descriptor;
    std::unique_ptr<CPU_Register<T>> reg;
};

template<typename T>
class RegisterFile {
public:
    RegisterFile() = default;
    RegisterFile(const RegisterFile& other);
    RegisterFile& operator=(const RegisterFile& other);
    RegisterFile(RegisterFile&&) noexcept = default;
    RegisterFile& operator=(RegisterFile&&) noexcept = default;
    ~RegisterFile() = default;

    const std::deque<RegisterEntry<T>>& entries() const noexcept;
    bool hasRegister(std::string_view id) const;
    RegisterEntry<T>* findRegister(std::string_view id);
    const RegisterEntry<T>* findRegister(std::string_view id) const;
    bool hasDescriptor(std::string_view id) const;
    RegisterDescriptor* findDescriptor(std::string_view id);
    const RegisterDescriptor* findDescriptor(std::string_view id) const;
    RegisterDescriptor& registerDescriptor(RegisterDescriptor descriptor);
    RegisterEntry<T>& addRegister(const RegisterDescriptor& descriptor);
    RegisterEntry<T>& addRegister(std::string_view id, bool isPair = false);
    RegisterEntry<T>& findOrInsert(const RegisterDescriptor& descriptor);
    RegisterEntry<T>& findOrInsert(std::string_view id, bool isPair = false);

private:
    std::deque<RegisterEntry<T>> file;
    std::deque<RegisterDescriptor> descriptors_;
};

template<typename T>
class RegisterInfo {
public:
    explicit RegisterInfo(std::string_view id) noexcept
        : name_(id) {}

    explicit RegisterInfo(RegisterId id) noexcept
        : name_(id.value) {}

    RegisterInfo(RegisterFile<T>& file, std::string_view id);
    RegisterInfo(RegisterFile<T>& file, RegisterId id);

    void registration(RegisterFile<T>& file, std::string_view id);
    void registration(RegisterFile<T>& file, RegisterId id);
    CPU_Register<T>* operator()() const;
    CPU_Register<T>* operator->() const;
    std::string_view getRegisterName() const noexcept;

private:
    std::string name_;
    CPU_Register<T>* reg_ = nullptr;
};

} // namespace BMMQ

#include "templ/reg_base.impl.hpp"
#include "templ/RegisterFile.impl.hpp"
#include "templ/RegisterInfo.impl.hpp"
#endif // __REG_BASE
