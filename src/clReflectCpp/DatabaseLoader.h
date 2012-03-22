
//
// ===============================================================================
// clReflect, DatabaseLoader.h - Very fast, memory-mapped runtime C++ Reflection
// Database loader. Hidden from the public API and callable through
// clcpp::Database.
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


#include <clcpp/Core.h>

namespace clcpp
{
	struct IFile;
	struct IAllocator;

	namespace internal
	{
		struct DatabaseMem;


		struct DatabaseFileHeader
		{
			// Initialises the file header to the current supported version
			DatabaseFileHeader();

			// Signature and version numbers for verifying header integrity
            // TODO: add check to pervent loading a 64-bit database from 32-bit
            // runtime system, or vice versa
			unsigned int signature0;
			unsigned int signature1;
			unsigned int version;

			int nb_ptr_schemas;
			int nb_ptr_offsets;
			int nb_ptr_relocations;

            clcpp::size_type data_size;

			// TODO: CRC verify?
		};


		DatabaseMem* LoadMemoryMappedDatabase(IFile* file, IAllocator* allocator);
	}
}
