
#pragma once


namespace crcpp
{
	//
	// Wrapper around a classic C-style array.
	//
	template <typename TYPE>
	class CArray
	{
	public:
		// Initialise an empty array
		CArray()
			: m_Size(0)
			, m_Data(0)
			, m_Owner(1)
		{
		}

		// Initialise with array count
		CArray(unsigned int size)
			: m_Size(size)
			, m_Data(new TYPE[size])
			, m_Owner(1)
		{
		}

		// Initialise with pre-allocated data
		CArray(void* data, unsigned int size)
			: m_Size(size)
			, m_Data((TYPE*)data)
			, m_Owner(0)
		{
		}

		// Copy construct
		CArray(const CArray& rhs)
			: m_Size(rhs.size())
			, m_Data(new TYPE[rhs.size()])
			, m_Owner(1)
		{
			// Copy each entry
			for (unsigned int i = 0; i < m_Size; i++)
			{
				m_Data[i] = rhs.m_Data[i];
			}
		}

		~CArray()
		{
			if (m_Owner)
			{
				delete [] m_Data;
			}
		}

		// Removes an element from the list without reallocating any memory
		// Causes the order of the entries in the list to change
		void unstable_remove(int index)
		{
			// TODO: assert index
			m_Data[index] = m_Data[m_Size - 1];
			m_Size--;
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
			for (unsigned int i = 0; i < m_Size; i++)
			{
				m_Data[i] = rhs.m_Data[i];
			}

			return *this;
		}

	private:
		unsigned int m_Size : 31;
		unsigned int m_Owner : 1;
		TYPE* m_Data;
	};
}