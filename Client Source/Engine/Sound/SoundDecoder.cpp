#include "StdH.h"

#include <Engine/Sound/SoundDecoder.h>
#include <Engine/Base/Stream.h>
#include <Engine/Base/Console.h>
#include <Engine/Base/ErrorReporting.h>
#include <Engine/Base/FileName.h>
#include <Engine/Base/Unzip.h>
#include <Engine/Base/Translation.h>
#include <Engine/Math/Functions.h>

// generic function called if a dll function is not found
static void FailFunction_t(const char *strName) {
  ThrowF_t(TRANS("Function %s not found."), strName);
}


// ------------------------------------ AMP11

// amp11lib vars
extern BOOL _bAMP11Enabled = FALSE;
#ifdef PLATFORM_WIN32
static HINSTANCE _hAmp11lib = NULL;

// amp11lib types
typedef signed char ALsint8;
typedef unsigned char ALuint8;
typedef signed short ALsint16;
typedef unsigned short ALuint16;
typedef signed int ALsint32;
typedef unsigned int ALuint32;
typedef signed int ALsize;
typedef int ALbool;
typedef float ALfloat;
#define ALtrue  1
#define ALfalse 0
typedef ALsint32 ALhandle;

// define amp11lib function pointers
#define DLLFUNCTION(dll, output, name, inputs, params, required) \
  output (__stdcall *p##name) inputs = NULL;
#include "al_functions.h"
#undef DLLFUNCTION

static void AMP11_SetFunctionPointers_t(void) {
  const char *strName;
  // get amp11lib function pointers
  #define DLLFUNCTION(dll, output, name, inputs, params, required) \
    strName = "_" #name "@" #params;  \
    p##name = (output (__stdcall*) inputs) GetProcAddress( _hAmp11lib, strName); \
    if(p##name == NULL) FailFunction_t(strName);
  #include "al_functions.h"
  #undef DLLFUNCTION
}

static void AMP11_ClearFunctionPointers(void) {
  // clear amp11lib function pointers
  #define DLLFUNCTION(dll, output, name, inputs, params, required) p##name = NULL;
  #include "al_functions.h"
  #undef DLLFUNCTION
}

class CDecodeData_MPEG {
public:
  ALhandle mpeg_hMainFile; // mainfile handle if using subfile
  ALhandle mpeg_hFile;     // file handle
  ALhandle mpeg_hDecoder;  // the decoder handle
  FLOAT mpeg_fSecondsLen;  // length of sound in seconds
  WAVEFORMATEX mpeg_wfeFormat; // format of sound
};

#else  // PLATFORM_WIN32

// Outside Windows there is no amp11lib: MP3 is decoded by dr_mp3 (3rdparty/dr_libs), compiled in
// here. It reads the file through the same window as the Ogg decoder below (offset and size of
// the stored file inside a .gro archive).
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include <3rdparty/dr_libs/dr_mp3.h>

class CDecodeData_MPEG {
public:
  FILE *mpeg_fFile;      // the stdio file that mp3 is in
  SLONG mpeg_slOffset;   // offset where the mp3 starts in the file (!=0 for mp3s in zip)
  SLONG mpeg_slSize;     // size of mp3 in the file (!=filesize for mp3s in zip)
  drmp3 *mpeg_pmp3;      // the decoder (NULL if not open)
  UBYTE mpeg_aubCarry[4];  // rest of a sample frame that the last Decode() could not return
  INDEX mpeg_ctCarry;      // bytes in mpeg_aubCarry
  WAVEFORMATEX mpeg_wfeFormat; // format of sound
};

// mp3 file reading callbacks
static size_t mpeg_read_func( void *pUserData, void *pBufferOut, size_t bytesToRead)
{
  CDecodeData_MPEG *pmpeg = (CDecodeData_MPEG *)pUserData;
  // read at most up to the end of the mp3
  const SLONG slCurrentPos = (SLONG)ftell(pmpeg->mpeg_fFile)-pmpeg->mpeg_slOffset;
  const SLONG slSizeLeft = ClampDn(pmpeg->mpeg_slSize-slCurrentPos, (SLONG)0);
  const size_t ctToRead = Min( bytesToRead, (size_t)slSizeLeft);
  if (ctToRead==0) {
    return 0;
  }
  return fread(pBufferOut, 1, ctToRead, pmpeg->mpeg_fFile);
}

static drmp3_bool32 mpeg_seek_func( void *pUserData, int offset, drmp3_seek_origin origin)
{
  CDecodeData_MPEG *pmpeg = (CDecodeData_MPEG *)pUserData;
  SLONG slPos = offset;
  if (origin==DRMP3_SEEK_CUR) {
    slPos += (SLONG)ftell(pmpeg->mpeg_fFile)-pmpeg->mpeg_slOffset;
  } else if (origin==DRMP3_SEEK_END) {
    slPos += pmpeg->mpeg_slSize;
  }
  if (slPos<0 || slPos>pmpeg->mpeg_slSize) {
    return DRMP3_FALSE;
  }
  return fseek(pmpeg->mpeg_fFile, pmpeg->mpeg_slOffset+slPos, SEEK_SET)==0;
}

static drmp3_bool32 mpeg_tell_func( void *pUserData, drmp3_int64 *pCursor)
{
  CDecodeData_MPEG *pmpeg = (CDecodeData_MPEG *)pUserData;
  *pCursor = ftell(pmpeg->mpeg_fFile)-pmpeg->mpeg_slOffset;
  return DRMP3_TRUE;
}

#endif // PLATFORM_WIN32


// ------------------------------------ Ogg Vorbis

//#include <vorbis\vorbisfile.h>  // we define needed stuff ourselves, and ignore the rest

// vorbis vars
extern BOOL _bOVEnabled = FALSE;
#ifdef PLATFORM_WIN32
static HINSTANCE _hOV = NULL;

#define OV_FALSE      -1  
#define OV_EOF        -2
#define OV_HOLE       -3

#define OV_EREAD      -128
#define OV_EFAULT     -129
#define OV_EIMPL      -130
#define OV_EINVAL     -131
#define OV_ENOTVORBIS -132
#define OV_EBADHEADER -133
#define OV_EVERSION   -134
#define OV_ENOTAUDIO  -135
#define OV_EBADPACKET -136
#define OV_EBADLINK   -137
#define OV_ENOSEEK    -138

// vorbis types
typedef __int64 ogg_int64_t;
typedef struct {
  size_t (*read_func)  (void *ptr, size_t size, size_t nmemb, void *datasource);
  int    (*seek_func)  (void *datasource, ogg_int64_t offset, int whence);
  int    (*close_func) (void *datasource);
  long   (*tell_func)  (void *datasource);
} ov_callbacks;

struct OggVorbis_File {
  // don't wanna know whats inside 
  UBYTE dummy[2048];   // last time checked, the actual size was 720 (ogg vorbis version 1.0rc1)
};
struct vorbis_info {
  int version;
  int channels;
  long rate;
  
  long bitrate_upper;
  long bitrate_nominal;
  long bitrate_lower;

  // don't want to know the rest...
  //..................
};

// define vorbis function pointers
#define DLLFUNCTION(dll, output, name, inputs, params, required) \
  output (__cdecl *p##name) inputs = NULL;
#include "ov_functions.h"
#undef DLLFUNCTION

static void OV_SetFunctionPointers_t(void) {
  const char *strName;
  // get vo function pointers
  #define DLLFUNCTION(dll, output, name, inputs, params, required) \
    strName = #name ;  \
    p##name = (output (__cdecl *) inputs) GetProcAddress( _hOV, strName); \
    if(p##name == NULL) FailFunction_t(strName);
  #include "ov_functions.h"
  #undef DLLFUNCTION
}
static void OV_ClearFunctionPointers(void) {
  // clear vo function pointers
  #define DLLFUNCTION(dll, output, name, inputs, params, required) p##name = NULL;
  #include "ov_functions.h"
  #undef DLLFUNCTION
}

#else  // PLATFORM_WIN32

// Outside Windows libvorbisfile is linked in (Homebrew's libvorbis): its own header and types,
// and the functions called directly under the names the Windows code gets from vorbisfile.dll.
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#define pov_clear          ov_clear
#define pov_open_callbacks ov_open_callbacks
#define pov_read           ov_read
#define pov_info           ov_info

#endif // PLATFORM_WIN32

class CDecodeData_OGG {
public:
  FILE *ogg_fFile;      // the stdio file that ogg is in
  SLONG ogg_slOffset;   // offset where the ogg starts in the file (!=0 for oggs in zip)
  SLONG ogg_slSize;     // size of ogg in the file (!=filesize for oggs in zip)
  OggVorbis_File *ogg_vfVorbisFile;  // the decoder file
  WAVEFORMATEX ogg_wfeFormat; // format of sound
};

// ogg file reading callbacks
//

static size_t ogg_read_func  (void *ptr, size_t size, size_t nmemb, void *datasource)
{
  CDecodeData_OGG *pogg = (CDecodeData_OGG *)datasource;
  // calculate how much can be read at most
  SLONG slToRead = size*nmemb;
  SLONG slCurrentPos = ftell(pogg->ogg_fFile)-pogg->ogg_slOffset;
  SLONG slSizeLeft = ClampDn(pogg->ogg_slSize-slCurrentPos, 0L);
  slToRead = ClampUp(slToRead, slSizeLeft);

  // rounded down to the block size
  slToRead/=size;
  slToRead*=size;
  // if there is nothing to read
  if (slToRead<=0) {
    return 0;
  }
  return fread(ptr, size, slToRead/size, pogg->ogg_fFile);
}

static int ogg_seek_func  (void *datasource, ogg_int64_t offset, int whence)
{
  return -1;
/*  !!!! seeking is evil with vorbisfile 1.0RC2
  CDecodeData_OGG *pogg = (CDecodeData_OGG *)datasource;
  SLONG slCurrentPos = ftell(pogg->ogg_fFile)-pogg->ogg_slOffset;
  if (whence==SEEK_CUR) {
    return fseek(pogg->ogg_fFile, offset, SEEK_CUR);
  } else if (whence==SEEK_END) {
    return fseek(pogg->ogg_fFile, pogg->ogg_slOffset+pogg->ogg_slSize-offset, SEEK_SET);
  } else {
    ASSERT(whence==SEEK_SET);
    return fseek(pogg->ogg_fFile, pogg->ogg_slOffset+offset, SEEK_SET);
  }
*/
}

static int ogg_close_func (void *datasource)
{
  return 0;
/* !!!! closing is evil with vorbisfile 1.0RC2
  CDecodeData_OGG *pogg = (CDecodeData_OGG *)datasource;
  fclose(pogg->ogg_fFile);
  */
}
static long ogg_tell_func  (void *datasource)
{
  return -1;
/*  !!!! seeking is evil with vorbisfile 1.0RC2
  CDecodeData_OGG *pogg = (CDecodeData_OGG *)datasource;
  ftell(pogg->ogg_fFile)-pogg->ogg_slOffset;
  */
}

static ov_callbacks ovcCallbacks = {
  ogg_read_func,
  ogg_seek_func,
  ogg_close_func,
  ogg_tell_func,
};


// initialize/end the decoding support engine(s)
void CSoundDecoder::InitPlugins(void)
{
#ifndef PLATFORM_WIN32
  // both decoders are part of the executable
  _bOVEnabled = TRUE;
  CPrintF(TRANS("  libvorbisfile linked, ogg playing enabled\n"));
  _bAMP11Enabled = TRUE;
  CPrintF(TRANS("  dr_mp3 compiled in, mpx playing enabled\n"));
#else
  try {
    // load vorbis
    if (_hOV==NULL) {
#ifndef NDEBUG
//안태훈 수정 시작	//(Exception Check & Fix)(0.1)
  #define VORBISLIB "vorbisfile.dll"
//  #define VORBISLIB "vorbisfile_d.dll"
//안태훈 수정 끝	//(Exception Check & Fix)(0.1)
#else
  #define VORBISLIB "vorbisfile.dll"
#endif
      _hOV = ::LoadLibrary(VORBISLIB);
    }
    if( _hOV == NULL) {
      ThrowF_t(TRANS("Cannot load vorbisfile.dll."));
    }
    // prepare function pointers
    OV_SetFunctionPointers_t();

    // if all successful, enable mpx playing
    _bOVEnabled = TRUE;
    CPrintF(TRANS("  vorbisfile.dll loaded, ogg playing enabled\n"));

  } catch (char *strError) {
    CPrintF(TRANS("OGG playing disabled: %s\n"), strError);
  }

  try {
    // load amp11lib
    if (_hAmp11lib==NULL) {
      _hAmp11lib = ::LoadLibrary( "amp11lib.dll");
    }
    if( _hAmp11lib == NULL) {
      ThrowF_t(TRANS("Cannot load amp11lib.dll."));
    }
    // prepare function pointers
    AMP11_SetFunctionPointers_t();

    // initialize amp11lib before calling any of its functions
    palInitLibrary();

    // if all successful, enable mpx playing
    _bAMP11Enabled = TRUE;
    CPrintF(TRANS("  amp11lib.dll loaded, mpx playing enabled\n"));

  } catch (char *strError) {
    CPrintF(TRANS("MPX playing disabled: %s\n"), strError);
  }
#endif // PLATFORM_WIN32
}

void CSoundDecoder::EndPlugins(void)
{
#ifndef PLATFORM_WIN32
  _bAMP11Enabled = FALSE;
  _bOVEnabled = FALSE;
#else
  // cleanup amp11lib when not needed anymore
  if (_bAMP11Enabled) {
    palEndLibrary();
    AMP11_ClearFunctionPointers();
    FreeLibrary(_hAmp11lib);
    _hAmp11lib = NULL;
    _bAMP11Enabled = FALSE;
  }

  // cleanup vorbis when not needed anymore
  if (_bOVEnabled) {
    OV_ClearFunctionPointers();
    FreeLibrary(_hOV);
    _hOV = NULL;
    _bOVEnabled = FALSE;
  }
#endif // PLATFORM_WIN32
}

// decoder that streams from file
CSoundDecoder::CSoundDecoder(const CTFileName &fnm, UBYTE *pubHeader/*=NULL*/, SLONG slHeaderSize/*=0*/)
{
  sdc_pogg = NULL;
  sdc_pmpeg = NULL;

  CTFileName fnmExpanded;
  INDEX iFileType = ExpandFilePath(EFP_READ, fnm, fnmExpanded);

  // if ogg
  if (fnmExpanded.FileExt()==".ogg") {
    if (!_bOVEnabled) {
      return;
    }
    sdc_pogg = new CDecodeData_OGG;
    sdc_pogg->ogg_fFile = NULL;
    sdc_pogg->ogg_vfVorbisFile = NULL;
    sdc_pogg->ogg_slOffset = 0;
    sdc_pogg->ogg_slSize = 0;
    INDEX iZipHandle = 0;

    try {
      // if in zip
      if (iFileType==EFP_BASEZIP || iFileType==EFP_MODZIP) {
        // open it
        iZipHandle = UNZIPOpen_t(fnmExpanded);

        CTFileName fnmZip;
        SLONG slOffset;
        SLONG slSizeCompressed;
        SLONG slSizeUncompressed;
        BOOL bCompressed;
        UNZIPGetFileInfo(iZipHandle, fnmZip, slOffset, slSizeCompressed, slSizeUncompressed, bCompressed);

        // if compressed
        if (bCompressed) {
          ThrowF_t(TRANS("encoded audio in archives must not be compressed!\n"));
        }
        // open ogg file
        sdc_pogg->ogg_fFile = fopen(fnmZip, "rb");
        // if error
        if (sdc_pogg->ogg_fFile==0) {
          ThrowF_t(TRANS("cannot open archive '%s'"), (const char*)fnmZip);
        }
        // remember offset and size
        sdc_pogg->ogg_slOffset = slOffset;
        sdc_pogg->ogg_slSize = slSizeUncompressed;
        fseek(sdc_pogg->ogg_fFile, slOffset, SEEK_SET);

      // if not in zip
      } else if (iFileType==EFP_FILE) {
        // open ogg file
        sdc_pogg->ogg_fFile = fopen(fnmExpanded, "rb");
        // if error
        if (sdc_pogg->ogg_fFile==0) {
          ThrowF_t(TRANS("cannot open encoded audio file"));
        }
        // remember offset and size
        sdc_pogg->ogg_slOffset = 0;

        fseek(sdc_pogg->ogg_fFile, 0, SEEK_END);
        sdc_pogg->ogg_slSize = ftell(sdc_pogg->ogg_fFile);
        fseek(sdc_pogg->ogg_fFile, 0, SEEK_SET);
      // if not found
      } else {
        ThrowF_t(TRANS("file not found"));
      }

      // initialize decoder
      sdc_pogg->ogg_vfVorbisFile = new OggVorbis_File;
      int iRes = pov_open_callbacks(sdc_pogg, sdc_pogg->ogg_vfVorbisFile, NULL, 0, ovcCallbacks);

      // if error
      if (iRes!=0) {
        ThrowF_t(TRANS("cannot open ogg decoder"));
      }

      // get info on the file
      vorbis_info *pvi = pov_info(sdc_pogg->ogg_vfVorbisFile, -1);

      // remember it's format
      WAVEFORMATEX form;
      form.wFormatTag=WAVE_FORMAT_PCM;
      form.nChannels=pvi->channels;
      form.nSamplesPerSec=pvi->rate;
      form.wBitsPerSample=16;
      form.nBlockAlign=form.nChannels*form.wBitsPerSample/8;
      form.nAvgBytesPerSec=form.nSamplesPerSec*form.nBlockAlign;
      form.cbSize=0;

      // check for stereo
      if (pvi->channels!=2) {
        ThrowF_t(TRANS("not stereo"));
      }
    
      sdc_pogg->ogg_wfeFormat = form;

    } catch (char*strError) {
      CPrintF(TRANS("Cannot open encoded audio '%s' for streaming: %s\n"), (const char*)fnm, (const char*)strError);
      if (sdc_pogg->ogg_vfVorbisFile!=NULL) {
        delete sdc_pogg->ogg_vfVorbisFile;
        sdc_pogg->ogg_vfVorbisFile = NULL;
      }
      if (sdc_pogg->ogg_fFile!=NULL) {
        fclose(sdc_pogg->ogg_fFile);
        sdc_pogg->ogg_fFile = NULL;
      }
      if (iZipHandle!=0) {
        UNZIPClose(iZipHandle);
      }
      Clear();
      return;
    }
    if (iZipHandle!=0) {
      UNZIPClose(iZipHandle);
    }

  // if mp3
  } else if (fnmExpanded.FileExt()==".mp3") {

    if (!_bAMP11Enabled) {
      return;
    }

#ifdef PLATFORM_WIN32
    sdc_pmpeg = new CDecodeData_MPEG;
    sdc_pmpeg->mpeg_hMainFile = 0;
    sdc_pmpeg->mpeg_hFile = 0;
    sdc_pmpeg->mpeg_hDecoder = 0;
    INDEX iZipHandle = 0;

    try {
      // if in zip
      if (iFileType==EFP_BASEZIP || iFileType==EFP_MODZIP) {
        // open it
        iZipHandle = UNZIPOpen_t(fnmExpanded);

        CTFileName fnmZip;
        SLONG slOffset;
        SLONG slSizeCompressed;
        SLONG slSizeUncompressed;
        BOOL bCompressed;
        UNZIPGetFileInfo(iZipHandle, fnmZip, slOffset, slSizeCompressed, slSizeUncompressed, bCompressed);

        // if compressed
        if (bCompressed) {
          ThrowF_t(TRANS("encoded audio in archives must not be compressed!\n"));
        }
        // open the zip file
        sdc_pmpeg->mpeg_hMainFile = palOpenInputFile(fnmZip);
        // if error
        if (sdc_pmpeg->mpeg_hMainFile==0) {
          ThrowF_t(TRANS("cannot open archive '%s'"), (const char*)fnmZip);
        }
        // open the subfile
        sdc_pmpeg->mpeg_hFile = palOpenSubFile(sdc_pmpeg->mpeg_hMainFile, slOffset, slSizeUncompressed);
        // if error
        if (sdc_pmpeg->mpeg_hFile==0) {
          ThrowF_t(TRANS("cannot open encoded audio file"));
        }

      // if not in zip
      } else if (iFileType==EFP_FILE) {
        // open mpx file
        sdc_pmpeg->mpeg_hFile = palOpenInputFile(fnmExpanded);
        // if error
        if (sdc_pmpeg->mpeg_hFile==0) {
          ThrowF_t(TRANS("cannot open mpx file"));
        }
      // if not found
      } else {
        ThrowF_t(TRANS("file not found"));
      }

      // get info on the file
      int layer, ver, freq, stereo, rate;
      if (!palGetMPXHeader(sdc_pmpeg->mpeg_hFile, &layer, &ver, &freq, &stereo, &rate)) {
        ThrowF_t(TRANS("not a valid mpeg audio file."));
      }

      // remember it's format
      WAVEFORMATEX form;
      form.wFormatTag=WAVE_FORMAT_PCM;
      form.nChannels=stereo?2:1;
      form.nSamplesPerSec=freq;
      form.wBitsPerSample=16;
      form.nBlockAlign=form.nChannels*form.wBitsPerSample/8;
      form.nAvgBytesPerSec=form.nSamplesPerSec*form.nBlockAlign;
      form.cbSize=0;

      // check for stereo
      if (!stereo) {
        ThrowF_t(TRANS("not stereo"));
      }
    
      sdc_pmpeg->mpeg_wfeFormat = form;

      // initialize decoder
      sdc_pmpeg->mpeg_hDecoder = palOpenDecoder(sdc_pmpeg->mpeg_hFile);

      // if error
      if (sdc_pmpeg->mpeg_hDecoder==0) {
        ThrowF_t(TRANS("cannot open mpx decoder"));
      }
    } catch (char*strError) {
      CPrintF(TRANS("Cannot open mpx '%s' for streaming: %s\n"), (const char*)fnm, (const char*)strError);
      if (iZipHandle!=0) {
        UNZIPClose(iZipHandle);
      }
      Clear();
      return;
    }

    if (iZipHandle!=0) {
      UNZIPClose(iZipHandle);
    }
    sdc_pmpeg->mpeg_fSecondsLen = palDecGetLen(sdc_pmpeg->mpeg_hDecoder);

#else  // PLATFORM_WIN32
    sdc_pmpeg = new CDecodeData_MPEG;
    sdc_pmpeg->mpeg_fFile = NULL;
    sdc_pmpeg->mpeg_pmp3 = NULL;
    sdc_pmpeg->mpeg_slOffset = 0;
    sdc_pmpeg->mpeg_slSize = 0;
    sdc_pmpeg->mpeg_ctCarry = 0;
    INDEX iZipHandle = 0;

    try {
      // if in zip
      if (iFileType==EFP_BASEZIP || iFileType==EFP_MODZIP) {
        // open it
        iZipHandle = UNZIPOpen_t(fnmExpanded);

        CTFileName fnmZip;
        SLONG slOffset;
        SLONG slSizeCompressed;
        SLONG slSizeUncompressed;
        BOOL bCompressed;
        UNZIPGetFileInfo(iZipHandle, fnmZip, slOffset, slSizeCompressed, slSizeUncompressed, bCompressed);

        // if compressed
        if (bCompressed) {
          ThrowF_t(TRANS("encoded audio in archives must not be compressed!\n"));
        }
        // open mp3 file
        sdc_pmpeg->mpeg_fFile = fopen(fnmZip, "rb");
        // if error
        if (sdc_pmpeg->mpeg_fFile==0) {
          ThrowF_t(TRANS("cannot open archive '%s'"), (const char*)fnmZip);
        }
        // remember offset and size
        sdc_pmpeg->mpeg_slOffset = slOffset;
        sdc_pmpeg->mpeg_slSize = slSizeUncompressed;
        fseek(sdc_pmpeg->mpeg_fFile, slOffset, SEEK_SET);

      // if not in zip
      } else if (iFileType==EFP_FILE) {
        // open mpx file
        sdc_pmpeg->mpeg_fFile = fopen(fnmExpanded, "rb");
        // if error
        if (sdc_pmpeg->mpeg_fFile==0) {
          ThrowF_t(TRANS("cannot open mpx file"));
        }
        // remember offset and size
        sdc_pmpeg->mpeg_slOffset = 0;

        fseek(sdc_pmpeg->mpeg_fFile, 0, SEEK_END);
        sdc_pmpeg->mpeg_slSize = (SLONG)ftell(sdc_pmpeg->mpeg_fFile);
        fseek(sdc_pmpeg->mpeg_fFile, 0, SEEK_SET);
      // if not found
      } else {
        ThrowF_t(TRANS("file not found"));
      }

      // initialize decoder (it reads the first frame for the format)
      sdc_pmpeg->mpeg_pmp3 = new drmp3;
      if (!drmp3_init(sdc_pmpeg->mpeg_pmp3, mpeg_read_func, mpeg_seek_func, mpeg_tell_func, NULL, sdc_pmpeg, NULL)) {
        delete sdc_pmpeg->mpeg_pmp3;
        sdc_pmpeg->mpeg_pmp3 = NULL;
        ThrowF_t(TRANS("not a valid mpeg audio file."));
      }

      // remember it's format
      WAVEFORMATEX form;
      form.wFormatTag=WAVE_FORMAT_PCM;
      form.nChannels=sdc_pmpeg->mpeg_pmp3->channels;
      form.nSamplesPerSec=sdc_pmpeg->mpeg_pmp3->sampleRate;
      form.wBitsPerSample=16;
      form.nBlockAlign=form.nChannels*form.wBitsPerSample/8;
      form.nAvgBytesPerSec=form.nSamplesPerSec*form.nBlockAlign;
      form.cbSize=0;

      // check for stereo
      if (form.nChannels!=2) {
        ThrowF_t(TRANS("not stereo"));
      }

      sdc_pmpeg->mpeg_wfeFormat = form;
    } catch (char*strError) {
      CPrintF(TRANS("Cannot open mpx '%s' for streaming: %s\n"), (const char*)fnm, (const char*)strError);
      if (iZipHandle!=0) {
        UNZIPClose(iZipHandle);
      }
      Clear();
      return;
    }

    if (iZipHandle!=0) {
      UNZIPClose(iZipHandle);
    }
#endif // PLATFORM_WIN32
  }
}

CSoundDecoder::~CSoundDecoder(void)
{
  Clear();
}

void CSoundDecoder::Clear(void)
{
  if (sdc_pmpeg!=NULL) {
#ifdef PLATFORM_WIN32
    if (sdc_pmpeg->mpeg_hDecoder!=0)  palClose(sdc_pmpeg->mpeg_hDecoder);
    if (sdc_pmpeg->mpeg_hFile!=0)     palClose(sdc_pmpeg->mpeg_hFile);
    if (sdc_pmpeg->mpeg_hMainFile!=0) palClose(sdc_pmpeg->mpeg_hMainFile);

    sdc_pmpeg->mpeg_hMainFile = 0;
    sdc_pmpeg->mpeg_hFile = 0;
    sdc_pmpeg->mpeg_hDecoder = 0;
#else
    if (sdc_pmpeg->mpeg_pmp3!=NULL) {
      drmp3_uninit(sdc_pmpeg->mpeg_pmp3);
      delete sdc_pmpeg->mpeg_pmp3;
      sdc_pmpeg->mpeg_pmp3 = NULL;
    }
    if (sdc_pmpeg->mpeg_fFile!=NULL) {
      fclose(sdc_pmpeg->mpeg_fFile);
      sdc_pmpeg->mpeg_fFile = NULL;
    }
#endif
    delete sdc_pmpeg;
    sdc_pmpeg = NULL;

  } else if (sdc_pogg!=NULL) {

    if (sdc_pogg->ogg_vfVorbisFile!=NULL) {
      pov_clear(sdc_pogg->ogg_vfVorbisFile);
      delete sdc_pogg->ogg_vfVorbisFile;
      sdc_pogg->ogg_vfVorbisFile = NULL;
    }
    if (sdc_pogg->ogg_fFile!=NULL) {
      fclose(sdc_pogg->ogg_fFile);
      sdc_pogg->ogg_fFile = NULL;
    }
    delete sdc_pogg;
    sdc_pogg = NULL;
  }
}

// reset decoder to start of sample
void CSoundDecoder::Reset(void)
{
  if (sdc_pmpeg!=NULL) {
#ifdef PLATFORM_WIN32
    palDecSeekAbs(sdc_pmpeg->mpeg_hDecoder, 0.0f);
#else
    drmp3_seek_to_pcm_frame(sdc_pmpeg->mpeg_pmp3, 0);
    sdc_pmpeg->mpeg_ctCarry = 0;
#endif
  } else if (sdc_pogg!=NULL) {
/*  !!!! seeking is evil with vorbisfile 1.0RC2
    pov_time_seek(sdc_pogg->ogg_vfVorbisFile, 0.0f);
    */
    // so instead, we reinit
    pov_clear(sdc_pogg->ogg_vfVorbisFile);
    fseek(sdc_pogg->ogg_fFile, sdc_pogg->ogg_slOffset, SEEK_SET);
    pov_open_callbacks(sdc_pogg, sdc_pogg->ogg_vfVorbisFile, NULL, 0, ovcCallbacks);
  }
}

BOOL CSoundDecoder::IsOpen(void) 
{
#ifdef PLATFORM_WIN32
  if (sdc_pmpeg!=NULL && sdc_pmpeg->mpeg_hDecoder!=0) {
#else
  if (sdc_pmpeg!=NULL && sdc_pmpeg->mpeg_pmp3!=NULL) {
#endif
    return TRUE;
  } else if (sdc_pogg!=NULL && sdc_pogg->ogg_vfVorbisFile!=0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

void CSoundDecoder::GetFormat(WAVEFORMATEX &wfe)
{
  if (sdc_pmpeg!=NULL) {
    wfe = sdc_pmpeg->mpeg_wfeFormat;

  } else if (sdc_pogg!=NULL) {
    wfe = sdc_pogg->ogg_wfeFormat;

  } else {
    NOTHING;
  }
}

// read decoder header if possible
SLONG CSoundDecoder::ReadHeader(void *pHeaderMemory, SLONG slHeaderSize)
{
  // not implemented!
  return FALSE;
}


// decode a block of bytes
INDEX CSoundDecoder::Decode(void *pvDestBuffer, INDEX ctBytesToDecode)
{
  // if ogg
  if (sdc_pogg!=NULL && sdc_pogg->ogg_vfVorbisFile!=0) {
    // decode ogg
    static int iCurrrentSection = -1; // we don't care about this
    char *pch = (char *)pvDestBuffer;
    INDEX ctDecoded = 0;
    while (ctDecoded<ctBytesToDecode) {
      long iRes = pov_read(sdc_pogg->ogg_vfVorbisFile, pch, ctBytesToDecode-ctDecoded, 
        0, 2, 1, &iCurrrentSection);
      if (iRes<=0) {
        return ctDecoded;
      }
      ctDecoded+=iRes;
      pch+=iRes;
    }
    return ctDecoded;

  // if mpeg
#ifdef PLATFORM_WIN32
  } else if (sdc_pmpeg!=NULL && sdc_pmpeg->mpeg_hDecoder!=0) {
    // decode mpeg
    return palRead(sdc_pmpeg->mpeg_hDecoder, pvDestBuffer, ctBytesToDecode);
#else
  } else if (sdc_pmpeg!=NULL && sdc_pmpeg->mpeg_pmp3!=NULL) {
    // decode mpeg; dr_mp3 gives whole sample frames, amp11lib gave any number of bytes
    CDecodeData_MPEG &mpeg = *sdc_pmpeg;
    const INDEX ctFrameBytes = 2*2;  // 16-bit stereo
    UBYTE *pub = (UBYTE *)pvDestBuffer;
    INDEX ctDecoded = 0;
    // what is left of the last frame
    const INDEX ctFromCarry = Min(mpeg.mpeg_ctCarry, ctBytesToDecode);
    if (ctFromCarry>0) {
      memcpy(pub, mpeg.mpeg_aubCarry, ctFromCarry);
      mpeg.mpeg_ctCarry -= ctFromCarry;
      memmove(mpeg.mpeg_aubCarry, mpeg.mpeg_aubCarry+ctFromCarry, mpeg.mpeg_ctCarry);
      ctDecoded += ctFromCarry;
    }
    // whole frames
    const INDEX ctFrames = (ctBytesToDecode-ctDecoded)/ctFrameBytes;
    if (ctFrames>0) {
      const drmp3_uint64 ctRead = drmp3_read_pcm_frames_s16(mpeg.mpeg_pmp3, ctFrames, (drmp3_int16 *)(pub+ctDecoded));
      ctDecoded += (INDEX)ctRead*ctFrameBytes;
      if ((INDEX)ctRead<ctFrames) {
        return ctDecoded;  // end of file
      }
    }
    // a part of one more frame, the rest kept for the next call
    const INDEX ctPart = ctBytesToDecode-ctDecoded;
    if (ctPart>0) {
      UBYTE aubFrame[ctFrameBytes];
      if (drmp3_read_pcm_frames_s16(mpeg.mpeg_pmp3, 1, (drmp3_int16 *)aubFrame)==1) {
        memcpy(pub+ctDecoded, aubFrame, ctPart);
        mpeg.mpeg_ctCarry = ctFrameBytes-ctPart;
        memcpy(mpeg.mpeg_aubCarry, aubFrame+ctPart, mpeg.mpeg_ctCarry);
        ctDecoded += ctPart;
      }
    }
    return ctDecoded;
#endif

  // if no decoder
  } else {
    // play all zeroes
    memset(pvDestBuffer, 0, ctBytesToDecode);
    return ctBytesToDecode;
  }
}
