
#pragma once


namespace clcpp
{
	struct Type;
	class Database;
}


namespace clutl
{
	struct Object
	{
		// TODO: is this field to be reflected? Probably not.
		const clcpp::Type* type;
	};


	// TODO: Reflect this and walk its object list so that
	class ObjectDatabase
	{
	public:
		ObjectDatabase(clcpp::Database& db);
		~ObjectDatabase();

		Object* CreateObject(unsigned int type_hash);
		void DestroyObject(Object* object);

	private:
		clcpp::Database& m_ReflectionDB;
	};
}