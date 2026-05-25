#pragma once

#include "FreeAccess/FixedString.hpp"

#include <meta>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>

namespace FreeAccess
{
namespace Details
{

template<typename T, FixedString Name>
constexpr static auto SearchInMembers()
{
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^T, std::meta::access_context::unchecked())
    );

    return std::define_static_array(
        std::meta::members_of(^^T, std::meta::access_context::unchecked())
         | std::views::filter([](auto member){ 
            return std::meta::has_identifier(member) && 
                   std::meta::identifier_of(member) == Name.View();
        }));
}

}

template<typename T, FixedString Name>
class PrivateBinder
{
    using RawType = std::remove_reference_t<T>;
    static constexpr inline auto memberInfos_ = [](){
        if (auto result = Details::SearchInMembers<RawType, Name>(); !result.empty())
            return result;

        // std::string and string_view are directly concatenatable since C++26.
        throw std::runtime_error{ std::string{ "Unknown member: " } + Name.View() };
    }();

public:
    constexpr static auto Get() { return memberInfos_[0]; }
    constexpr static auto GetAll() { return memberInfos_; }
};

namespace Details
{
template<typename T, FixedString Name>
concept ReferredAsStaticData = requires(PrivateBinder<T, Name> phony)
{
    requires is_static_member(phony.Get()) && !is_function(phony.Get()) 
             && !is_template(phony.Get());
};

template<typename T, FixedString Name>
concept ReferredAsNonstaticData = requires(PrivateBinder<T, Name> phony)
{
    requires is_nonstatic_data_member(phony.Get());
};

template<typename T, FixedString Name>
concept ReferredAsFunction = requires(PrivateBinder<T, Name> phony)
{
    requires is_function(phony.Get()) || is_function_template(phony.Get()); 
};

template<typename T, FixedString Name>
concept ReferredAsType = requires(PrivateBinder<T, Name> phony)
{
    requires is_type(phony.Get()) || is_type_alias(phony.Get());
};

template<typename T, FixedString Name>
concept ReferredAsTemplateType = requires(PrivateBinder<T, Name> phony)
{
    requires is_class_template(phony.Get());
};

template <typename Derived, std::meta::info Func, typename... Args>
class Overload
{
    using OriginalType_ = [:template_arguments_of(^^Derived)[0]:];

public:
    decltype(auto) Call(Args... args)
    requires requires(OriginalType_ obj)
    {
        (std::forward<OriginalType_>(obj).[:Func:])(std::forward<Args>(args)...);
    }
    {
        auto ptr = ((Derived*)this)->[:PrivateBinder<Derived, "ptr_">::Get():];
        return (std::forward<OriginalType_>(*ptr).[:Func:])(std::forward<Args>(args)...);
    }
};

template <typename Derived, std::meta::info Func>
class TemplateOverload
{
    using OriginalType_ = [:template_arguments_of(^^Derived)[0]:];

public:
    template<typename... Args>
    decltype(auto) Call(Args... args)
    requires requires(OriginalType_ obj)
    {
        (std::forward<OriginalType_>(obj).template [:Func:])(std::forward<Args>(args)...);
    }
    {
        auto ptr = ((Derived*)this)->[:PrivateBinder<Derived, "ptr_">::Get():];
        return (std::forward<OriginalType_>(*ptr).template [:Func:])(std::forward<Args>(args)...);
    }
};

template <typename... OverloadTypes>
class Overloads : public OverloadTypes...
{
public:
    using OverloadTypes::Call...;
};

template<typename Binder, typename FirstType, 
         template <typename... OverloadTypes> typename OverloadsType,
         template <typename, std::meta::info, typename...> typename OverloadType, 
         template <typename, std::meta::info> typename TemplateOverloadType>
consteval auto MakeOverloads()
{
    std::vector<std::meta::info> overloads;
    for (auto info : Binder::GetAll())
    {
        std::vector realParams{ ^^FirstType, reflect_constant(info) };
        if (is_function_template(info))
        {
            overloads.push_back(substitute(^^TemplateOverloadType, realParams));
            continue;
        }
        // is_function.
        auto params = parameters_of(info);
        auto it = params.begin();
        while (it != params.end() && !has_default_argument(*it))
        {
            realParams.push_back(type_of(*it));
            ++it;
        }
        overloads.push_back(substitute(^^OverloadType, realParams));
        while (it != params.end())
        {
            realParams.push_back(type_of(*it++));
            overloads.push_back(substitute(^^OverloadType, realParams));
        }
    }
    
    return substitute(^^OverloadsType, overloads);
}

template<typename Accessor>
consteval auto MakeOverloads()
{
    using Binder = [:substitute(^^PrivateBinder, template_arguments_of(^^Accessor)):];
    return MakeOverloads<Binder, Accessor, Overloads, Overload, TemplateOverload>();
}

template<typename T, FixedString Name>
class PrivateAccessorFuncBase
    : public [:Details::MakeOverloads<PrivateAccessorFuncBase<T, Name>>():]
{
    using RawType = std::remove_reference_t<T>;

    RawType* ptr_ = nullptr;
    using Super_ = [:Details::MakeOverloads<PrivateAccessorFuncBase<T, Name>>():];
    
public:
    using Binder = PrivateBinder<RawType, Name>;

    PrivateAccessorFuncBase() = default;
    PrivateAccessorFuncBase(RawType* ptr) : ptr_{ ptr } { }

    using Super_::Call;
};

}

