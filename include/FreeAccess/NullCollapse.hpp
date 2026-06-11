#pragma once
#include "FreeAccess/AccessTable.hpp"
#include <optional>

namespace FreeAccess
{
template<typename T>
struct UnwrapPointerTraits
{
    static inline constexpr bool NeedInplace = true;
    static T* Unwrap(T& obj) { return std::addressof(obj); }
};

template<typename T>
struct UnwrapPointerTraits<T*>
{
    static T* Unwrap(T* ptr) { return ptr; }
};

template<typename T>
using UnwrapPointerTraitsT = [:return_type_of(^^UnwrapPointerTraits<T>::Unwrap):];

template<typename T, std::meta::access_context AccessContext>
class NullCollapse;

namespace Details
{
template<typename From, std::size_t ByteOffset, bool UseOptional, typename T>
inline auto SafeProxyGetFrom(T* self)
{
    if constexpr (UseOptional)
    {
        auto& buffer = *(std::optional<From>*)((char*)(self) - ByteOffset);
        return buffer ? std::addressof(*buffer) : nullptr;
    }
    else
    {
        return *(From**)((char*)(self) - ByteOffset);
    }
}

template<typename From, auto Member, std::meta::access_context AccessContext,
         std::size_t ByteOffset, bool UseOptional>
class SafeProxy
{
    using To = std::remove_reference_t<std::invoke_result_t<decltype(Member), From>>;
    using UnwrapToPtr = UnwrapPointerTraitsT<To>;

    auto GetFrom() { return SafeProxyGetFrom<From, ByteOffset, UseOptional>(this); }

public:
    To* Get() { auto ptr = GetFrom(); return ptr ? std::addressof(ptr->*Member) : nullptr; }
    To GetCopy() { auto ptr = GetFrom(); return ptr ? ptr->*Member : To{}; }
    UnwrapToPtr Unwrap()
    {
        auto ptr = GetFrom();
        return ptr ? UnwrapPointerTraits<To>::Unwrap(ptr->*Member) : nullptr;
    }
    operator UnwrapToPtr() { return Unwrap(); }
    NullCollapse<UnwrapToPtr, AccessContext> operator->() { return { Unwrap() }; }
};

template<std::meta::access_context AccessContext, std::size_t ByteOffset, bool UseOptional>
class SafeFunctions
{
public:
    template <typename From, std::meta::info Func, typename... Args>
    class Overload
    {
        auto GetFrom() { return SafeProxyGetFrom<From, ByteOffset, UseOptional>(this); }
        using RetType = [:return_type_of(Func):];

    public:
        auto operator()(Args... args)
        requires requires(From&& obj)
        {
            obj.[:Func:](std::forward<Args>(args)...);
        }
        {
            auto objPtr = GetFrom();
            if constexpr (std::is_same_v<RetType, void>)
            {
                if (!objPtr)
                    return false;
                (objPtr->[:Func:])(std::forward<Args>(args)...);
                return true;
            }
            else
            {
                using SafeRetType = NullCollapse<RetType, AccessContext>;
                if (!objPtr)
                    return SafeRetType{};
                return SafeRetType{ (objPtr->[:Func:])(std::forward<Args>(args)...) };
            }
        }
    };

    template <typename From, std::meta::info Func>
    class TemplateOverload
    {
        auto GetFrom() { return SafeProxyGetFrom<From, ByteOffset, UseOptional>(this); }

