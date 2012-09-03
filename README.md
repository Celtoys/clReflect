
clReflect v0.4 pre-release, C++ Reflection using clang
======================================================

- - - 

Installation Instructions
-------------------------

The executables in the "bin" directory can be run directly but require the [MSVC 2010 x86 redistributables](http://www.microsoft.com/en-us/download/details.aspx?id=5555).

Quick Tour
----------

[clReflectTest](https://bitbucket.org/dwilliamson/clreflect/src/tip/src/clReflectTest) is an up-to-date test of the clReflect library, showing how to build a database and load it at runtime.

C++ primitives are not reflected by default and need to be marked by clcpp_reflect Reflection Specs or using reflect attributes. More details can be found [here](https://bitbucket.org/dwilliamson/clreflect/wiki/Marking%20Primitives%20for%20Reflection). Basic build steps are...

	// Use clReflectScan to parse your C++ files and output a readable database of type information:
	clscan.exe file_a.cpp -output file_a.csv -ast_log file_a_astlog.txt -spec_log file_a_speclog.txt -i "include_path"

	// Use bin\clmerge to merge the output of many C++ files into one readable database of type information:
	clmerge.exe module.csv file_a.csv file_b.csv file_c.csv ...

	// Use bin\clexport to convert the readable database to a memory-mapped, C++ loadable/queryable database:
	clexport.exe module.csv -cpp module.cppbin -cpp_log module_cpplog.txt

This will give you a runtime loadable database with two limitations:

1. Functions and their parameters/return types will be reflected but their runtime address will be null.
2. You will have to use [clcpp::Database::GetType](https://bitbucket.org/dwilliamson/clreflect/src/tip/inc/clcpp/clcpp.h#cl-752) in unison with [clcpp::Database::GetName](https://bitbucket.org/dwilliamson/clreflect/src/tip/inc/clcpp/clcpp.h#cl-748) to get types at runtime.

