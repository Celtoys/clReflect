
#ifndef _CZ_TEST1_H_
#define _CZ_TEST1_H_

#include "clcpp/clcpp.h"

clcpp_reflect(cz)
namespace cz
{

template<class T>
class TArray
{
};

struct Fu1
{
	int a;
	float b;
	float array1[10];
};

class Fu2
{
public:
	virtual void DoSomething() = 0;
	float array2[10];

};

clcpp_attr(scriptable, customname = "Vector")
class Vector3 : public Fu1
{
public:
	float x;
	float y;
	float z;
	void Add(float _x, float _y, float _z)
	{
		x += _x;
		y += _y;
		z += _z;
	}
};

class DFu : public Fu1, public Fu2
{
	int c;
};

typedef TArray<int> IntArray;

} // namespace cz

#endif