template<typename T, FixedString Name>
class PrivateAccessor;

template<typename T, FixedString Name>
requires Details::ReferredAsNonstaticData<T, Name>
class PrivateAccessor<T, Name>
{
    T* ptr_ = nullptr;

public:
    using Binder = PrivateBinder<T, Name>;

    PrivateAccessor() = default;
    PrivateAccessor(T& obj) : ptr_{ &obj } { }
    auto& Get() { return ptr_->[:Binder::Get():]; }
    auto* TryGet() { return ptr_ == nullptr ? nullptr : &Get(); }
};

template<typename T, FixedString Name>
requires Details::ReferredAsNonstaticData<T, Name> && std::is_rvalue_reference_v<T>
class PrivateAccessor<T, Name>
{
    using RawType = std::remove_reference_t<T>;
    using Binder = PrivateBinder<RawType, Name>;
    RawType* ptr_ = nullptr;

public:
    PrivateAccessor() = default;
    PrivateAccessor(T obj) : ptr_{ &obj } { }
    auto&& Get() { return std::move(ptr_->[:Binder::Get():]); }
    auto* TryGet() { return ptr_ == nullptr ? nullptr : &Get(); }
};

template<typename T, FixedString Name>
requires Details::ReferredAsStaticData<T, Name>
class PrivateAccessor<T, Name>
{
public:
    using Binder = PrivateBinder<T, Name>;

    PrivateAccessor() = default;
    PrivateAccessor(T& obj) {}
    PrivateAccessor(T&& obj) {}

    static auto& Get() { return [:Binder::Get():]; }
    static auto* TryGet() { return &Get(); }
};

template<typename T, FixedString Name>
requires Details::ReferredAsType<T, Name>
class PrivateAccessor<T, Name>
{
public:
    using Binder = PrivateBinder<T, Name>;
    using Type = [:Binder::Get():];
};

template<typename T, FixedString Name>
requires Details::ReferredAsFunction<T, Name>
class PrivateAccessor<T, Name> : private Details::PrivateAccessorFuncBase<T, Name>
{
    using Super_ = Details::PrivateAccessorFuncBase<T, Name>;
    
public:
    using Binder = Super_::Binder;
    using Super_::Call;

    PrivateAccessor() = default;
    PrivateAccessor(T& obj) : Super_{ &obj } { }
};

template<typename T, FixedString Name>
requires Details::ReferredAsFunction<T, Name> && std::is_rvalue_reference_v<T>
class PrivateAccessor<T, Name> : private Details::PrivateAccessorFuncBase<T, Name>
{
    using Super_ = Details::PrivateAccessorFuncBase<T, Name>;
    
public:
    using Binder = Super_::Binder;
    using Super_::Call;

    PrivateAccessor() = default;
    PrivateAccessor(T obj) : Super_{ &obj } { }
};

template<typename T, FixedString Name>
requires Details::ReferredAsTemplateType<T, Name>
class PrivateAccessor<T, Name>
{
public:
    using Binder = PrivateBinder<T, Name>;
    static inline constexpr auto TemplateReflection = Binder::Get();
};

// Helpers
template<FixedString Name, typename T>
auto MakePrivateAccessor(T&& obj)
{
    if constexpr (std::is_lvalue_reference_v<T>)
        return PrivateAccessor<std::remove_reference_t<T>, Name>{ obj };
    else
        return PrivateAccessor<T&&, Name>{ std::move(obj) };
}

template<typename T, FixedString Name>
using PrivateAccessorT = PrivateAccessor<T, Name>::Type;

template<typename T, FixedString Name, std::meta::info... Infos>
using PrivateAccessorTemplateT = [:substitute(PrivateAccessor<T, Name>::TemplateReflection, 
                                              std::vector{ Infos... }):];
} // namespace FreeAccess