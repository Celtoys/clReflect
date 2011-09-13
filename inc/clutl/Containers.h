
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