
#include "Logging.h"

#include <cstdio>
#include <cassert>
#include <cstdarg>
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
		}

		void Log(const char* text)
		{
			// Open the file for each log attempt
			FILE* fp = fopen(filename, "a");
			if (fp == 0)
			{
				return;
			}

			// Write and close immediately so that the file isn't left open on crash and all output is flushed
			fputs(text, fp);
			fclose(fp);
		}

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
	// The stream  map allows each tag to have its own unique set of streams, per name.
	//
	// stream name -> [ 0 ] stream(0) -> stream(1) ...
	//                [ 1 ] ^...
	//                 ...
	//                [ N ]
	//
	// Each stream object is allocated independently of the others.
	//
	typedef FixedArray<Stream*, NB_TAG_BITS> StreamArray;
	typedef std::map<const char*, StreamArray> StreamMap;
	StreamMap g_StreamMap;


	void DeleteAllStreams()
	{
		for (StreamMap::iterator i = g_StreamMap.begin(); i != g_StreamMap.end(); ++i)
		{
			for (int j = 0; j < NB_TAG_BITS; j++)
			{
				// Delete everything in the linked list
				while (Stream* stream = i->second[j])
				{
					i->second[j] = stream->next;
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
				StreamArray& streams = g_StreamMap[name];
				copy.next = streams[i];
				streams[i] = new STREAM_TYPE(copy);
			}
		}
	}


	unsigned int Log2(unsigned int v)
	{
		// Branchless, taking into account v=0, x86 specific
		_asm
		{
			mov eax, v
			mov ebx, -1
			bsr eax, eax
			cmovz eax, ebx
			mov v, eax
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


logging::StreamHandle logging::GetStreamHandle(const char* name, Tag tag)
{
	int index = Log2(tag);
	return g_StreamMap[name][index];
}


void logging::Log(StreamHandle handle, Tag tag, const char* format, ...)
{
	// Format to a local buffer
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
	va_end(args);

	// Iterate over every log output
	Stream* stream = (Stream*)handle;
	while (stream)
	{
		// Output a custom prefix based on tag
		switch (tag)
		{
		case (TAG_WARNING): stream->Log("WARNING: "); break;
		case (TAG_ERROR): stream->Log("ERROR: "); break;
		}

		stream->Log(buffer);
		stream = stream->next;
	}
}