/* Copyright (c) 2015, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/* See http://code.google.com/p/googletest/wiki/Primer */

// First include (the generated) my_config.h, to get correct platform defines.
#include "my_config.h"

/* Enable to perf test std::map instead of
the InnoDB lock free hash. */
#if 0
#define TEST_STD_MAP 1
#endif

/* Enable to perf test std::unordered_map instead of
the InnoDB lock free hash, compile with -std=c++11 */
#if 0
#define TEST_STD_UNORDERED_MAP 1
#endif

/* Enable to perf test tbb::concurrent_hash_map instead of
the InnoDB lock free hash, download from
https://www.threadingbuildingblocks.org/ and also adjust
unittest/gunit/innodb/CMakeLists.txt */
#if 0
#define TEST_TBB 1
#endif

#if TEST_STD_MAP + TEST_STD_UNORDERED_MAP + TEST_TBB > 1
#error TEST_STD_MAP, TEST_STD_UNORDERED_MAP and TEST_TBB are mutually exclusive
#endif /* TEST_STD_MAP && TEST_STD_UNORDERED_MAP */

#ifdef TEST_STD_UNORDERED_MAP
#include <unordered_map>
#endif /* TEST_STD_UNORDERED_MAP */

#ifdef TEST_STD_MAP
#include <map>
#endif /* TEST_STD_MAP */

#ifdef TEST_TBB
#include <tbb/concurrent_hash_map.h>
#endif

#define __STDC_LIMIT_MACROS

#include <gtest/gtest.h>

#include "univ.i"

#include "sync0policy.h" /* needed by ib0mutex.h, which is not self contained */
#include "os0thread.h" /* os_thread_*() */
#include "srv0conc.h" /* srv_max_n_threads */
#include "sync0debug.h" /* sync_check_init(), sync_check_close() */
#include "ut0dbg.h" /* ut_chrono_t */
#include "ut0lock_free_hash.h"
#include "ut0mutex.h" /* SysMutex, mutex_enter() */

extern SysMutex	thread_mutex;

namespace innodb_lock_free_hash_unittest {

#if defined(TEST_STD_MAP) || defined(TEST_STD_UNORDERED_MAP)
class std_hash_t : public ut_hash_interface_t {
public:
#ifdef TEST_STD_MAP
	typedef std::map<uint64_t, int64_t>		map_t;
#else
	typedef std::unordered_map<uint64_t, int64_t>	map_t;
#endif

	/** Constructor. */
	std_hash_t()
	{
		m_map_latch.init(LATCH_ID_NONE, __FILE__, __LINE__);
	}

	/** Destructor. */
	~std_hash_t()
	{
		m_map_latch.destroy();
	}

	int64_t
	get(
		uint64_t	key) const
	{
		m_map_latch.enter(0, 0, __FILE__, __LINE__);

		map_t::const_iterator	it = m_map.find(key);

		int64_t	val;

		if (it != m_map.end()) {
			val = it->second;
		} else {
			val = NOT_FOUND;
		}

		m_map_latch.exit();

		return(val);
	}

	void
	set(
		uint64_t	key,
		int64_t		val)
	{
		m_map_latch.enter(0, 0, __FILE__, __LINE__);

		m_map[key] = val;

		m_map_latch.exit();
	}

	void
	del(
		uint64_t	key)
	{
		m_map_latch.enter(0, 0, __FILE__, __LINE__);

		m_map.erase(key);

		m_map_latch.exit();
	}

	void
	inc(
		uint64_t	key)
	{
		m_map_latch.enter(0, 0, __FILE__, __LINE__);

		map_t::iterator	it = m_map.find(key);

		if (it != m_map.end()) {
			++it->second;
		} else {
			m_map.insert(map_t::value_type(key, 1));
		}

		m_map_latch.exit();
	}

	void
	dec(
		uint64_t	key)
	{
		m_map_latch.enter(0, 0, __FILE__, __LINE__);

		map_t::iterator	it = m_map.find(key);

		if (it != m_map.end()) {
			--it->second;
		} else {
			m_map.insert(map_t::value_type(key, -1));
		}

		m_map_latch.exit();
	}

#ifdef UT_HASH_IMPLEMENT_PRINT_STATS
	void
	print_stats()
	{
	}
#endif /* UT_HASH_IMPLEMENT_PRINT_STATS */

private:
	map_t				m_map;
	mutable OSTrackMutex<NoPolicy>	m_map_latch;
};

#elif defined(TEST_TBB)

class tbb_hash_t : public ut_hash_interface_t {
public:
	typedef	uint64_t				key_t;
	typedef	int64_t					val_t;
	typedef tbb::concurrent_hash_map<key_t, val_t>	map_t;

