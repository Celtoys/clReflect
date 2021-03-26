
//
// ===============================================================================
// clReflect, SerialiseFunction.h - Serialisation of function call parameters and
// ABI-specific call functions for binding to other languages and RPC.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
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
	//
	// Contains a list of parameters ready to be passed to a function
	// Each parameter is represented as a type/pointer pair, describing how the parameter is passed and
	// where the parameter is located in memory.
	//
	class CLCPP_API ParameterData
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

		unsigned int GetNbParameters() const;
		ParamDesc& GetParameter(unsigned int index);
		const ParamDesc& GetParameter(unsigned int index) const;

	private:
		// Parameter array allocated locally
		char m_ParameterData[MAX_NB_FIELDS * sizeof(ParamDesc)];
		unsigned int m_NbParameters;
	};


	//
	// When deserialising a chunk of data that has to be passed as to a function as parameters,
	// this serves as the deserialisation source, allocating and constructing the required objects.
	//
	class CLCPP_API ParameterObjectCache
	{
	public:
		~ParameterObjectCache();

		// Call to initialise the object cache for a specific function
		// Can safely be called multiple times with different objects
		void Init(const clcpp::Function* function);

		// Allocates and constructs a region of memory in the cache for objects of the type specified in the field
		void* AllocParameter(const clcpp::Field* field);

		ParameterData& GetParameters() { return m_Parameters; }
		const ParameterData& GetParameters() const { return m_Parameters; }

	private:
		void DeleteObjects();

		WriteBuffer m_Data;
		ParameterData m_Parameters;
	};



	CLCPP_API bool BuildParameterObjectCache_JSON(ParameterObjectCache& poc, const clcpp::Function* function, ReadBuffer& parameter_source);


	CLCPP_API bool CallFunction_x86_32_msvc_cdecl(const clcpp::Function* function, const ParameterData& parameters);
	CLCPP_API bool CallFunction_x86_32_msvc_thiscall(const clcpp::Function* function, const ParameterData& parameters);
}