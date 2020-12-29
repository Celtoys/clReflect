
clReflect: C++ Reflection using clang
=====================================

- - - 

Installation Instructions
-------------------------

The executables in the "bin" directory can be run directly but require the [MSVC 2017 x86 redistributables](https://go.microsoft.com/fwlink/?LinkId=746571).

Quick Tour
----------

[clReflectTest](https://github.com/Celtoys/clReflect/tree/master/src/clReflectTest) is an up-to-date test of the clReflect library, showing how to build a database and load it at runtime.

C++ primitives are not reflected by default and need to be marked by `clcpp_reflect` Reflection Specs or using reflect attributes. More details can be found [here](https://github.com/Celtoys/clReflect/blob/master/doc/MarkingPrimitivesForReflection.md).

First use `clscan` to parse your C++ files and output readable databases of type information:

```
clscan.exe test.cpp --output test.csv
```

Each of these databases can then be merged into one for your module using `clmerge`:

```
clmerge.exe output.csv input0.csv input1.csv input2.csv ...
```

Finally you can use `clexport` to convert this text database to a binary, memory-mapped database that can be quickly loaded by your C++ code:

```
clexport output.csv -cpp output.cppbin
```

This will give you a runtime loadable database with two limitations:

1. Functions and their parameters/return types will be reflected but their call address will not.
2. You will have to use [clcpp::Database::GetType](https://github.com/Celtoys/clReflect/blob/master/inc/clcpp/clcpp.h#L885) in unison with [clcpp::Database::GetName](https://github.com/Celtoys/clReflect/blob/master/inc/clcpp/clcpp.h#L881) to get types at runtime instead of the more efficient [clcpp::GetType](https://github.com/Celtoys/clReflect/blob/master/inc/clcpp/clcpp.h#L934).

Make sure you pay attention to all reported warnings and inspect all output log files if you suspect there is a problem!

Reflecting Function Addresses
-----------------------------

All platforms support parsing the output of your compiler's MAP file and matching function address to their reflected equivalents in the database. Modify your `clexport` calls to achieve this:

```
clexport output.csv -cpp output.cppbin -map module.map
```

Constant-time, Stringless Type-of Operator
------------------------------------------

To use the constant-time, stringless `GetType` and `GetTypeNameHash` functions you need to ask `clmerge` to generate their implementations for you:

```
clmerge.exe output.csv -cpp_codegen gengettype.cpp input0.csv input1.csv input2.csv ...
```

Compile and link this generated C++ file with the rest of your code, load your database, call the generated initialisation function to perform one-time setup and all features of clReflect are available to you.

Matching your Compiler Settings
-------------------------------

`clscan` can forward compiler options to the underlying Clang compiler. Add `--` at the end of your `clscan` command-line and pass any options you want. For example, these are the advised command-line options for generating databases are Windows MSVC compatible:

```
clscan test.cpp --spec_log spec_log.txt --ast_log ast_log.txt --output test.csv -- \
-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D__clcpp_parse__ \
-fdiagnostics-format=msvc -fms-extensions -fms-compatibility -mms-bitfields -fdelayed-template-parsing -std=c++17 -fno-rtti
```
