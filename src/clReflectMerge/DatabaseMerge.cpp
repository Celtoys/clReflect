
//
// ===============================================================================
// clReflect
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

#include "DatabaseMerge.h"

#include <clReflectCore/Database.h>
#include <clReflectCore/Logging.h>


namespace
{
	void CheckClassMergeFailure(const cldb::Class& class_a, const cldb::Class& class_b)
	{
		const char* class_name = class_a.name.text.c_str();

		// This has to be the same class included multiple times in different translation units
		// Ensure that their descriptions match up as best as possible at this point
		if (class_a.base_class != class_b.base_class)
			LOG(main, WARNING, "Class %s differs in base class specification during merge\n", class_name);
		if (class_a.size != class_b.size)
			LOG(main, WARNING, "Class %s differs in size during merge\n", class_name);
	}


	template <typename TYPE>
	void MergeUniques(
		cldb::Database& dest_db,
		const cldb::Database& src_db,
		void (*check_failure)(const TYPE&, const TYPE&) = 0)
	{
		cldb::DBMap<TYPE>& dest_map = dest_db.GetDBMap<TYPE>();
		const cldb::DBMap<TYPE>& src_store = src_db.GetDBMap<TYPE>();

		// Add primitives that don't already exist for primitives where the symbol name can't be overloaded
		for (cldb::DBMap<TYPE>::const_iterator src = src_store.begin();
			src != src_store.end();
			++src)
		{
			cldb::DBMap<TYPE>::const_iterator dest = dest_map.find(src->first);
			if (dest == dest_map.end())
				dest_db.AddPrimitive(src->second);

			else if (check_failure != 0)
				check_failure(src->second, dest->second);
		}
	}


	template <typename TYPE>
	void MergeOverloads(
		cldb::Database& dest_db,
		const cldb::Database& src_db)
	{
		cldb::DBMap<TYPE>& dest_map = dest_db.GetDBMap<TYPE>();
		const cldb::DBMap<TYPE>& src_map = src_db.GetDBMap<TYPE>();

		// Unconditionally add primitives that don't already exist
		for (cldb::DBMap<TYPE>::const_iterator src = src_map.begin();
			src != src_map.end();
			++src)
		{
			cldb::DBMap<TYPE>::const_iterator dest = dest_map.find(src->first);
			if (dest == dest_map.end())
			{
				dest_db.AddPrimitive(src->second);
			}

			else
			{
				// A primitive of the same name exists so double-check all existing entries for a matching primitives before adding
				bool add = true;
				cldb::DBMap<TYPE>::const_range dest_range = dest_map.equal_range(src->first);
				for (cldb::DBMap<TYPE>::const_iterator i = dest_range.first; i != dest_range.second; ++i)
				{
					if (i->second.Equals(src->second))
					{
						add = false;
						break;
					}
				}

				if (add)
					dest_db.AddPrimitive(src->second);
			}
		}
	}

}


void MergeDatabases(cldb::Database& dest_db, const cldb::Database& src_db)
{
	// Merge name maps
	for (cldb::NameMap::const_iterator i = src_db.m_Names.begin(); i != src_db.m_Names.end(); ++i)
		dest_db.GetName(i->second.text.c_str());

	// The symbol names for these primitives can't be overloaded
	MergeUniques<cldb::Namespace>(dest_db, src_db);
	MergeUniques<cldb::Type>(dest_db, src_db);
	MergeUniques<cldb::Enum>(dest_db, src_db);
	MergeUniques<cldb::Template>(dest_db, src_db);
	MergeUniques<cldb::TemplateType>(dest_db, src_db);

	// Class symbol names can't be overloaded but extra checks can be used to make sure
	// the same class isn't violating the One Definition Rule
	MergeUniques<cldb::Class>(dest_db, src_db, CheckClassMergeFailure);

	// Add enum constants as if they are overloadable
	// NOTE: Technically don't need to do this enum constants are scoped. However, I might change
	// that in future so this code will become useful.
	MergeOverloads<cldb::EnumConstant>(dest_db, src_db);

	// Functions can be overloaded so rely on their unique id to merge them
	MergeOverloads<cldb::Function>(dest_db, src_db);

	// Field names aren't scoped and hence overloadable. They are parented to unique functions so that will
	// be the key deciding factor in whether fields should be merged or not.
	MergeOverloads<cldb::Field>(dest_db, src_db);

	// Attributes are not scoped and are shared to save runtime memory so all of these are overloadable
	MergeOverloads<cldb::FlagAttribute>(dest_db, src_db);
	MergeOverloads<cldb::IntAttribute>(dest_db, src_db);
	MergeOverloads<cldb::FloatAttribute>(dest_db, src_db);
	MergeOverloads<cldb::NameAttribute>(dest_db, src_db);
	MergeOverloads<cldb::TextAttribute>(dest_db, src_db);

	// Merge containers
	MergeUniques<cldb::ContainerInfo>(dest_db, src_db);
}