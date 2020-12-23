
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "DatabaseMerge.h"

#include <clReflectCore/Database.h>
#include <clReflectCore/Logging.h>


namespace
{
	const char* g_FilenameMergingDB = 0;

	template <typename TYPE>
	void MergeUniques(cldb::Database &dest_db, const cldb::Database &src_db)
	{
		cldb::DBMap<TYPE> &dest_map = dest_db.GetDBMap<TYPE>();
		const cldb::DBMap<TYPE> &src_store = src_db.GetDBMap<TYPE>();

		// Add primitives that don't already exist for primitives where the symbol name can't be overloaded
		for (typename cldb::DBMap<TYPE>::const_iterator src = src_store.begin(); src != src_store.end(); ++src)
		{
			typename cldb::DBMap<TYPE>::const_iterator dest = dest_map.find(src->first);
			if (dest == dest_map.end())
			{
				dest_db.AddPrimitive(src->second);
			}
		}
	}

    void MergeClasses(cldb::Database& dest_db, const cldb::Database& src_db)
    {
        cldb::DBMap<cldb::Class> &dest_map = dest_db.GetDBMap<cldb::Class>();
        const cldb::DBMap<cldb::Class>& src_map = src_db.GetDBMap<cldb::Class>();

        // Addclasses that don't already exist for classes where the symbol name can't be overloaded
		for (typename cldb::DBMap<cldb::Class>::const_iterator src = src_map.begin(); src != src_map.end(); ++src)
		{
			typename cldb::DBMap<cldb::Class>::iterator dest = dest_map.find(src->first);

			if (dest == dest_map.end())
			{
				dest_db.AddPrimitive(src->second);
			}
			else
			{
				const cldb::Class &dst_class = dest->second;
				const cldb::Class &src_class = src->second;

				// Ensure forward-declarations down't overwrite defined classes
				if (dst_class.size == cldb::Class::FORWARD_DECL_SIZE && src_class.size != cldb::Class::FORWARD_DECL_SIZE)
				{
					dest->second = src_class;
				}

				// This has to be the same class included multiple times in different translation units
				// Ensure that their descriptions match up as best as possible at this point
				else if (dst_class.size != cldb::Class::FORWARD_DECL_SIZE && src_class.size != cldb::Class::FORWARD_DECL_SIZE &&
						dst_class.size != src_class.size)
				{
					LOG(main, WARNING, "Class %s differs in size during merge (source file %s)\n",
						dst_class.name.text.c_str(), g_FilenameMergingDB);
				}
			}
		}
    }

    template <typename TYPE>
    void MergeOverloads(cldb::Database& dest_db, const cldb::Database& src_db)
    {
		cldb::DBMap<TYPE>& dest_map = dest_db.GetDBMap<TYPE>();
		const cldb::DBMap<TYPE>& src_map = src_db.GetDBMap<TYPE>();

		// Unconditionally add primitives that don't already exist
        for (typename cldb::DBMap<TYPE>::const_iterator src = src_map.begin(); src != src_map.end(); ++src)
        {
			typename cldb::DBMap<TYPE>::const_iterator dest = dest_map.find(src->first);
			if (dest == dest_map.end())
			{
				dest_db.AddPrimitive(src->second);
			}

			else
			{
				// A primitive of the same name exists so double-check all existing entries for a matching primitives before adding
				bool add = true;
				typename cldb::DBMap<TYPE>::const_range dest_range = dest_map.equal_range(src->first);
				for (typename cldb::DBMap<TYPE>::const_iterator i = dest_range.first; i != dest_range.second; ++i)
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


void MergeDatabases(cldb::Database& dest_db, const cldb::Database& src_db, const char* filename)
{
	g_FilenameMergingDB = filename;

	// Merge name maps
	for (cldb::NameMap::const_iterator i = src_db.m_Names.begin(); i != src_db.m_Names.end(); ++i)
		dest_db.GetName(i->second.text.c_str());

	// The symbol names for these primitives can't be overloaded
	MergeUniques<cldb::Namespace>(dest_db, src_db);
	MergeUniques<cldb::Type>(dest_db, src_db);
	MergeUniques<cldb::Enum>(dest_db, src_db);
	MergeUniques<cldb::Template>(dest_db, src_db);

	// Class/template type symbol names can't be overloaded but extra checks can be used to make sure
	// the same primitive isn't violating the One Definition Rule
	MergeUniques<cldb::TemplateType>(dest_db, src_db);
    MergeClasses(dest_db, src_db);

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
	MergeOverloads<cldb::PrimitiveAttribute>(dest_db, src_db);
	MergeOverloads<cldb::TextAttribute>(dest_db, src_db);

	// Merge uniquely named non-primitives
	MergeUniques<cldb::ContainerInfo>(dest_db, src_db);
	MergeUniques<cldb::TypeInheritance>(dest_db, src_db);

	g_FilenameMergingDB = 0;
}
