#ifndef SE_INCL_TLVAR_H
#define SE_INCL_TLVAR_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

#include <vector>
#include <algorithm>

// thread-local storage slots: Win32 TLS, or POSIX thread keys elsewhere
#ifdef PLATFORM_WIN32
typedef DWORD TLSSlot;
inline TLSSlot TLS_Alloc(void)                     { return TlsAlloc(); }
inline void    TLS_Free(TLSSlot slot)              { TlsFree(slot); }
inline void   *TLS_Get(TLSSlot slot)               { return TlsGetValue(slot); }
inline void    TLS_Set(TLSSlot slot, void *pvData) { TlsSetValue(slot, pvData); }
#else
#include <pthread.h>
typedef pthread_key_t TLSSlot;
inline TLSSlot TLS_Alloc(void)                     { pthread_key_t key; pthread_key_create(&key, NULL); return key; }
inline void    TLS_Free(TLSSlot slot)              { pthread_key_delete(slot); }
inline void   *TLS_Get(TLSSlot slot)               { return pthread_getspecific(slot); }
inline void    TLS_Set(TLSSlot slot, void *pvData) { pthread_setspecific(slot, pvData); }
#endif

class TLVarBase {
public:
	virtual void cleanUp() = 0;
	static std::vector<TLVarBase*> m_Vars;
};

template<class T>
class TLVar: public TLVarBase
{
public:
	TLVar( T t )
	{
		m_TlsSlot = TLS_Alloc();
		m_Default = t;
		m_Vars.push_back(this);
	}

	TLVar( const TLVar<T>& t )
	{
		m_TlsSlot = TLS_Alloc();
		m_Default = t.m_Default;
		m_Vars.push_back(this);
	}

	virtual ~TLVar()
	{
		cleanUp();
		TLS_Free( m_TlsSlot);
#ifdef	_DEBUG
		if (m_Vars.size() == 0)
			return;
#endif	// _DEBUG
		std::vector<TLVarBase*>::iterator i = std::find(m_Vars.begin(), m_Vars.end(), this);
		if (i != m_Vars.end())
			m_Vars.erase(i);
	}

	void cleanUp() 
	{
		T* tls= get();
		if (tls)
			delete tls;
	}
	

	T& operator=( const T& t ) 
	{
		T* tls = get();
		if (!tls)
			set(tls = new T(t));
		else
			*tls = t;

		return *tls;
	}

	operator T&() 
	{
		T* tls = get();
		if (!tls)
			set(tls = new T(m_Default));
		
		return *tls;
	}

	//T* operator& () { return (T*)get(); }

protected:
	T *get() const
	{
		return reinterpret_cast<T*>(TLS_Get(m_TlsSlot));
	}

	void set( void* data)
	{
		TLS_Set( m_TlsSlot, reinterpret_cast<void*>(data));
	}

	T m_Default;
	TLSSlot m_TlsSlot;
};

#endif