
#include "DatabaseTextSerialiser.h"
#include "Database.h"

#include <stdio.h>


namespace
{
	//
	// b64 encode/decode block taken from http://base64.sourceforge.net/ by Bob Trower
	// Copyright (c) Trantor Standard Systems Inc., 2001
	// Under the MIT license
	//


	/*
	** Translation Table as described in RFC1113
	*/
	static const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


	/*
	** Translation Table to decode (created by author)
	*/
	static const char cd64[] = "|$$$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$$$XYZ[\\]^_`abcdefghijklmnopq";


	/*
	** encodeblock
	**
	** encode 3 8-bit binary bytes as 4 '6-bit' characters
	*/
	void encodeblock(unsigned char in[3], char out[4], int len)
	{
		out[0] = cb64[in[0] >> 2];
		out[1] = cb64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
		out[2] = len > 1 ? cb64[ ((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6) ] : '=';
		out[3] = len > 2 ? cb64[ in[2] & 0x3f ] : '=';
	}


	/*
	** decodeblock
	**
	** decode 4 '6-bit' characters into 3 8-bit binary bytes
	*/
	void decodeblock(const char in[4], unsigned char out[3])
	{
		out[0] = (unsigned char)(in[0] << 2 | in[1] >> 4);
		out[1] = (unsigned char)(in[1] << 4 | in[2] >> 2);
		out[2] = (unsigned char)(((in[2] << 6) & 0xc0) | in[3]);
	}
}


void crdb::WriteTextDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "w");

	fputs("Names\n", fp);
	fputs("-------------------------------------------------------------------------\n", fp);
	fputs("HashID\tName\n", fp);
	fputs("-------------------------------------------------------------------------\n", fp);

	for (NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
	{
		u8* name_b256 = (u8*)&i->first;
		char name_b64[8];
		encodeblock(name_b256 + 0, name_b64, 3);
		encodeblock(name_b256 + 3, name_b64 + 3, 1);
		name_b64[5] = 0;

		fputs(name_b64, fp);
		fputs("\t", fp);
		fputs(i->second.c_str(), fp);
		fputs("\n", fp);
	}

	fputs("-------------------------------------------------------------------------\n", fp);

	fclose(fp);
}