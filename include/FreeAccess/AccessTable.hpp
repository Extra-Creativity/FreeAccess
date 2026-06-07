#pragma once

#include "FreeAccess/PrivateAccessor.hpp"
#include <algorithm>
#include <expected>
#include <functional>
#include <variant>

namespace FreeAccess
{

namespace Details
{

template<typename T, std::meta::access_context AccessContext, bool IsStatic>
consteval auto GenerateDataMembers()
{
    std::vector<std::meta::info> infos;
    template for (constexpr auto memberInfo : define_static_array(
        IsStatic ? static_data_members_of(^^T, AccessContext) :
        nonstatic_data_members_of(^^T, AccessContext)))
    {
        infos.push_back(data_member_spec(^^decltype(&[:memberInfo:]), 
                                        { .name = identifier_of(memberInfo) }));
    }
    return infos;
}

template<typename T, std::meta::access_context AccessContext, bool IsStatic, typename DstType>
consteval auto FillDataMembers()
{
    DstType members;
    static constexpr auto dstArr = define_static_array(
            nonstatic_data_members_of(^^DstType, AccessContext)), 
        srcArr = define_static_array(
            IsStatic ? static_data_members_of(^^T, AccessContext) :
            nonstatic_data_members_of(^^T, AccessContext));

    // Thanks to preserving order.
    template for (constexpr auto idx : std::views::iota(0zu, dstArr.size()))
    {
        members.[:dstArr[idx]:] = &[:srcArr[idx]:];
    }
    return members;
}

template <typename ObjectType, std::meta::info Func, typename... Args>
class StaticOverload
{
public:
    template<typename T>
    static decltype(auto) operator()(T&& obj, Args... args)
    requires requires
    {
        requires std::same_as<std::remove_cvref_t<T>, ObjectType>;
        (std::forward<T>(obj).[:Func:])(std::forward<Args>(args)...);
    }
    {
        return (std::forward<T>(obj).[:Func:])(std::forward<Args>(args)...);
    }
};

template <typename ObjectType, std::meta::info Func>
class StaticTemplateOverload
{
public:
    template<typename T, typename... Args>
    static decltype(auto) operator()(T&& obj, Args... args)
    requires requires
    {
        requires std::same_as<std::remove_cvref_t<T>, ObjectType>;
        (std::forward<T>(obj).template [:Func:])(std::forward<Args>(args)...);
    }
    {
        return (std::forward<T>(obj).template [:Func:])(std::forward<Args>(args)...);
    }
};

template <typename... OverloadTypes>
class StaticOverloads : public OverloadTypes...
{
public:
    using OverloadTypes::operator()...;
};

template <std::meta::info... Members>
struct ListBinder
{
    static inline constexpr auto Name = FixedString<identifier_of(Members...[0]).size()>{
        identifier_of(Members...[0])
    }; 

    static constexpr auto GetAll()
    {
        return std::array<std::meta::info, sizeof...(Members)>{ Members... };
    }
};

template <typename T, std::meta::access_context AccessContext>
consteval auto GroupMemberFunctionsByName() {
    auto members = members_of(^^T, AccessContext)
        | std::views::filter([](std::meta::info m) {
            return (is_function(m) || is_function_template(m))
                && has_identifier(m);
        }) | std::ranges::to<std::vector>();

    std::ranges::sort(members, [](std::meta::info a, std::meta::info b) {
        return identifier_of(a) < identifier_of(b);
    });

    auto groups = members | std::views::chunk_by(
        [](std::meta::info a, std::meta::info b) {
            return identifier_of(a) == identifier_of(b);
        });

    std::vector<std::meta::info> listBinders;
    for (auto group : groups)
    {
        listBinders.push_back(substitute(^^ListBinder,
            group | std::views::transform([](auto info) { return reflect_constant(info); })));
    }

    return substitute(^^std::tuple, listBinders);
}

template<typename T, std::meta::access_context AccessContext>
consteval auto GenerateFunctions()
{
    std::vector<std::meta::info> infos;
    static constexpr auto listBinders = GroupMemberFunctionsByName<T, AccessContext>();
    template for (constexpr auto binder : define_static_array(
        template_arguments_of(listBinders)))
    {
        infos.push_back(data_member_spec(
            MakeOverloads<typename [:binder:], T, StaticOverloads, 
                StaticOverload, StaticTemplateOverload>(), 
            { .name = [:binder:]::Name.View() }));
    }
    return infos;
}

template<typename T, typename E>
using ExpectedType = std::expected<
    std::conditional_t<std::is_reference_v<T>, 
        std::reference_wrapper<std::remove_reference_t<T>>, T>, 
    E>;
}

template<typename T, std::meta::access_context AccessContext = std::meta::access_context::unchecked()>
struct AccessTable
{
    struct NonStaticDataMember;
    struct StaticDataMember;
    struct Functions;

    consteval
    {
        define_aggregate(^^NonStaticDataMember, Details::GenerateDataMembers<T, AccessContext, false>());
        define_aggregate(^^StaticDataMember, Details::GenerateDataMembers<T, AccessContext, true>());
        define_aggregate(^^Functions, Details::GenerateFunctions<T, AccessContext>());
    }

