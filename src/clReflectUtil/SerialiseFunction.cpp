
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clutl/SerialiseFunction.h>
#include <clutl/JSONLexer.h>
#include <clcpp/FunctionCall.h>


namespace
{
	unsigned int ParamAllocSize(const clcpp::Field* field)
	{
		unsigned int param_size = field->type->size;
		if (field->qualifier.op == clcpp::Qualifier::POINTER)
			param_size = sizeof(void*);
		return param_size;
	}
}


clutl::ParameterData::ParameterData()
	: m_NbParameters(0)
{
}


void clutl::ParameterData::Reset()
{
	m_NbParameters = 0;
}


void clutl::ParameterData::PushParameter(const clcpp::Type* type, clcpp::Qualifier::Operator op, void* object)
{
	clcpp::internal::Assert(m_NbParameters < MAX_NB_FIELDS);
	ParamDesc& param = (ParamDesc&)m_ParameterData[m_NbParameters * sizeof(ParamDesc)];
	param.type = type;
	param.op = op;
	param.object = object;
	m_NbParameters++;
}


unsigned int clutl::ParameterData::GetNbParameters() const
{
	return m_NbParameters;
}


clutl::ParameterData::ParamDesc& clutl::ParameterData::GetParameter(unsigned int index)
{
	clcpp::internal::Assert(m_NbParameters < MAX_NB_FIELDS);
	ParamDesc& param = (ParamDesc&)m_ParameterData[index * sizeof(ParamDesc)];
	return param;
}


const clutl::ParameterData::ParamDesc& clutl::ParameterData::GetParameter(unsigned int index) const
{
	clcpp::internal::Assert(m_NbParameters < MAX_NB_FIELDS);
	ParamDesc& param = (ParamDesc&)m_ParameterData[index * sizeof(ParamDesc)];
	return param;
}


clutl::ParameterObjectCache::~ParameterObjectCache()
{
	DeleteObjects();
}


void clutl::ParameterObjectCache::Init(const clcpp::Function* function)
{
	DeleteObjects();

	// Calculate the total space occupied by parameters
	unsigned int total_param_size = 0;
	for (unsigned int i = 0; i < function->parameters.size; i++)
		total_param_size += ParamAllocSize(function->parameters[i]);

	// Pre-allocate the data for the parameters
	m_Data.Reset();
	m_Data.Alloc(total_param_size);

	// And put all write pointers back at the beginning
	m_Data.Reset();
	m_Parameters.Reset();
}


void* clutl::ParameterObjectCache::AllocParameter(const clcpp::Field* field)
{
	// Allocate the space for the parameter
	unsigned int param_size = ParamAllocSize(field);
	void* param_object = m_Data.Alloc(param_size);

	// Call any class constructors
	if (field->type->kind == clcpp::Primitive::KIND_CLASS && field->qualifier.op != clcpp::Qualifier::POINTER)
	{
		const clcpp::Class* class_type = field->type->AsClass();
		if (class_type->constructor)
			CallFunction(class_type->constructor, param_object);
	}

	// Keep track of the parameter before its written to
	m_Parameters.PushParameter(field->type, field->qualifier.op, param_object);

	return param_object;
}


void clutl::ParameterObjectCache::DeleteObjects()
{
	// Call destructor on every known parameter
	unsigned int nb_params = m_Parameters.GetNbParameters();
	for (unsigned int i = 0; i < nb_params; i++)
	{
		const ParameterData::ParamDesc& param = m_Parameters.GetParameter(i);
		if (param.type->kind == clcpp::Primitive::KIND_CLASS && param.op != clcpp::Qualifier::POINTER)
		{
			const clcpp::Class* class_type = param.type->AsClass();
			if (class_type->destructor)
				CallFunction(class_type->destructor, param.object);
		}
	}
}


