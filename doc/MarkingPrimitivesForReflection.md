
## Marking Primitives for Reflection

By default, running `clscan` on your files will output no reflection information. Any primitives that you want to be reflected need to be marked in your source code and there are two ways to achieve this, with the best results gained by using both.

From the global namespace you can use Reflection Specs:

```cpp
// Reflects the entire contents of the namespace
clcpp_reflect(core)
namespace core
{
	struct X { };
	struct Y { };
}

// Reflects only the namespace, the nested structures are not reflected
clcpp_reflect_part(core)
namespace core
{
	struct X { };
	struct Y { };
}

// Reflects the namespace and the struct 'X', 'Y' is not reflected
clcpp_reflect_part(core)
clcpp_reflect(core::X)
namespace core
{
	struct X { };
	struct Y { };
}

// Similar to the previous example except none of the contents of 'X' are reflected
clcpp_reflect_part(core)
clcpp_reflect_part(core::X)
namespace core
{
	struct X { int x; };
	struct Y { };
}
```

It's the simplest means of marking primitives for reflection that can be applied to namespaces, classes, fields, functions, enums and templates. It's also non-intrusive, allowing you to reflect 3rd party APIS whose source you can't modify.

This can be made a little clearer by using the reflect and reflect_part attributes:

```cpp
// Reflects the namespace and the struct 'X', 'Y' is not reflected
clcpp_reflect_part(core)
namespace core
{
	struct clcpp_attr(reflect) X { };
	struct Y { };
}

// Similar to the previous example except none of the contents of 'X' are reflected
clcpp_reflect_part(core)
namespace core
{
	struct clcpp_attr(reflect_part) X { int x; };
	struct Y { };
}
```

You can also override reflection specifications made at a higher level with the noreflect attribute:

```cpp
// Full reflection of X is requested but 'x' is not reflected
clcpp_reflect_part(core)
namespace core
{
	struct clcpp_attr(reflect) X
	{
		clcpp_attr(noreflect) int x;
	};
}
```

Due to a limitation with the clang parser, attributes can not be applied to templates or namespaces.
