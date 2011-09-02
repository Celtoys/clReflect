
#pragma once


namespace clutl
{
	class OutputBuffer
	{
	public:
		OutputBuffer(unsigned int size);
		~OutputBuffer();

		void Write(const void* data, unsigned int length);
		void WriteAt(const void* data, unsigned int length, unsigned int position);

		unsigned int GetPosition() const { return m_Position; }

	private:
		char* m_Data;
		unsigned int m_Size;
		unsigned int m_Position;
	};
}