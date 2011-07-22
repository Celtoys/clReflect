
#pragma once


namespace logging
{
	//
	// Type of logging message
	//
	enum Tag
	{
		TAG_INFO		= 0x01,
		TAG_WARNING		= 0x02,
		TAG_ERROR		= 0x04,
		TAG_ALL			= TAG_INFO | TAG_WARNING | TAG_ERROR
	};


	//
	// Functions for mapping log names/tags to output streams
	//
	void SetLogToStdout(const char* name, Tag tag);
	void SetLogToFile(const char* name, Tag tag, const char* filename);


	//
	// Get a pre-created stream handle
	//
	typedef void* StreamHandle;
	StreamHandle GetStreamHandle(const char* name);


	//
	// Format and log the specified text to the given streams
	//
	void Log(StreamHandle handle, Tag tag, bool do_prefix, const char* format, ...);


	//
	// Manual control of the indentation level
	//
	void PushIndent(StreamHandle handle);
	void PopIndent(StreamHandle handle);
}


//
// Macros for setting up the log mappings
//
#define LOG_TO_STDOUT(name, tag) logging::SetLogToStdout(#name, logging::TAG_##tag)
#define LOG_TO_FILE(name, tag, filename) logging::SetLogToFile(#name, logging::TAG_##tag, filename)


#define LOG_GET_STREAM_HANDLE(name)											\
	static logging::StreamHandle handle = logging::GetStreamHandle(#name);


//
// Format and log, named and tagged text
//
#define LOG(name, tag, ...)						\
{												\
	LOG_GET_STREAM_HANDLE(name);				\
	logging::Tag t = logging::TAG_##tag;		\
	logging::Log(handle, t, true, __VA_ARGS__);	\
}


//
// Similar to LOG() except that logging the prefix is skipped, allowing you to spread the
// formatting of a single line over multiple log calls.
//
#define LOG_APPEND(name, tag, ...)				\
{												\
	LOG_GET_STREAM_HANDLE(name);				\
	logging::Tag t = logging::TAG_##tag;		\
	logging::Log(handle, t, false, __VA_ARGS__);\
}



#define LOG_PUSH_INDENT(name)		\
{									\
	LOG_GET_STREAM_HANDLE(name);	\
	logging::PushIndent(handle);	\
}


#define LOG_POP_INDENT(name)		\
{									\
	LOG_GET_STREAM_HANDLE(name);	\
	logging::PopIndent(handle);		\
}


#define LOG_NEWLINE(name) LOG(name, INFO, "\n")