# FreeAccess
FreeAccess is a tiny header-only library that supports access class members by string, including non-public members, through C++26 compile-time reflection. This library is in an early-development stage so any issues are welcomed!

> Note: It's generally discouraged to access private members unless you know what you're doing.

Say we have a class as follows:
```c++
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
        std::println("Template Function Called");
        return static_cast<double>(a);
    }
    
    int AnotherFunc(int d) { return d + 42; }
};
```

The core features are:
+ Single-member accessor: if you just want to access a particular member of the object, then

  ```c++
  #include "FreeAccess/PrivateAccessor.hpp"
  using namespace FreeAccess;
  
  A a1, a2;
  // Equiv. to: a1.privateData, std::move(a2).privateData, A::staticData.
  std::println("{} {} {}", MakePrivateAccessor<"privateData">(a1).Get(), 
               MakePrivateAccessor<"publicData">(std::move(a1)).Get(),
               PrivateAccessor<A, "staticData">::Get());
  // Calls the first (with default arguments), the first, the second (with rvalue) and the third template overload respectively.
  auto publicFunc = MakePrivateAccessor<"PublicFunc">(a2);
  std::println("{} {} {} {}", publicFunc.Call(1), 
               publicFunc.Call(2, 4),
               MakePrivateAccessor<"PublicFunc">(std::move(a2)).Call(1.5),
               publicFunc.Call(3ull));
  
  using Type = PrivateAccessorT<A, "Type">;
  static_assert(std::same_as<Type, void*>);
  // The reason we accept meta info is that NTTP may be involved, so a normal typename Args... cannot represent all cases.
  using Type2 = PrivateAccessorTemplateT<A, "B", ^^int>;
  static_assert(std::same_as<Type2, A::ForTestUse>);
  ```

  Theoretically private functions can be accessed as well, but [gcc 16.1 hasn't supported it yet](https://github.com/gcc-mirror/gcc/blob/fed7299b89f62b64bd3433faace4eb5bb69fc3ef/gcc/testsuite/g%2B%2B.dg/reflect/member14.C#L21) so it's not currently usable.

+ Access table: if you want to access any data or function members in a class, then

  ```c++
  #include "FreeAccess/AccessTable.hpp"
  using namespace FreeAccess;
  
  A a;
  // Equiv. to a.privateData
  std::println("{}", a.*AccessTable<A>::data.privateData);
  // You can wrap a simple macro if you feels the code above is clumsy:
  // #define ACCESS(obj, member) \
  //     (obj.*AccessTable<std::remove_cvref_t<decltype(obj)>>::data.member)
  
  // Equiv. to A::staticData
  std::println("{}", *AccessTable<A>::sdata.staticData);
  // Equiv. to std::move(a).PublicFunc(1.5);
  // Similarly, you can call any overload as wanted.
  std::println("{}", AccessTable<A>::funcs.PublicFunc(std::move(a), 1.5));
  ```

  Or even call them with runtime string!

  ```c++
  // Note: the actual returned value is std::expected, and you should check
  int GetData(A& a, std::string_view name)
  { // You can also use int& as long as you can ensure that "a.name" is int.
      return *DynamicAccessTable<A>::GetData<int>(name, a);
  }
  
  int CallFunction(A& a, std::string_view name)
  {
      // Find the best candidate among "a.name(1.5);" and converts to int.
      return *DynamicAccessTable<A>::Call<int>(name, a, 1.5);
  }
  
  int main()
  {
      int d1 = GetData(a, "privateData"), d2 = GetData(a, "publicData");
      int r1 = CallFunction(a, "PublicFunc"), r2 = CallFunction(a, "AnotherFunc");
  }
  ```

  So you can save the trouble of manually building function table with lots of if-else.

  > **Note that `a.*AccessTable<A>::data.privateData` now causes compilation error on gcc 16.1, but such compiler bug has been fixed in latest gcc build**.

+ const/volatile safety: a const object can only access const members, and non-const function overloads will be excluded. For example:

  ```c++
  A a1;
  const A a2;
  // Calls double-variant and int-variant respectively, since only int-variant has const qualifier.
  std::println("{} {}", MakePrivateAccessor<"PublicFunc">(a1).Call(1.5), 
               MakePrivateAccessor<"PublicFunc">(a2).Call(1.5));
  // Compilation error:
  a2.*AccessTable<A>::data.privateData = 3;
  ```

## Why not XYZ？

Another library that supports private members access is [martong/access_private](https://github.com/martong/access_private) (or its C++20 version [schaumb/access_private_20](https://github.com/schaumb/access_private_20)). However, say we have a simple class as:

```c++
class A
{
    int m_i = 3;
    int m_f(int p) { return 14 * p; }
};
```

+ It needs more complex code writing than ours since they rely on template friend injection (which is part of stateful metaprogramming and is generally discouraged). For example,

  ```c++
  // access_private:
  ACCESS_PRIVATE_FIELD(A, int, m_i) // must be in global namespace
  ACCESS_PRIVATE_FUN(A, int(int), m_f)
      
  int main()
  {
      A a;
      int p = 3;
      auto &i = access_private::m_i(a);
      auto res = call_private::m_f(a, p);   
  }
  ```

  ```c++
  // access_private_20:
  template struct access_private::access<&A::m_i>;
  template struct access_private::access<&A::m_f>;
  
  int main()
  {
      A a;
      int p = 3;
      auto &i = access_private::accessor<"m_i">(a);
      auto res = access_private::accessor<"m_f">(a, 3);
  }
  ```

+ It's less flexible than ours since it doesn't support access table, especially dynamic one.

+ It's less powerful since types cannot be accessed in such technique.

## TODOs

This library is still in an immature status (just as reflection is still in an immature status in current C++ compilers). There can still be some improvements:

+ Unnamed functions are not supported yet, including constructor, destructor and operator.
+ Currently `DynamicAccessTable` will generate a table for all members, which may be too large in some cases. Annotation labels like `[[=DynamicCall]]` or other methods may be designed to support pruning.
+ Code formatting is not good since clang-format hasn't supported formatting syntax regarding reflection yet.
+ `noexcept` is not kept yet. But such feature is not hard to add, and please submit an issue if you want it eagerly :).