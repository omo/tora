/*
 * Copyright (c) 2007 MORITA Hajime
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. 
 */ 

#ifndef TORA_HPP
#define TORA_HPP

#include <cstddef>
#include <cassert>
#include <vector>
#include <exception>

namespace tora
{

	typedef struct word_struct* word_t;
	typedef word_t* addr_t;
	
	/* an opaque scalar value */
	typedef std::size_t id_t;

	/*
	 * exception to notify transactional contention 
	 */
	class bad_consistency_t : public std::exception {};

	template<class T> inline
	word_t to_word(T t) { return reinterpret_cast<word_t>(t); }

	template<class T> inline
	id_t to_id(T t) { return reinterpret_cast<id_t>(t); }

	enum
	{
		/* keep versions odd */
		VERSION_FIRST = 1,
		VERSION_STEP = 2,
		VERSIONS
	};

	struct value_t
	{
		value_t() {}
		value_t(word_t val, id_t ver) : m_value(val), m_version(ver) {}
		word_t m_value;
		id_t   m_version;
	};

	inline bool operator==(const value_t& lhs, const value_t& rhs)
	{
		return (lhs.m_value == rhs.m_value &&
						lhs.m_version == rhs.m_version);
	}

	enum acquisition_e
	{
		ACQUISITION_SUCCEED,
		ACQUISITION_FAILED,
		ACQUISITION_BUSY,
		ACQUISITIONS
	};

	/*
	 * ownership record "orec" :
	 * - we omit "logical state" computation due to implementation difficulty
	 */
	class ownership_t
	{
	public:

		ownership_t() : m_id(VERSION_FIRST) {}
		explicit ownership_t(id_t id) : m_id(id) {}

		/* volatile-copy-constructor/op=, which compiler does not make... */
		ownership_t(const volatile ownership_t& rhs) : m_id(rhs.m_id) {}
		ownership_t(const ownership_t& rhs) : m_id(rhs.m_id) {}

		acquisition_e acquire(id_t prev, id_t next) volatile;
		value_t resolve(addr_t addr) const; 

		/* pseudo op=(), with volatile */
		void assign(const volatile ownership_t& rhs) volatile;

		/*
		 * implementation detail
		 */
	public:

		/* the CAS */
		ownership_t compare_and_swap(id_t prev, id_t next) volatile;

		/*
		 * with volatile need duplicate; we can not cast magic as with const...
		 */
		id_t id() const { return m_id; }
		id_t id() const volatile { return m_id; }
		bool is_version() const { return 0 != (m_id%2); }
		bool is_version() const volatile { return 0 != (m_id%2); }

	private:
		id_t m_id; /* version or ptr to transaction */
	};

	/*
	 * orec table
	 */
	class ownership_table_t
	{
	public:
		enum { SIZE = 256 };

		ownership_t find(addr_t addr) const volatile;
		void insert(addr_t addr, const ownership_t& orec) volatile;
		acquisition_e acquire(addr_t addr, id_t prev, id_t next) volatile;

	private:
		static size_t hash_fn(addr_t addr);
		static size_t index(addr_t addr) { return hash_fn(addr)%SIZE; }

		volatile ownership_t m_records[SIZE];
	};

	/*
	 * transactional context: 
	 * - transaction descriptor that hold consistency
	 *   should share (be initialized with) same transactional context
	 */
	class context_t
	{
	public:
		context_t() {}

		// for trasnsaction_t; friend seems too restrictive...
		ownership_table_t* orecs() { return &m_orecs; }

	private: // noncopyable
		context_t(const context_t& rhs);
		const context_t& operator=(const context_t& rhs);

	private:
		ownership_table_t m_orecs;
	};

	/*
	 * transaction entry for the descriptor:
	 * - hold old/new version number and value of specific address
	 */
	struct entry_t
	{
		entry_t() {}
		entry_t(addr_t addr, const value_t& o, const value_t& n)
			: m_addr(addr), m_old(o), m_new(n) {}

		addr_t m_addr;
		value_t m_old;
		value_t m_new;
	};

	inline bool operator==(const entry_t& lhs, const entry_t& rhs)
	{
		return (lhs.m_addr == rhs.m_addr &&
						lhs.m_old == rhs.m_old &&
						lhs.m_new == rhs.m_new);
	}

	typedef std::vector<entry_t> entry_list_t;

	/*
	 * transaction descriptor:
	 * - hold transaction state
	 * - provide transactional access APIs
	 */
	class transaction_t
	{
	public:

		enum state_e
		{
			STATE_ACTIVE,
			STATE_COMMITED,
			STATE_ABORTED,
			STATES
		};

		explicit transaction_t(context_t* ctx);
		~transaction_t();

		void write(addr_t addr, word_t word);
		word_t read(addr_t addr);
		void commit();
		void abort();

		/*
		 * implementation detail 
		 */
	public:
		std::size_t ensure(addr_t addr);
		bool acquire(const entry_t& entry); 
		void release(const entry_t& entry);

		const	entry_t& get(size_t i) const {
			assert(i < m_entries.size());
			return m_entries[i];
		}

		void set(size_t i, const entry_t& e) {
			assert(i < m_entries.size());
			m_entries[i] = e;
		}

		state_e state() const { return m_state; }
		bool active() const { return STATE_ACTIVE == m_state; }

		/*
		 * commit() subroutines:
		 * extract to emulate concurrent access in tests 
		 */
		bool acquire_all();
		void make_all_changes();

		/* debug-y inspection */
		size_t entry_size() const { return m_entries.size(); }

		static transaction_t* narrow(const ownership_t& orec) {
			assert(!orec.is_version());
			return reinterpret_cast<transaction_t*>(orec.id());
		}

	private: // noncopyable
		transaction_t(const transaction_t& rhs);
		const transaction_t& operator=(const transaction_t& rhs);

	private:
		/* we assume that the descriptor is never shared between threads */
		state_e m_state;
		ownership_table_t* m_orecs;
		entry_list_t m_entries;
	};

}

#endif//TORA_HPP