    public:
        template<typename T, typename... Args>
        auto operator()(T&& obj, Args... args)
        requires requires(From&& obj)
        {
            obj.[:Func:](std::forward<Args>(args)...);
        }
        {
            using RetType = decltype(obj.[:Func:](std::forward<Args>(args)...));

            auto objPtr = GetFrom();
            if constexpr (std::is_same_v<RetType, void>)
            {
                if (!objPtr)
                    return false;
                (objPtr->template [:Func:])(std::forward<Args>(args)...);
                return true;
            }
            else
            {
                using SafeRetType = NullCollapse<RetType, AccessContext>;
                if (!objPtr)
                    return SafeRetType{};
                return SafeRetType{ (objPtr->template [:Func:])(std::forward<Args>(args)...) };
            }
        }
    };
};

template<typename T, std::meta::access_context AccessContext, std::size_t ByteOffset, bool UseOptional>
consteval auto GenerateSafeImplMembers()
{
    std::vector<std::meta::info> infos;
    if constexpr (is_class_type(^^T))
    {
        template for (constexpr auto memberInfo : define_static_array(
            nonstatic_data_members_of(^^T, AccessContext)))
        {
            infos.push_back(data_member_spec(
                substitute(^^SafeProxy, 
                    { ^^T, std::meta::reflect_constant(&[:memberInfo:]), reflect_constant(AccessContext),
                    std::meta::reflect_constant(ByteOffset), std::meta::reflect_constant(UseOptional) }), 
                { .name = identifier_of(memberInfo), .no_unique_address = true })
            );
        }

        static constexpr auto listBinders = GroupMemberFunctionsByName<T, AccessContext>();
        template for (constexpr auto binder : define_static_array(
            template_arguments_of(listBinders)))
        {
            using Functions = SafeFunctions<AccessContext, ByteOffset, UseOptional>;
            infos.push_back(data_member_spec(
                MakeOverloads<typename [:binder:], T, StaticOverloads, 
                    Functions::template Overload, Functions::template TemplateOverload>(), 
                { .name = [:binder:]::Name.View(), .no_unique_address = true }));
        }
    }
    
    return infos;
}

template<typename T, typename U>
consteval bool CheckSameLayout()
{
    if (sizeof(T) != sizeof(U))
        return false;

    static constexpr auto membersT = define_static_array(
        nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    static constexpr auto membersU = define_static_array(
        nonstatic_data_members_of(^^U, std::meta::access_context::unchecked()));
    if (membersT.size() != membersU.size())
        return false;

    for (auto idx : std::views::iota(0zu, membersT.size()))
    {
        if (offset_of(membersT[idx]) != offset_of(membersU[idx]))
            return false;
    }
    return true;
}

}

template<typename T, std::meta::access_context AccessContext = std::meta::access_context::current()>
class NullCollapse
{
    class Impl;

    struct Phony { struct Empty {}; UnwrapPointerTraitsT<T> a; Empty b; };
    static constexpr auto Offset = offset_of(^^Phony::b).bytes;
    consteval
    {
        define_aggregate(^^Impl, Details::GenerateSafeImplMembers<
            std::remove_pointer_t<UnwrapPointerTraitsT<T>>, AccessContext, Offset, false>());
    }

    struct Data { UnwrapPointerTraitsT<T> ptr = nullptr; Impl impl; } data_;
    static_assert(Details::CheckSameLayout<Phony, Data>());

public:
    NullCollapse() = default;
    NullCollapse(T& obj) : data_{ UnwrapPointerTraits<T>::Unwrap(obj) } { }
    NullCollapse(T&& obj) : data_{ UnwrapPointerTraits<T>::Unwrap(obj) } { }
    auto operator->() { return &data_.impl; }

    auto Get() { return data_.ptr; }
    auto GetCopy() { return data_.ptr ? *data_.ptr : T{}; }
    auto Unwrap() { return data_.ptr; }
    operator auto() { return Unwrap(); }
};

template<typename T, std::meta::access_context AccessContext>
requires UnwrapPointerTraits<T>::NeedInplace
class NullCollapse<T, AccessContext>
{
    class Impl;

    struct Phony { struct Empty {}; std::optional<T> a; Empty b; };
    static constexpr auto Offset = offset_of(^^Phony::b).bytes;
    consteval
    {
        define_aggregate(^^Impl, Details::GenerateSafeImplMembers<
            std::remove_pointer_t<UnwrapPointerTraitsT<T>>, AccessContext, Offset, true>());
    }

    struct Data { std::optional<T> buffer; Impl impl; } data_;
    static_assert(Details::CheckSameLayout<Phony, Data>());

public:
    NullCollapse() = default;
    NullCollapse(T& obj) : data_{ obj } { }
    NullCollapse(T&& obj) : data_{ std::move(obj) } { }
    auto operator->() { return &data_.impl; }

    auto Get() { return data_.buffer ? std::addressof(*data_.buffer) : nullptr; }
    auto GetCopy() { return data_.buffer ? *data_.buffer : T{}; }
    auto Unwrap() { return Get(); }
    operator auto() { return Unwrap(); }
};

}