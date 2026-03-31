#ifndef BMMQ_REG_BASE_HPP
#define BMMQ_REG_BASE_HPP

#include <deque>
#include <memory>
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
    RegisterId id = RegisterId::AF;
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
    bool hasRegister(RegisterId id) const;
    bool hasRegister(std::string_view id) const;
    RegisterEntry<T>* findRegister(RegisterId id);
    const RegisterEntry<T>* findRegister(RegisterId id) const;
    RegisterEntry<T>* findRegister(std::string_view id);
    const RegisterEntry<T>* findRegister(std::string_view id) const;
    RegisterEntry<T>& addRegister(RegisterId id, bool isPair = false);
    RegisterEntry<T>& addRegister(std::string_view id, bool isPair = false);
    RegisterEntry<T>& findOrInsert(RegisterId id, bool isPair = false);
    RegisterEntry<T>& findOrInsert(std::string_view id, bool isPair = false);

private:
    std::deque<RegisterEntry<T>> file;
};

template<typename T>
class RegisterInfo {
public:
    explicit RegisterInfo(RegisterId id) noexcept
        : id_(id) {}

    RegisterInfo(RegisterFile<T>& file, RegisterId id);
    RegisterInfo(RegisterFile<T>& file, std::string_view id);

    void registration(RegisterFile<T>& file, RegisterId id);
    void registration(RegisterFile<T>& file, std::string_view id);
    CPU_Register<T>* operator()() const;
    CPU_Register<T>* operator->() const;
    RegisterId getRegisterID() const noexcept;

private:
    RegisterId id_;
    CPU_Register<T>* reg_ = nullptr;
};

} // namespace BMMQ

#include "templ/reg_base.impl.hpp"
#include "templ/RegisterFile.impl.hpp"
#include "templ/RegisterInfo.impl.hpp"
#endif // __REG_BASE
