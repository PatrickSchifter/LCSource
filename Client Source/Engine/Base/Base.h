
/*
 * rcg10042001 In case these don't get defined in the project file, try to
 *   catch them here...
 */
#ifdef _MSC_VER
  #ifndef PLATFORM_WIN32
    #define PLATFORM_WIN32
  #endif

  #ifndef PRAGMA_ONCE
    #define PRAGMA_ONCE
  #endif

  // disable problematic warnings

  #pragma warning(disable: 4251)  // dll interfacing problems
  #pragma warning(disable: 4275)  // dll interfacing problems
  #pragma warning(disable: 4018)  // signed/unsigned mismatch
  #pragma warning(disable: 4244)  // type conversion warnings
  #pragma warning(disable: 4284)  // using -> for UDT
  #pragma warning(disable: 4355)  // 'this' : used in base member initializer list
  #pragma warning(disable: 4660)  // template-class specialization is already instantiated
  #pragma warning(disable: 4723)  // potential divide by 0

  // define engine api exporting declaration specifiers
  #ifdef ENGINE_EXPORTS
    #define ENGINE_API __declspec(dllexport)
  #else
    #define ENGINE_API __declspec(dllimport)

    #ifdef NDEBUG
      #pragma comment(lib, "Engine.lib")
    #else
      #pragma comment(lib, "EngineD.lib")
    #endif
  #endif

#endif  // defined _MSC_VER

#if !defined(_MSC_VER) && !defined(PLATFORM_UNIX)
  #define PLATFORM_UNIX
#endif

// Outside Windows the window, events and input come from SDL3 (Engine/Platform/Platform.h); the
// Windows build uses them only when configured with LC_PLATFORM_SDL.
#if defined(PLATFORM_UNIX) && !defined(LC_PLATFORM_SDL)
  #define LC_PLATFORM_SDL
#endif

// The x86 inline assembly is MSVC syntax for 32-bit x86 only; everywhere else the engine uses
// its C paths. Define USE_PORTABLE_C in the Windows build too to test the C paths there.
#if !defined(_MSC_VER) || !defined(_M_IX86)
  #ifndef USE_PORTABLE_C
    #define USE_PORTABLE_C
  #endif
#endif


#ifdef PLATFORM_UNIX  /* rcg10042001 */
  #define ENGINE_API
#endif

