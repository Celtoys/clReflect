
//
// ===============================================================================
// clReflect, ReflectionSpecs.h - First pass traversal of the clang AST for C++,
// locating reflection specifications.
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


#include <map>


namespace clang
{
	class TranslationUnitDecl;
}

namespace cldb
{
	class Database;
}


// Info for reflecting a container
struct ReflectionSpecContainer
{
	typedef std::map<std::string, ReflectionSpecContainer> MapType;
	std::string read_iterator_type;
	std::string write_iterator_type;
	bool has_key;
};


// TODO: Optimise the use of strings in this class - it's not really any good
class ReflectionSpecs
{
public:
	ReflectionSpecs(bool reflect_all, const std::string& spec_log);

	void Gather(clang::TranslationUnitDecl* tu_decl);

	bool IsReflected(std::string name) const;

	const ReflectionSpecContainer::MapType& GetContainerSpecs() const { return m_ContainerSpecs; }

private:
	void AddReflectionSpec(const std::string& symbol, bool partial);

	bool m_ReflectAll;

	std::map<std::string, bool> m_ReflectionSpecs;

	ReflectionSpecContainer::MapType m_ContainerSpecs;
};