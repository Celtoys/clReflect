
//
// ===============================================================================
// clReflect, SerialiseFunction.h - Serialisation of function call parameters and
// ABI-specific call functions for binding to other languages and RPC.
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//

#pragma once


#include <clcpp/clcpp.h>
#include <clutl/Serialise.h>


//
// Two use-cases here:
//
//    1. JSON parameter description to binary data, followed by function call
//    2. Parameters as binary data, serialised to JSON
//
// With a function pointer, JSON parameters can be deserialised and executed with that function:
//
//   const clpp::Function* function = ...;
//   ReadBuffer json_parameters = ...;
//
//   ParameterObjectCache poc;
//   if (BuildParameterObjectCache_JSON(poc, function, json_parameters))
//      CallFunction_x86_32_msvc_cdecl(function, poc.GetParameters());
//
// TODO: Needs binary serialisation support.
//
namespace clutl
{
	class WriteBuffer;
	class ReadBuffer;


	//
	// Contains a list of parameters ready to be passed to a function
	// Each parameter is represented as a type/pointer pair, describing how the parameter is passed and
	// where the parameter is located in memory.
	//
	class ParameterData
	{
	public:
		static const int MAX_NB_FIELDS = 16;

		struct ParamDesc
		{
			const clcpp::Type* type;
			clcpp::Qualifier::Operator op;
			void* object;
		};

		ParameterData();

		// Clears all parameter data
		void Reset();

		// Adds parameters, in left-to-right call order
		void PushParameter(const clcpp::Type* type, clcpp::Qualifier::Operator op, void* object);

		unsigned int GetNbParameters() const { return m_NbParameters; }
		const ParamDesc& GetParameter(unsigned int index) const { return m_Parameters[index]; }

	private:
		// Parameter array allocated locally
		char m_ParameterData[MAX_NB_FIELDS * sizeof(ParamDesc)];
		clcpp::CArray<ParamDesc> m_Parameters;
		unsigned int m_NbParameters;
	};


	//
	// When deserialising a chunk of data that has to be passed as to a function as parameters,
	// this serves as the deserialisation source, allocating and constructing the required objects.
	//
	class ParameterObjectCache
	{
	public:
		~ParameterObjectCache();

		// Call to initialise the object cache for a specific function
		// Can safely be called multiple times with different objects
		void Init(const clcpp::Function* function);

		// Allocates and constructs a region of memory in the cache for objects of the type specified in the field
		void* AllocParameter(const clcpp::Field* field);

		const ParameterData& GetParameters() const { return m_Parameters; }

	private:
		void DeleteObjects();

		WriteBuffer m_Data;
		ParameterData m_Parameters;
	};



	bool BuildParameterObjectCache_JSON(ParameterObjectCache& poc, const clcpp::Function* function, ReadBuffer& parameter_source);


	bool CallFunction_x86_32_msvc_cdecl(const clcpp::Function* function, const ParameterData& parameters);
	bool CallFunction_x86_32_msvc_thiscall(const clcpp::Function* function, const ParameterData& parameters);
}