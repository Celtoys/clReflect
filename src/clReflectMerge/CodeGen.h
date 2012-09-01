
#pragma once


#include <string>
#include <vector>


namespace cldb
{
	class Database;
}


//
// Simple class for generating lines of code and writing them to file
//
class CodeGen
{
public:
	CodeGen();

	void Line(const char* format, ...);
	void Line();

	void PrefixLine(const char* format, ...);

	void Indent();
	void UnIndent();

	void EnterScope();
	void ExitScope();

	void PushUndoPoint();
	void PopUndoPoint();
	void Undo();

	unsigned int GenerateHash() const;

	void WriteToFile(const char* filename);

	int Size() const { return m_Text.size(); }

private:
	std::string m_Text;
	int m_Indent;
	std::vector<std::string::size_type> m_UndoPoints;
};


void GenMergedCppImpl(const char* filename, const cldb::Database& db);