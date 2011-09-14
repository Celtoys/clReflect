
//
// ===============================================================================
// clReflect, Containers.h - All data containers and container abstractions that
// the runtime API can deal with by default.
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


namespace clutl
{
	// asserts will only trigger when you attempt to read below 0 or above m_Size
	class DataBuffer
	{
	public:
		DataBuffer(unsigned int capacity);
		~DataBuffer();

		void ResetPosition();
		void ResetPositionAndSize();

		void Write(const void* data, unsigned int length);
		void WriteAt(const void* data, unsigned int length, unsigned int position);

		void Read(void* data, unsigned int length);
		const char* ReadAt(unsigned int position) const;

		void SeekAbs(unsigned int position);
		void SeekRel(int offset);
		void SeekEnd(int offset);

		unsigned int GetPosition() const { return m_Position; }
		unsigned int GetSize() const { return m_Size; }
		bool AtEnd() const { return m_Position == m_Size; }

	private:
		char* m_Data;
		unsigned int m_Capacity;
		unsigned int m_Size;
		unsigned int m_Position;
	};
}