CLCPP_API bool clutl::BuildParameterObjectCache_JSON(clutl::ParameterObjectCache& poc, const clcpp::Function* function, clutl::ReadBuffer& parameter_source)
{
	// Reuse the incoming cache
	poc.Init(function);

	// A local cache of all function parameters in their sorted order
	const clcpp::Field* sorted_fields[ParameterData::MAX_NB_FIELDS];

	// Sort each parameter into its call order
	unsigned int nb_fields = function->parameters.size;
	for (unsigned int i = 0; i < nb_fields; i++)
	{
		const clcpp::Field* field = function->parameters[i];
		clcpp::internal::Assert(field->offset < ParameterData::MAX_NB_FIELDS);
		sorted_fields[field->offset] = field;
	}

	// Check for parameter opening list
	JSONContext ctx(parameter_source);
	JSONToken t = LexerNextToken(ctx);
	if (t.type != JSON_TOKEN_LBRACKET)
		return false;

	// Allocate parameters for each field
	for (unsigned i = 0; i < nb_fields; i++)
	{
		const clcpp::Field* field = sorted_fields[i];
		void* param_object = poc.AllocParameter(field);

		// Parse the JSON parameter and move onto the next one
		JSONError error = LoadJSON(ctx, param_object, field);
		if (error.code != clutl::JSONError::NONE)
			return false;
	}

	// TODO: What about patching up pointers?

	return true;
}


#ifdef _MSC_VER


static bool CallFunction_x86_32_msvc(const clcpp::Function* function, const clutl::ParameterData& parameters, bool thiscall)
{
	// Ensure parameter count matches
	unsigned int nb_params = function->parameters.size;
	if (nb_params != parameters.GetNbParameters())
		return false;

	int last_param = 0;
	if (thiscall)
		last_param = 1;

	// Walk over the parameters, pushing them on the native stack in right-to-left order
	unsigned int stack_size = 0;
	for (int i = nb_params - 1; i >= last_param; i--)
	{
		const clutl::ParameterData::ParamDesc& param = parameters.GetParameter(i);

		if (param.op == clcpp::Qualifier::POINTER)
		{
			// Directly push the pointer value
			void* param_object = *(void**)param.object;
			__asm push param_object
			stack_size += sizeof(void*);
		}

		else if (param.op == clcpp::Qualifier::REFERENCE)
		{
			void* param_object = param.object;
			__asm push param_object
			stack_size += sizeof(void*);
		}

		else
		{
			unsigned int param_size = param.type->size;
			void* param_object = param.object;

			__asm
			{
				// Get the parameter size, round up to 32-bit alignment and allocate some stack space
				mov ecx, param_size
				add ecx, 3
				and ecx, 0FFFFFFFCh
				sub esp, ecx
				add stack_size, ecx

				// Copy the object
				shr ecx, 2
				mov edi, esp
				mov esi, param_object
				rep movsd
			}
		}
	}

	// Call the function
	unsigned int function_address = function->address;
	if (thiscall)
	{
		const clutl::ParameterData::ParamDesc& param = parameters.GetParameter(0);
		void* param_object = *(void**)param.object;

		// Only call if this pointer is valid
		if (param_object)
		{
			__asm
			{
				mov ecx, param_object
				mov ebx, function_address
				call ebx
			}
		}
	}
	else
	{
		__asm
		{
			call function_address
			add esp, stack_size
		}
	}

	// TODO: Need to do bounds checking here?

	return true;
}


CLCPP_API bool clutl::CallFunction_x86_32_msvc_cdecl(const clcpp::Function* function, const ParameterData& parameters)
{
	unsigned int nb_params = function->parameters.size;
	if (nb_params != parameters.GetNbParameters())
		return false;

	return CallFunction_x86_32_msvc(function, parameters, false);
}


CLCPP_API bool clutl::CallFunction_x86_32_msvc_thiscall(const clcpp::Function* function, const clutl::ParameterData& parameters)
{
	unsigned int nb_params = function->parameters.size;
	if (nb_params != parameters.GetNbParameters())
		return false;

	return CallFunction_x86_32_msvc(function, parameters, true);
}


#endif
