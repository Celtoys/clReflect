
#include "DatabaseMerge.h"

#include "ClangReflectCore/Database.h"
#include "ClangReflectCore/Logging.h"


namespace
{
	void CheckClassMergeFailure(const crdb::Class& class_a, const crdb::Class& class_b)
	{
		const char* class_name = class_a.name.text.c_str();

		// This has to be the same class included multiple times in different translation units
		// Ensure that their descriptions match up as best as possible at this point
		if (class_a.base_class != class_b.base_class)
		{
			LOG(main, WARNING, "Class %s differs in base class specification during merge\n", class_name);
		}
		if (class_a.size != class_b.size)
		{
			LOG(main, WARNING, "Class %s differs in size during merge\n", class_name);
		}
	}


	template <typename TYPE>
	void MergeUniques(
		crdb::Database& dest_db,
		const crdb::Database& src_db,
		void (*check_failure)(const TYPE&, const TYPE&) = 0)
	{
		crdb::PrimitiveStore<TYPE>& dest_store = dest_db.GetPrimitiveStore<TYPE>();
		const crdb::PrimitiveStore<TYPE>& src_store = src_db.GetPrimitiveStore<TYPE>();

		// Add primitives that don't already exist for primitives where the symbol name can't be overloaded
		for (crdb::PrimitiveStore<TYPE>::const_iterator src = src_store.begin();
			src != src_store.end();
			++src)
		{
			crdb::PrimitiveStore<TYPE>::const_iterator dest = dest_store.find(src->first);
			if (dest == dest_store.end())
			{
				dest_db.AddPrimitive(src->second);
			}

			else if (check_failure != 0)
			{
				check_failure(src->second, dest->second);
			}
		}
	}


	template <typename TYPE>
	void MergeOverloads(
		crdb::Database& dest_db,
		const crdb::Database& src_db)
	{
		crdb::PrimitiveStore<TYPE>& dest_store = dest_db.GetPrimitiveStore<TYPE>();
		const crdb::PrimitiveStore<TYPE>& src_store = src_db.GetPrimitiveStore<TYPE>();

		// Unconditionally add primitives that don't already exist
		for (crdb::PrimitiveStore<TYPE>::const_iterator src = src_store.begin();
			src != src_store.end();
			++src)
		{
			crdb::PrimitiveStore<TYPE>::const_iterator dest = dest_store.find(src->first);
			if (dest == dest_store.end())
			{
				dest_db.AddPrimitive(src->second);
			}

			else
			{
				// A primitive of the same name exists so double-check all existing entries for a matching primitives before adding
				bool add = true;
				crdb::PrimitiveStore<TYPE>::const_range dest_range = dest_store.equal_range(src->first);
				for (crdb::PrimitiveStore<TYPE>::const_iterator i = dest_range.first; i != dest_range.second; ++i)
				{
					if (i->second.Equals(src->second))
					{
						add = false;
						break;
					}
				}

				if (add)
				{
					dest_db.AddPrimitive(src->second);
				}
			}
		}
	}

}


void MergeDatabases(crdb::Database& dest_db, const crdb::Database& src_db)
{
	// Merge name maps
	for (crdb::NameMap::const_iterator i = src_db.m_Names.begin(); i != src_db.m_Names.end(); ++i)
	{
		dest_db.GetName(i->second.text.c_str());
	}

	// The symbol names for these primitives can't be overloaded
	MergeUniques<crdb::Namespace>(dest_db, src_db);
	MergeUniques<crdb::Type>(dest_db, src_db);
	MergeUniques<crdb::Enum>(dest_db, src_db);

	// Class symbol names can't be overloaded but extra checks can be used to make sure
	// the same class isn't violating the One Definition Rule
	MergeUniques<crdb::Class>(dest_db, src_db, CheckClassMergeFailure);

	// Add enum constants as if they are overloadable
	// NOTE: Technically don't need to do this enum constants are scoped. However, I might change
	// that in future so this code will become useful.
	MergeOverloads<crdb::EnumConstant>(dest_db, src_db);

	// Functions can be overloaded so rely on their unique id to merge them
	MergeOverloads<crdb::Function>(dest_db, src_db);

	// Field names aren't scoped and hence overloadable. They are parented to unique functions so that will
	// be the key deciding factor in whether fields should be merged or not.
	MergeOverloads<crdb::Field>(dest_db, src_db);

	// Attributes are not scoped and are shared to save runtime memory so all of these are overloadable
	MergeOverloads<crdb::FlagAttribute>(dest_db, src_db);
	MergeOverloads<crdb::IntAttribute>(dest_db, src_db);
	MergeOverloads<crdb::FloatAttribute>(dest_db, src_db);
	MergeOverloads<crdb::NameAttribute>(dest_db, src_db);
	MergeOverloads<crdb::TextAttribute>(dest_db, src_db);
}