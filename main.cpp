
#include <tora.hpp>
#include <iostream>

static tora::word_t g_hello_read  = tora::to_word(20);
static tora::word_t g_hello_write = tora::to_word(200);

static int test_hello_read_write()
{
	tora::context_t ctx;
	tora::transaction_t* t = new tora::transaction_t(&ctx);
	tora::addr_t a = &g_hello_read;
	assert(t->read(a) == tora::to_word(20));

	tora::entry_t e;

	e = t->get(t->ensure(a));
	assert(e.m_new.m_value == tora::to_word(20));
	assert(e.m_old.m_value == tora::to_word(20));
	assert(e.m_new.m_version == 1);
	assert(e.m_old.m_version == 1);

	t->write(a, tora::to_word(30));
	assert(t->read(a) == tora::to_word(30));
	e = t->get(t->ensure(a));
	assert(e.m_new.m_value == tora::to_word(30));
	assert(e.m_old.m_value == tora::to_word(20));
	assert(e.m_new.m_version == 3);
	assert(e.m_old.m_version == 1);
	
	t->write(a, tora::to_word(40));
	e = t->get(t->ensure(a));
	assert(e.m_new.m_value == tora::to_word(40));
	assert(e.m_old.m_value == tora::to_word(20));
	assert(e.m_new.m_version == 5);
	assert(e.m_old.m_version == 1);

	assert(t->entry_size() == 1);

	tora::addr_t b = &g_hello_write;
	t->write(b, tora::to_word(210));
	assert(t->entry_size() == 2);

	t->commit();
	delete t;
}

static tora::word_t g_hello_commit = tora::to_word(10);

static int test_hello_commit()
{
	tora::context_t ctx;
	tora::transaction_t* t = new tora::transaction_t(&ctx);

	t->write(&g_hello_commit, tora::to_word(20));

	assert(tora::to_word(10) == g_hello_commit);
	t->commit();
	assert(tora::to_word(20) == g_hello_commit);

	delete t;
}

static tora::word_t g_hello_abort = tora::to_word(10);

static int test_hello_abort()
{
	tora::context_t ctx;
	tora::transaction_t* t = new tora::transaction_t(&ctx);

	t->write(&g_hello_abort, tora::to_word(20));

	assert(tora::to_word(10) == g_hello_abort);
	t->abort();
	assert(tora::to_word(10) == g_hello_abort);

	delete t;
}

static tora::word_t g_hello_commit_abort = tora::to_word(10);

static int test_hello_commit_abort()
{
	tora::context_t ctx;
	tora::addr_t a = &g_hello_commit_abort;

	tora::transaction_t t1(&ctx);
	tora::transaction_t t2(&ctx);

	t1.write(a,  tora::to_word(20));
	assert(tora::to_word(20) == t1.read(a));
	assert(tora::to_word(10) == t2.read(a));

	t2.write(a, tora::to_word(30));
	assert(tora::to_word(20) == t1.read(a));
	assert(tora::to_word(30) == t2.read(a));

	assert(tora::to_word(10) == g_hello_commit_abort);
	t1.commit();
	assert(tora::to_word(20) == g_hello_commit_abort);

	bool t2_is_bad = false;
	try { t2.commit(); } catch (tora::bad_consistency_t&) { t2_is_bad = true; }
	assert(t2_is_bad);
	assert(tora::to_word(20) == g_hello_commit_abort);

}


static tora::word_t g_hello_false_abort = tora::to_word(10);
static int test_hello_false_abort()
{
	tora::context_t ctx;
	tora::addr_t a = &g_hello_false_abort;

	tora::transaction_t t1(&ctx);

	t1.write(a, tora::to_word(20));

	/* enter commit()-ing */
	bool ok;
	ok = t1.acquire_all();
	assert(ok);

	bool thrown = false;
	try {
		/* another transaction start, that should make contention */
		tora::transaction_t t2(&ctx);
		t2.write(a, tora::to_word(20));
	} catch (const tora::bad_consistency_t&) {
		thrown = true;
	}
	assert(thrown);

	
	assert(g_hello_false_abort == tora::to_word(10));
	// finish commit().
	t1.make_all_changes();
	assert(g_hello_false_abort == tora::to_word(20));

}

int main(int argc, char* argv[])
{
	test_hello_read_write();
	test_hello_commit();
	test_hello_abort();
	test_hello_commit_abort();
	test_hello_false_abort();
	return 0;
}
