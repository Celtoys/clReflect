
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "Logging.h"

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <map>


namespace
{
	//
	// Base stream class
	//
	struct Stream
	{
		Stream() : next(0) { }

		virtual ~Stream() { }

		// Implementation required by base classes to do what they want with the text
		virtual void Log(const char* text) = 0;

		// Forward linked list of streams
		Stream* next;
	};


	//
	// Outputs logged strings to stdout
	//
	struct StdoutStream : public Stream
	{
		void Log(const char* text)
		{
			// Doesn't append the '\n'
			fputs(text, stdout);
		}
	};


	//
	// Outputs logged strings to the specified file
	//
	struct FileStream : public Stream
	{
		FileStream(const char* f)
			: filename(f)
		{
			fp = fopen(filename, "w");
		}

		FileStream(const FileStream& fs)
			: filename(fs.filename)
		{
			fp = fopen(filename, "w");
		}

		~FileStream()
		{
			if (fp != 0)
				fclose(fp);
		}

		void Log(const char* text)
		{
			// Flush the file for each write, trying to prevent missing log data
			if (fp != 0)
			{
				fputs(text, fp);
				fflush(fp);
			}
		}

		FILE* fp;
		const char* filename;
	};


	template <typename TYPE, int N>
	struct FixedArray
	{
		FixedArray()
		{
			// Default initialise every entry
			for (int i = 0; i < N; i++)
			{
				data[i] = TYPE(0);
			}
		}

		// Array access
		TYPE operator [] (int index) const
		{
			assert(index >= 0 && index < N && "index out of range");
			return data[index];
		}
		TYPE& operator [] (int index)
		{
			assert(index >= 0 && index < N && "index out of range");
			return data[index];
		}

		TYPE data[N];
	};


	//
	// Number of bits, excluding sign
	//
	const int NB_TAG_BITS = sizeof(logging::Tag) * 8 - 1;


	//
	// Container for a set of streams linked to a name
	//
	typedef FixedArray<Stream*, NB_TAG_BITS> StreamArray;
	struct StreamSet
	{
		StreamSet() : indent_depth(0) { }
		int indent_depth;
		StreamArray streams;
	};


	//
	// The stream  map allows each tag to have its own unique set of streams, per name.
	//
	// stream name -> [ 0 ] stream(0) -> stream(1) ...
	//                [ 1 ] ^...
	//                 ...
	//                [ N ]
	//
	// Each stream object is allocated independently of the others.
	//
	typedef std::map<const char*, StreamSet> StreamMap;
	StreamMap g_StreamMap;


	void DeleteAllStreams()
	{
		for (StreamMap::iterator i = g_StreamMap.begin(); i != g_StreamMap.end(); ++i)
		{
			for (int j = 0; j < NB_TAG_BITS; j++)
			{
				// Delete everything in the linked list
				while (Stream* stream = i->second.streams[j])
				{
					i->second.streams[j] = stream->next;
					delete stream;
				}
			}
		}
	}


	template <typename STREAM_TYPE>
	void SetLogToStream(const char* name, logging::Tag tag, STREAM_TYPE copy)
	{
		// Ensure all streams are deleted on shutdown
		atexit(DeleteAllStreams);

		// Iterate over every set tag
		for (int i = 0; i < NB_TAG_BITS; i++)
		{
			int mask = 1 << i;
			if (tag & mask)
			{
				// Link the newly allocated copy into the forward linked list
				StreamArray& streams = g_StreamMap[name].streams;
				copy.next = streams[i];
				streams[i] = new STREAM_TYPE(copy);
			}
		}
	}


	unsigned int Log2(unsigned int v)
	{
        if (v == 0)
        {
            v = -1;
        }
        else
        {
            v = sizeof(unsigned int) * 8 - 1 - __lzcnt(v);
        }
		return v;
	}
}


void logging::SetLogToStdout(const char* name, Tag tag)
{
	SetLogToStream(name, tag, StdoutStream());
}


void logging::SetLogToFile(const char* name, Tag tag, const char* filename)
{
	// Open the file for writing, destroying older writes
	FILE* fp = fopen(filename, "w");
	if (fp)
	{
		fclose(fp);
	}

	SetLogToStream(name, tag, FileStream(filename));
}


logging::StreamHandle logging::GetStreamHandle(const char* name)
{
	return &g_StreamMap[name];
}


void logging::Log(StreamHandle handle, Tag tag, bool do_prefix, const char* format, ...)
{
	// Format to a local buffer
	char buffer[512];
	va_list args;
	va_start(args, format);
#ifdef _MSC_VER
	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
    vsnprintf(buffer, sizeof(buffer), format, args);
#endif  // _MSC_VER
	va_end(args);

	char prefix[128] = { 0 };
	StreamSet* stream_set = (StreamSet*)handle;

	if (do_prefix)
	{
		if (tag == TAG_INFO)
		{
			// Kick the prefix off with indent characters
			for (int i = 0; i < stream_set->indent_depth; i++)
			{
				prefix[i] = '\t';
			}
			prefix[stream_set->indent_depth] = 0;
		}

		// Add any tag annotations
		switch (tag)
		{
		case (TAG_WARNING): strcat(prefix, "WARNING: "); break;
		case (TAG_ERROR): strcat(prefix, "ERROR: "); break;
		default: break;
		}
	}

	// Iterate over every log output
	int index = Log2(tag);
	Stream* stream = stream_set->streams[index];
	while (stream)
	{
		if (do_prefix)
		{
			stream->Log(prefix);
		}
		stream->Log(buffer);
		stream = stream->next;
	}
}


void logging::PushIndent(StreamHandle handle)
{
	((StreamSet*)handle)->indent_depth++;
}


void logging::PopIndent(StreamHandle handle)
{
	((StreamSet*)handle)->indent_depth--;
}
