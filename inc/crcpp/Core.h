
#pragma once


namespace crcpp
{
	template <typename TYPE>
	class ConstArray
	{
	public:
		// Initialise an empty array
		ConstArray()
			: m_Size(0)
			, m_Data(0)
		{
		}

		// Initialise with array count
		ConstArray(int size)
			: m_Size(size)
			, m_Data(new TYPE[size])
		{
		}

		// Copy construct
		ConstArray(const ConstArray& rhs)
			: m_Size(rhs.size)
			, m_Data(new TYPE[rhs.size])
		{
		}

		~ConstArray()
		{
			delete [] m_Data;
		}

		int size() const
		{
			return m_Size;
		}

		const TYPE* data() const
		{
			return m_Data;
		}

		const TYPE& operator [] (int index) const
		{
			return m_Data[index];
		}

		ConstArray& operator = (const ConstArray& rhs)
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