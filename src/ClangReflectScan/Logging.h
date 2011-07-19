
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
	StreamHandle GetStreamHandle(const char* name, Tag tag);


	//
	// Format and log the specified text to the given streams
	//
	void Log(StreamHandle handle, Tag tag, const char* format, ...);
}


//
// Macros for setting up the log mappings
//
#define LOG_TO_STDOUT(name, tag) logging::SetLogToStdout(#name, logging::TAG_##tag)
#define LOG_TO_FILE(name, tag, filename) logging::SetLogToFile(#name, logging::TAG_##tag, filename)


//
// Format and log, named and tagged text
//
#define LOG(name, tag, ...)												\
{																		\
	logging::Tag t = logging::TAG_##tag;								\
	logging::StreamHandle handle = logging::GetStreamHandle(#name, t);	\
	logging::Log(handle, t, __VA_ARGS__);								\
}