	/** Constructor. */
	tbb_hash_t()
	{
	}

	/** Destructor. */
	~tbb_hash_t()
	{
	}

	int64_t
	get(
		uint64_t	key) const
	{
		map_t::const_accessor	a;

		if (m_map.find(a, key)) {
			return(a->second);
		}

		return(NOT_FOUND);
	}

	void
	set(
		uint64_t	key,
		int64_t		val)
	{
		map_t::accessor	a;

		if (m_map.insert(a, map_t::value_type(key, val))) {
			/* Insert succeeded, do nothing. */
		} else {
			/* A tuple with the given key already exists,
			overwrite its value. */
			a->second = val;
		}
	}

	void
	del(
		uint64_t	key)
	{
		m_map.erase(key);
	}

	void
	inc(
		uint64_t	key)
	{
		delta(key, 1);
	}

	void
	dec(
		uint64_t	key)
	{
		delta(key, -1);
	}

#ifdef UT_HASH_IMPLEMENT_PRINT_STATS
	void
	print_stats()
	{
	}
#endif /* UT_HASH_IMPLEMENT_PRINT_STATS */

private:
	void
	delta(
		uint64_t	key,
		int64_t		delta)
	{
		map_t::accessor	a;

		if (m_map.insert(a, map_t::value_type(key, delta))) {
			/* Insert succeeded because a tuple with this key
			did not exist before, do nothing. */
		} else {
			/* A tuple with the given key already exists,
			apply the delta to its value. */
			os_atomic_increment_uint64(
				static_cast<uint64_t*>(&a->second),
				delta);
		}
	}

	map_t	m_map;
};
#endif

/** Generate a key to use in the (key, value) tuples.
@param[in]	i		some sequential number
@param[in]	extra_bits	extra bits to OR into the result
@return a key, derived from 'i' and 'extra_bits' */
inline
uint64_t
key_gen(
	size_t		i,
	uint64_t	extra_bits)
{
	return((i * 7 + 3) | extra_bits);
}

/** Generate a value to use in the (key, value) tuples.
@param[in]	i	some sequential number
@return a value derived from 'i' */
inline
int64_t
val_from_i(
	size_t	i)
{
	/* Make sure that the returned value is big enough, so that a few
	decrements don't make it negative. */
	return(i * 13 + 10000);
}

/** Insert some tuples in the hash, generating their keys and values
@param[in,out]	hash		hash into which to insert
@param[in]	n_elements	number of elements to insert
@param[in]	key_extra_bits	extra bits to use for key generation */
void
hash_insert(
	ut_hash_interface_t*	hash,
	size_t			n_elements,
	uint64_t		key_extra_bits)
{
	for (size_t i = 0; i < n_elements; i++) {
		hash->set(key_gen(i, key_extra_bits), val_from_i(i));
	}
}

/** Delete the tuples from the hash, inserted by hash_insert(), when called
with the same arguments.
@param[in,out]	hash		hash from which to delete
@param[in]	n_elements	number of elements to delete
@param[in]	key_extra_bits	extra bits to use for key generation */
void
hash_delete(
	ut_hash_interface_t*	hash,
	size_t			n_elements,
	uint64_t		key_extra_bits)
{
	for (size_t i = 0; i < n_elements; i++) {
		hash->del(key_gen(i, key_extra_bits));
	}
}

/** Check that the tuples inserted by hash_insert() are present in the hash.
@param[in]	hash		hash to check
@param[in]	n_elements	number of elements inserted by hash_insert()
@param[in]	key_extra_bits	extra bits that were given to hash_insert() */
void
hash_check_inserted(
	const ut_hash_interface_t*	hash,
	size_t				n_elements,
	uint64_t			key_extra_bits)
{
	for (size_t i = 0; i < n_elements; i++) {
		const uint64_t	key = key_gen(i, key_extra_bits);

		ASSERT_EQ(val_from_i(i), hash->get(key));
	}
}

/** Check that the tuples deleted by hash_delete() are missing from the hash.
@param[in]	hash		hash to check
@param[in]	n_elements	number of elements deleted by hash_delete()
@param[in]	key_extra_bits	extra bits that were given to hash_delete() */
void
hash_check_deleted(
	const ut_hash_interface_t*	hash,
	size_t				n_elements,
	uint64_t			key_extra_bits)
{
	for (size_t i = 0; i < n_elements; i++) {
		const uint64_t	key = key_gen(i, key_extra_bits);

		const int64_t	not_found = ut_hash_interface_t::NOT_FOUND;

		ASSERT_EQ(not_found, hash->get(key));
	}
}

class ut0lock_free_hash : public ::testing::Test {
public:
	static
	void
	SetUpTestCase()
	{
		srv_max_n_threads = 1024;

		sync_check_init();
		os_thread_init();
	}

