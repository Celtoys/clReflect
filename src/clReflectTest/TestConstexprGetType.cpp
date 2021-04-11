
#include "clcppcodegen.h"
#include <cstdio>

struct Base
{
    const clcpp::Type* type = nullptr;
};

struct attrReflect FirstType : public Base
{
};

struct attrReflect SecondType : public Base
{
};

struct attrReflect ThirdType : public Base
{
};

template <typename Type>
Type* New()
{
    auto* object = new Type();
    object->type = clcpp::GetType<Type>();
    return object;
}

void TestConstexprGetType()
{
    Base* objects[] = {
        New<FirstType>(),
        New<SecondType>(),
        New<ThirdType>(),
    };

    for (Base* object : objects)
    {
        switch (object->type->name.hash)
        {
        case clcppTypeHash<FirstType>():
            printf("Do something with the first type\n");
            break;
        case clcppTypeHash<SecondType>():
            printf("Do something with the second type\n");
            break;
        case clcppTypeHash<ThirdType>():
            printf("Do something with the third type\n");
            break;
        }
    }
}
