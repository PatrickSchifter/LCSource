#ifndef __MEMORYPOOLBASE_H__
#define __MEMORYPOOLBASE_H__

#include <assert.h>
#include <boost/pool/pool.hpp>
#include <boost/thread/recursive_mutex.hpp>

template <class T>
class MemoryPoolBase
{
public:
	MemoryPoolBase(void) {}
	virtual ~MemoryPoolBase(void) {}

	void * operator new (size_t size)
	{
		(void)size;
		return (void *)(pool().malloc());
	}

	void * operator new (size_t size, void *p)
	{
		return p;
	}

	void operator delete (void *ptr)
	{
		pool().free( (T *)ptr );
	}

	void operator delete (void *ptr, T *p)
	{
	}

private:
	// built on first use: objects with these pools are created by other files' static
	// initializers (bnf's instance), whose order against a static member's is unspecified
	static boost::pool<>& pool() { static boost::pool<> p(sizeof(T), 32); return p; }
};

//////////////////////////////////////////////////////////////////////////

template <class T>
class MemoryPoolBaseWithMutex
{
public:
	MemoryPoolBaseWithMutex(void) {}
	virtual ~MemoryPoolBaseWithMutex(void) {}

	void * operator new (size_t size)
	{
		(void)size;

		boost::recursive_mutex::scoped_lock lock(mutex());
		return (void *)(pool().malloc());
	}

	void * operator new (size_t size, void *p)
	{
		return p;
	}

	void operator delete (void *ptr)
	{
		boost::recursive_mutex::scoped_lock lock(mutex());
		pool().free( (T *)ptr );
	}

	void operator delete (void *ptr, T *p)
	{
	}

private:
	// built on first use (see MemoryPoolBase)
	static boost::pool<>& pool() { static boost::pool<> p(sizeof(T), 32); return p; }
	static boost::recursive_mutex& mutex() { static boost::recursive_mutex m; return m; }
};

#endif