	static
	void
	TearDownTestCase()
	{
		os_thread_free();
		sync_check_close();
	}
};

TEST_F(ut0lock_free_hash, single_threaded)
{
#ifdef HAVE_UT_CHRONO_T
	ut_chrono_t	chrono("single threaded");
#endif /* HAVE_UT_CHRONO_T */

#if defined(TEST_STD_MAP) || defined(TEST_STD_UNORDERED_MAP)
	ut_hash_interface_t*	hash = new std_hash_t();
#elif defined(TEST_TBB)
	ut_hash_interface_t*	hash = new tbb_hash_t();
#else
	ut_hash_interface_t*	hash = new ut_lock_free_hash_t(1048576, true);
#endif

	const size_t	n_elements = 16 * 1024;

	hash_insert(hash, n_elements, 0);

	hash_check_inserted(hash, n_elements, 0);

	hash_delete(hash, n_elements, 0);

	hash_check_deleted(hash, n_elements, 0);

	hash_insert(hash, n_elements, 0);

	hash_check_inserted(hash, n_elements, 0);

	const size_t	n_iter = 512;

	for (size_t it = 0; it < n_iter; it++) {
		/* Increment the values of some and decrement of others. */
		for (size_t i = 0; i < n_elements; i++) {

			const bool	should_inc = i % 2 == 0;
			const uint64_t	key = key_gen(i, 0);

			/* Inc/dec from 0 to 9 times, depending on 'i'. */
			for (size_t j = 0; j < i % 10; j++) {
				if (should_inc) {
					hash->inc(key);
				} else {
					hash->dec(key);
				}
			}
		}
	}

	/* Check that increment/decrement was done properly. */
	for (size_t i = 0; i < n_elements; i++) {

		const bool	was_inc = i % 2 == 0;
		const int64_t	delta = (i % 10) * n_iter;

		ASSERT_EQ(val_from_i(i) + (was_inc ? delta : -delta),
			  hash->get(key_gen(i, 0)));
	}

	hash_delete(hash, n_elements, 0);

	hash_check_deleted(hash, n_elements, 0);

	delete hash;
}

/** A thread's parameters. */
struct thread_params_t {
	/** Common hash, accessed by many threads concurrently. */
	ut_hash_interface_t*	hash;

	/** Thread id. Used to derive keys that are private to a given
	thread, whose tuples are accessed only by that thread. */
	uint64_t		thread_id;

	/** Number of common tuples (accessed by all threads) that are inserted
	into the hash before starting the threads. */
	size_t			n_common;