    static constexpr inline NonStaticDataMember data = Details::FillDataMembers<T, AccessContext, false, NonStaticDataMember>();
    static constexpr inline StaticDataMember sdata = Details::FillDataMembers<T, AccessContext, true, StaticDataMember>();
    static constexpr inline Functions funcs{};
};

struct DynamicAccessTableBase
{
    enum class Error { NoSuchMember, NoMatchingCall };
};

template<typename T, std::meta::access_context AccessContext = std::meta::access_context::unchecked()>
class DynamicAccessTable : public DynamicAccessTableBase
{
    using DataType = [:substitute(^^std::variant, 
        nonstatic_data_members_of(^^typename AccessTable<T, AccessContext>::NonStaticDataMember,
                                  std::meta::access_context::unprivileged()) 
        | std::views::transform(std::meta::type_of)):];
    using StaticDataType = [:substitute(^^std::variant, 
        nonstatic_data_members_of(^^typename AccessTable<T, AccessContext>::StaticDataMember,
                                  std::meta::access_context::unprivileged()) 
        | std::views::transform(std::meta::type_of)):];
    using FuncType = [:substitute(^^std::variant, 
        nonstatic_data_members_of(^^typename AccessTable<T, AccessContext>::Functions, 
                                  std::meta::access_context::unprivileged()) 
        | std::views::transform(std::meta::type_of)):];

    template<typename U, typename V>
    static void RegisterDynamicMembers(U& members, V& map)
    {
        static constexpr auto GetAllDataMembers = []() consteval {
            static constexpr auto arr = define_static_array(
                nonstatic_data_members_of(^^U, std::meta::access_context::unprivileged())
            );
            
            std::vector<std::meta::info> binderInfos;
            template for (constexpr auto info : arr)
                binderInfos.push_back(substitute(^^Details::ListBinder, { reflect_constant(info) }));
            return substitute(^^std::tuple, binderInfos);
        };

        using ListBinders = [:GetAllDataMembers():];
        template for (constexpr auto idx : std::views::iota(0zu, std::tuple_size<ListBinders>::value))
        {
            using Binder = std::tuple_element<idx, ListBinders>::type;
            map.emplace(std::piecewise_construct, std::tuple{ Binder::Name.View() },
                        std::tuple{ std::in_place_index<idx>, members.[:Binder::GetAll()[0]:] });
        }
    }

    template<typename RetType, typename U, typename V, typename... Args>
    static Details::ExpectedType<RetType, Error> Invoke(
        U& map, std::string_view name, V&& obj, Args... args)
    {
        static_assert(std::derived_from<std::remove_cvref_t<V>, T>,
                      "Passed object isn't derived from the type!");

        auto it = map.find(name);
        if (it == map.end())
            return std::unexpected(Error::NoSuchMember);
    
        auto& callable = it->second;
        return callable.visit([&](auto&& realCallable) -> Details::ExpectedType<RetType, Error> {
            using Type = std::remove_cvref_t<decltype(realCallable)>;
            if constexpr (std::is_invocable_r_v<RetType, Type, V&&, Args...>)
            {
                if constexpr (std::is_void_v<RetType>)
                {
                    std::invoke(realCallable, std::forward<V>(obj),
                                std::forward<Args>(args)...);
                    return {};
                }
                else
                {
                    return std::invoke_r<RetType>(realCallable, std::forward<V>(obj), 
                                                  std::forward<Args>(args)...);
                }
            }
            return std::unexpected(Error::NoMatchingCall);
        });
    }

    template<typename RetType, typename U>
    static Details::ExpectedType<RetType, Error> InvokeStatic(
        U& map, std::string_view name)
    {
        auto it = map.find(name);
        if (it == map.end())
            return std::unexpected(Error::NoSuchMember);
    
        auto& callable = it->second;
        return callable.visit([&](auto objPtr) -> Details::ExpectedType<RetType, Error> {
            using Type = std::remove_cvref_t<decltype(*objPtr)>;
            if constexpr (std::convertible_to<Type, RetType>)
            {
                if constexpr (!std::is_void_v<RetType>)
                    return static_cast<RetType>(*objPtr);
                else
                    return {};
            }
            return std::unexpected(Error::NoMatchingCall);
        });
    }

public:
    static inline std::unordered_map<std::string_view, DataType> dataMap = [](){
        std::unordered_map<std::string_view, DataType> result;
        RegisterDynamicMembers(AccessTable<T, AccessContext>::data, result);
        return result;
    }();
    static inline std::unordered_map<std::string_view, StaticDataType> sdataMap = [](){
        std::unordered_map<std::string_view, StaticDataType> result;
        RegisterDynamicMembers(AccessTable<T, AccessContext>::sdata, result);
        return result;
    }();;
    static inline std::unordered_map<std::string_view, FuncType> funcMap = [](){
        std::unordered_map<std::string_view, FuncType> result;
        RegisterDynamicMembers(AccessTable<T, AccessContext>::funcs, result);
        return result;
    }();

    template<typename RetType, typename V>
    static auto GetData(std::string_view name, V&& obj)
    {
        return Invoke<RetType>(dataMap, name, std::forward<V>(obj));
    }

    template<typename RetType>
    static auto GetStaticData(std::string_view name)
    {
        return InvokeStatic<RetType>(dataMap, name);
    }

    template<typename RetType, typename V>
    static auto GetAllData(std::string_view name, V&& obj)
    {
        if (auto result = GetData<RetType>(name, std::forward<V>(obj)))
            return result;

        return GetData<RetType>(name);
    }

    template<typename RetType, typename V, typename... Args>
    static auto Call(std::string_view name, V&& obj, Args&&... args)
    {
        return Invoke<RetType>(funcMap, name, std::forward<V>(obj),
                               std::forward<Args>(args)...);
    }
};

} // namespace FreeAccess