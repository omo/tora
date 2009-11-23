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

#include <tora.hpp>
#include <iostream> // for debug
#include <atomic_ops.h>

namespace tora
{
	namespace detail
	{
		bool compare_and_swap(volatile id_t* addr, id_t prev, id_t next) {
			return (0 != AO_compare_and_swap_full
							(reinterpret_cast<volatile AO_t*>(addr), 
							 static_cast<AO_t>(prev), static_cast<AO_t>(next)));
		}

		void barrier()
		{
			AO_nop_full();
		}

	}

	ownership_t
	ownership_t::compare_and_swap(id_t prev, id_t next) volatile
	{
#if 0
		// cas equivalent
		if (m_id == prev) {
			m_id = next;
			return ownership_t(prev);
		} else {
			return ownership_t(m_id);
		}
#else
		if (detail::compare_and_swap(&m_id, prev, next)) {
			assert(m_id == next);
			return ownership_t(prev);
		} else {
			return ownership_t(m_id);
		}
#endif
	}

	void ownership_t::assign(const volatile ownership_t& rhs) volatile
	{
		m_id = rhs.m_id; 
		detail::barrier();
	}

	acquisition_e
	ownership_t::acquire(id_t prev, id_t next) volatile
	{
		ownership_t seen = compare_and_swap(prev, next);
		if (seen.id() == prev || seen.id() == next) {
			return ACQUISITION_SUCCEED;
		} else if (seen.is_version()) {
			return ACQUISITION_FAILED;
		} else {
			return ACQUISITION_BUSY;
		}
	}

	value_t
	ownership_t::resolve(addr_t addr) const
	{
		if (!is_version()) {
			throw bad_consistency_t();
		}

		return value_t(*addr, m_id);
	}

	size_t ownership_table_t::hash_fn(addr_t addr)
	{
		// FIXME: use better hash
		return reinterpret_cast<size_t>(addr)/3;
	}

	ownership_t ownership_table_t::find(addr_t addr) const volatile
	{
		/* here we return copy instead of const ref to keep atomicity. */
		return m_records[index(addr)];
	}

	void ownership_table_t::insert(addr_t addr, const ownership_t& orec) volatile
	{
		m_records[index(addr)].assign(orec);
	}

	acquisition_e
	ownership_table_t::acquire(addr_t addr, id_t prev, id_t next) volatile
	{
		return m_records[index(addr)].acquire(prev, next);
	}

	transaction_t::transaction_t(context_t* ctx)
		: m_state(STATE_ACTIVE), m_orecs(ctx->orecs())
	{}

	transaction_t::~transaction_t()
	{
		if (transaction_t::STATE_ACTIVE == m_state) {
			this->abort();
		}
	}

	word_t transaction_t::read(addr_t addr)
	{
		assert(active());
		return get(ensure(addr)).m_new.m_value;
	}
	
	void transaction_t::write(addr_t addr, word_t word)
	{
		assert(active());

		size_t i = ensure(addr);
		entry_t e = get(i);
		e.m_new.m_value = word;
		e.m_new.m_version += VERSION_STEP;
		set(i, e);
	}

	std::size_t transaction_t::ensure(addr_t addr)
	{
		for (size_t i=0; i<m_entries.size(); ++i) {
			if (m_entries[i].m_addr == addr) {
				return i;
			}
		}

		try {
			value_t val = m_orecs->find(addr).resolve(addr);
			m_entries.push_back(entry_t(addr, val, val));
		}	catch (const bad_consistency_t& ) {
			throw;
		}
		
		return m_entries.size()-1;
	}

	bool
	transaction_t::acquire(const entry_t& entry)
	{
		acquisition_e a = m_orecs->acquire
			(entry.m_addr, entry.m_old.m_version, to_id(this));
		switch (a) {
		case ACQUISITION_SUCCEED:
			return true;
		default:
			/* currently we just fail on BUSY */
			return false;
		}
	}

	void 
	transaction_t::release(const entry_t& entry)
	{
		assert(transaction_t::STATE_ACTIVE != m_state);
		assert(to_id(this) == m_orecs->find(entry.m_addr).id());

		if (transaction_t::STATE_COMMITED == m_state) {
			m_orecs->insert(entry.m_addr, ownership_t(entry.m_new.m_version));
		} else {
			assert(transaction_t::STATE_ABORTED == m_state);
			m_orecs->insert(entry.m_addr, ownership_t(entry.m_old.m_version));
		}

	}

	void transaction_t::commit()
	{
		assert(active());

		if (!acquire_all()) {
			throw bad_consistency_t();
		}

		make_all_changes();
	}

	bool transaction_t::acquire_all()
	{
		// ??? may need sorting by the addrs to avoid livelock
		for (entry_list_t::const_iterator i=m_entries.begin(); 
				 i != m_entries.end(); ++i) {
			if (!acquire(*i)) {
				for (entry_list_t::const_iterator j=m_entries.begin();
						 j!=i; ++j) {
					release(*j);
				}
				return false;
			}
		}

		return true;
	}

	void transaction_t::make_all_changes()
	{
		m_state = transaction_t::STATE_COMMITED;

		for (entry_list_t::const_iterator i=m_entries.begin(); 
				 i != m_entries.end(); ++i) {
			*(i->m_addr) = i->m_new.m_value;
			release(*i);
		}
	}

	void transaction_t::abort()
	{
		assert(active());
		m_state = transaction_t::STATE_ABORTED;
	}

}