	/** Number of private, per-thread tuples to insert by each thread. */
	size_t			n_priv_per_thread;
};

/** Run a multi threaded test.
@param[in]	label			label used when printing the timing
@param[in]	initial_hash_size	initial number of cells in the hash
@param[in]	n_common		number of common tuples (accessed by
all threads) to insert into the hash before starting up all threads
@param[in]	n_priv_per_thread	number of private, per-thread tuples
to insert by each thread.
@param[in]	n_threads		number of threads to start. Overall
the hash will be filled with n_common + n_threads * n_priv_per_thread tuples
@param[in]	thread_func		function to fire up as a new thread */
static
void
run_multi_threaded(
	const char*		label,
	size_t			initial_hash_size,
	size_t			n_common,
	size_t			n_priv_per_thread,
	size_t			n_threads,
	os_thread_func_t	thread_func)
{
#ifdef HAVE_UT_CHRONO_T
	ut_chrono_t		chrono(label);
#endif /* HAVE_UT_CHRONO_T */

	ut_hash_interface_t*	hash;

#if defined(TEST_STD_MAP) || defined(TEST_STD_UNORDERED_MAP)
	hash = new std_hash_t();
#elif defined(TEST_TBB)
	hash = new tbb_hash_t();
#else
	hash = new ut_lock_free_hash_t(initial_hash_size, true);
#endif

	thread_params_t*	params = new thread_params_t[n_threads];

	hash_insert(hash, n_common, 0);

	for (uintptr_t i = 0; i < n_threads; i++) {
		params[i].hash = hash;
		/* Avoid thread_id == 0 because that will collide with the
		shared tuples, thus use 'i + 1' instead of 'i'. */
		params[i].thread_id = i + 1;
		params[i].n_common = n_common;
		params[i].n_priv_per_thread = n_priv_per_thread;

		os_thread_create(thread_func, &params[i], NULL);
	}

	/* Wait for all threads to exit. */
	mutex_enter(&thread_mutex);
	while (os_thread_count > 0) {
		mutex_exit(&thread_mutex);
		os_thread_sleep(100000 /* 0.1 sec */);
		mutex_enter(&thread_mutex);
	}
	mutex_exit(&thread_mutex);

	hash_check_inserted(hash, n_common, 0);

#ifdef UT_HASH_IMPLEMENT_PRINT_STATS
	hash->print_stats();
#endif /* UT_HASH_IMPLEMENT_PRINT_STATS */

	delete[] params;

	delete hash;
}

/** Hammer a common hash with inc(), dec() and set(), 100% writes.
The inc()/dec() performed on the common keys will net to 0 when this thread
ends. It also inserts some tuples with keys that are unique to this thread.
@param[in]	arg	thread arguments */
extern "C"
os_thread_ret_t
DECLARE_THREAD(thread_0r100w)(
	void*	arg)
{
	const thread_params_t*	p = static_cast<const thread_params_t*>(arg);
	const uint64_t		key_extra_bits = p->thread_id << 32;

	hash_insert(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_check_inserted(p->hash, p->n_priv_per_thread, key_extra_bits);

	const size_t	n_iter = 512 * 4096 / p->n_common;

	for (size_t i = 0; i < n_iter; i++) {
		for (size_t j = 0; j < p->n_common; j++) {
			const uint64_t	key = key_gen(j, 0);

			p->hash->inc(key);
			p->hash->inc(key);
			p->hash->inc(key);

			p->hash->dec(key);
			p->hash->inc(key);

			p->hash->dec(key);
			p->hash->dec(key);
			p->hash->dec(key);
		}

		for (size_t j = 0; j < p->n_priv_per_thread; j++) {
			const uint64_t	key = key_gen(j, key_extra_bits);

			for (size_t k = 0; k < 4; k++) {
				p->hash->inc(key);
				p->hash->dec(key);
				p->hash->inc(key);
				p->hash->dec(key);
			}
		}
	}

	hash_check_inserted(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_delete(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_check_deleted(p->hash, p->n_priv_per_thread, key_extra_bits);

	os_thread_exit(NULL);

	OS_THREAD_DUMMY_RETURN;
}

TEST_F(ut0lock_free_hash, multi_threaded_0r100w)
{
	run_multi_threaded(
		"multi threaded,   0% read, 100% write, many keys" /* label */,
		1024 * 32 /* initial hash size */,
		4096 /* n_common */,
		256 /* n_priv_per_thread */,
		64 /* n_threads */,
		reinterpret_cast<os_thread_func_t>(thread_0r100w) /* thr func */
	);
}

TEST_F(ut0lock_free_hash, multi_threaded_0r100w_few_keys)
{
	run_multi_threaded(
		"multi threaded,   0% read, 100% write,  few keys" /* label */,
		1024 * 32 /* initial hash size */,
		16 /* n_common */,
		0 /* n_priv_per_thread */,
		64 /* n_threads */,
		reinterpret_cast<os_thread_func_t>(thread_0r100w) /* thr func */
	);
}

TEST_F(ut0lock_free_hash, multi_threaded_0r100w_grow)
{
	run_multi_threaded(
		"multi threaded,   0% read, 100% write, arraygrow" /* label */,
		1024 /* initial hash size */,
		4096 /* n_common */,
		256 /* n_priv_per_thread */,
		64, /* n_threads */
		reinterpret_cast<os_thread_func_t>(thread_0r100w) /* thr func */
	);
}

/** Hammer a common hash with get(), inc(), dec() and set(), 50% reads and
50% writes. The inc()/dec() performed on the common keys will net to 0 when
this thread ends. It also inserts some tuples with keys that are unique to
this thread.
@param[in]	arg	thread arguments */
extern "C"
os_thread_ret_t
DECLARE_THREAD(thread_50r50w)(
	void*	arg)
{
	const thread_params_t*	p = static_cast<const thread_params_t*>(arg);
	const uint64_t		key_extra_bits = p->thread_id << 32;

	hash_insert(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_check_inserted(p->hash, p->n_priv_per_thread, key_extra_bits);

	const size_t	n_iter = 512;

	for (size_t i = 0; i < n_iter; i++) {
		for (size_t j = 0; j < p->n_common; j++) {
			const uint64_t	key_write = key_gen(j, 0);
			/* Make 1/4 of the reads access possibly nonexisting
			tuples. */
			const uint64_t	key_read = key_gen(j + p->n_common / 4,
							   0);

			p->hash->get(key_read);

			p->hash->inc(key_write);
			p->hash->get(key_read);
			p->hash->inc(key_write);

			p->hash->dec(key_write);
			p->hash->get(key_read);
			p->hash->dec(key_write);

			p->hash->get(key_read);
		}

		for (size_t j = 0; j < p->n_priv_per_thread; j++) {
			const uint64_t	key_write = key_gen(j, key_extra_bits);
			/* Make 1/4 of the reads access possibly nonexisting
			tuples. */
			const uint64_t	key_read = key_gen(
				j + p->n_priv_per_thread / 4, key_extra_bits);

			for (size_t k = 0; k < 4; k++) {
				p->hash->inc(key_write);
				p->hash->get(key_read);
				p->hash->dec(key_write);
				p->hash->get(key_read);
			}
		}
	}

	hash_check_inserted(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_delete(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_check_deleted(p->hash, p->n_priv_per_thread, key_extra_bits);

	os_thread_exit(NULL);

	OS_THREAD_DUMMY_RETURN;
}

TEST_F(ut0lock_free_hash, multi_threaded_50r50w)
{
	run_multi_threaded(
		"multi threaded,  50% read,  50% write, many keys" /* label */,
		1024 * 32 /* initial hash size */,
		4096 /* n_common */,
		256 /* n_priv_per_thread */,
		64 /* n_threads */,
		reinterpret_cast<os_thread_func_t>(thread_50r50w) /* thr func */
	);
}

/** Hammer a commmon hash with get()s, 100% reads.
@param[in]	arg	thread arguments */
extern "C"
os_thread_ret_t
DECLARE_THREAD(thread_100r0w)(
	void*	arg)
{
	const thread_params_t*	p = static_cast<const thread_params_t*>(arg);
	const uint64_t		key_extra_bits = p->thread_id << 32;

	hash_insert(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_check_inserted(p->hash, p->n_priv_per_thread, key_extra_bits);

	const size_t	n_iter = 512;

	for (size_t i = 0; i < n_iter; i++) {
		for (size_t j = 0; j < p->n_common; j++) {
			/* Make 1/4 of the reads access possibly nonexisting
			tuples. */
			const uint64_t	key_read = key_gen(j + p->n_common / 4,
							   0);

			p->hash->get(key_read);
			p->hash->get(key_read);
			p->hash->get(key_read);
			p->hash->get(key_read);
			p->hash->get(key_read);
			p->hash->get(key_read);
			p->hash->get(key_read);
			p->hash->get(key_read);
		}

		for (size_t j = 0; j < p->n_priv_per_thread; j++) {
			/* Make 1/4 of the reads access possibly nonexisting
			tuples. */
			const uint64_t	key_read = key_gen(
				j + p->n_priv_per_thread / 4, key_extra_bits);

			for (size_t k = 0; k < 4; k++) {
				p->hash->get(key_read);
				p->hash->get(key_read);
				p->hash->get(key_read);
				p->hash->get(key_read);
			}
		}
	}

	hash_check_inserted(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_delete(p->hash, p->n_priv_per_thread, key_extra_bits);

	hash_check_deleted(p->hash, p->n_priv_per_thread, key_extra_bits);

	os_thread_exit(NULL);

	OS_THREAD_DUMMY_RETURN;
}

TEST_F(ut0lock_free_hash, multi_threaded_100r0w)
{
	run_multi_threaded(
		"multi threaded, 100% read,   0% write, many keys" /* label */,
		1024 * 32 /* initial hash size */,
		4096 /* n_common */,
		256 /* n_priv_per_thread */,
		64 /* n_threads */,
		reinterpret_cast<os_thread_func_t>(thread_100r0w) /* thr func */
	);
}

}
