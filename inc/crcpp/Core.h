
#pragma once


namespace crcpp
{
	// Wrapper around a classic C-style array.
	template <typename TYPE>
	class CArray
	{
	public:
		// Initialise an empty array
		CArray()
			: m_Size(0)
			, m_Data(0)
		{
		}

		// Initialise with array count
		CArray(int size)
			: m_Size(size)
			, m_Data(new TYPE[size])
		{
		}

		// Copy construct
		CArray(const CArray& rhs)
			: m_Size(rhs.size)
			, m_Data(new TYPE[rhs.size])
		{
		}

		~CArray()
		{
			delete [] m_Data;
		}

		int size() const
		{
			return m_Size;
		}

		TYPE* data()
		{
			return m_Data;
		}
		const TYPE* data() const
		{
			return m_Data;
		}

		TYPE& operator [] (int index)
		{
			// TODO: assert
			return m_Data[index];
		}
		const TYPE& operator [] (int index) const
		{
			// TODO: assert
			return m_Data[index];
		}

		CArray& operator = (const CArray& rhs)
		{
			// Check for self-assignment
			if (this == &rhs)
			{
				return *this;
			}

			// Default construct the array data
			delete [] m_Data;
			m_Size = rhs.m_Size;
			m_Data = new TYPE[m_Size];

			// Assign each entry
			for (int i = 0; i < m_Size; i++)
			{
				m_Data[i] = rhs.m_Data[i];
			}

			return *this;
		}

	private:
		int m_Size;
		TYPE* m_Data;
	};
}