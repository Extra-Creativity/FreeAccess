#include "FreeAccess/PrivateAccessor.hpp"
#include "FreeAccess/AccessTable.hpp"

#include <print>
#include <stdexcept>

using namespace FreeAccess;

class A
{
    int privateData = 1;

    using Type = void*;
    template<typename T> class B {};
    
    static inline int staticData = 3;

public:
    using ForTestUse = B<int>;

    int publicData = 2;
    double PublicFunc(int a, int b = 2) const { return a + b; }
    double PublicFunc(double d) { return d; }
    template<typename T>
    double PublicFunc(T a)
    {
        std::println("Template Function Called.");
        return static_cast<double>(a);
    }
    
    int AnotherFunc(int d) { return d + 42; }
};

void SingleTest()
{
    A a1, a2;
    std::println("{} {} {}", MakePrivateAccessor<"privateData">(a1).Get(), 
                MakePrivateAccessor<"publicData">(std::move(a1)).Get(),
                PrivateAccessor<A, "staticData">::Get());
    auto publicFunc = MakePrivateAccessor<"PublicFunc">(a2);
    std::println("{} {} {} {}", publicFunc.Call(1), 
                publicFunc.Call(2, 4),
                MakePrivateAccessor<"PublicFunc">(std::move(a2)).Call(1.5),
                publicFunc.Call(3ull));

    using Type = PrivateAccessorT<A, "Type">;
    static_assert(std::same_as<Type, void*>);
    using Type2 = PrivateAccessorTemplateT<A, "B", ^^int>;
    static_assert(std::same_as<Type2, A::ForTestUse>);
}

void TableTest()
{
    A a;
    std::println("{}", a.*AccessTable<A>::data.privateData);
    // You can wrap a simple macro if you feels the code above is clumsy:
    // #define ACCESS(obj, member) 
    //     (obj.*AccessTable<std::remove_cvref_t<decltype(obj)>>::data.member)

    std::println("{}", *AccessTable<A>::sdata.staticData);
    std::println("{}", AccessTable<A>::funcs.PublicFunc(std::move(a), 1.5));
}

int GetData(A& a, std::string_view name)
{ // You can also use int& as long as you can ensure that "a.name" is int.
    if (auto result = DynamicAccessTable<A>::GetData<int>(name, a))
        return *result;
    else
        throw std::runtime_error{ "Error code: " + std::to_string((int)result.error()) };
}

int CallFunction(A& a, std::string_view name)
{
    // Find the best candidate among "a.name(1.5);" and converts to int.
    if (auto result = DynamicAccessTable<A>::Call<int>(name, a, 1.5))
        return *result;
    else
        throw std::runtime_error{ "Error code: " + std::to_string((int)result.error()) };
}

void DTableTest()
{
    A a;
    int d1 = GetData(a, "privateData"), d2 = GetData(a, "publicData");
    int r1 = CallFunction(a, "PublicFunc"), r2 = CallFunction(a, "AnotherFunc");
    std::println("{} {} {} {}", d1, d2, r1, r2);
}

int main()
{
    SingleTest();
    std::println("----------------------");
    TableTest();
    std::println("----------------------");
    DTableTest();
}