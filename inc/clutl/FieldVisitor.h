
#pragma once


namespace clcpp
{
	struct Qualifier;
	struct Type;
	struct Class;
}


namespace clutl
{
	//
	// A very fast callback delegate that allows you to specify a method pointer and this pointer for the callback.
	// Expects to be passed three parameters for visiting fields of an object.
	//
	// Example use:
	//
	//    struct Visitor
	//    {
	//       // Visit method signature
	//       void Visit(void* field_object, const clcpp::Type* type, const clcpp::Qualifier& qualifier)
	//       {
	//       };
	//    };
	//
	//    // Create a visitor
	//    Visitor v;
	//    FieldDelegate d = FieldDelegate::Make<Visitor, &Visitor::Visit>(&v);
	//
	//    // Use it to visit the fields of this object
	//    clutl::Object* visited_object = ...;
	//    VisitFields(visited_object, visited_object->type, d);
	//    
	class FieldDelegate
	{
	public:
		template <typename THIS_TYPE, void (THIS_TYPE::*MethodPtr)(void*, const clcpp::Type*, const clcpp::Qualifier&)>
		static FieldDelegate Make(THIS_TYPE* this_ptr)
		{
			FieldDelegate d;
			d.m_This = this_ptr;
			d.m_Function = &CallMethod<THIS_TYPE, MethodPtr>;
			return d;
		}

		FieldDelegate()
			: m_This(0)
			, m_Function(0)
		{
		}

		void operator () (void* field_object, const clcpp::Type* type, const clcpp::Qualifier& qualifier) const
		{
			(*m_Function)(m_This, field_object, type, qualifier);
		}

	private:
		typedef void (*FunctionPtr)(void*, void*, const clcpp::Type*, const clcpp::Qualifier&);

		template <typename THIS_TYPE, void (THIS_TYPE::*MethodPtr)(void*, const clcpp::Type*, const clcpp::Qualifier&)>
		static void CallMethod(void* this_ptr, void* field_object, const clcpp::Type* type, const clcpp::Qualifier& qualifier)
		{
			(((THIS_TYPE*)this_ptr)->*MethodPtr)(field_object, type, qualifier);
		}

		void* m_This;
		FunctionPtr m_Function;
	};


	// Shallow visitation of all fields in an object, including the entries of any containers, any
	// base classes and nested data types.
	// See FieldVisitor for an example of its use.
	void VisitFields(void* object, const clcpp::Type* type, const FieldDelegate& visitor);
}