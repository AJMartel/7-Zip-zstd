// (C) 2016 Tino Reichardt

#define LZ4_STATIC_LINKING_ONLY
#include "../../../C/Alloc.h"
#include "../../../C/Threads.h"
#include "../../../C/lz4/lz4.h"
#include "../../../C/zstdmt/lz4mt.h"

#include "../../Windows/System.h"
#include "../../Common/Common.h"
#include "../../Common/MyCom.h"
#include "../ICoder.h"
#include "../Common/StreamUtils.h"
#include "../Common/RegisterCodec.h"

struct Lz4Stream {
  ISequentialInStream *inStream;
  ISequentialOutStream *outStream;
  ICompressProgressInfo *progress;
  UInt64 *processedIn;
  UInt64 *processedOut;
  CCriticalSection *cs;
};

extern int Lz4Read(void *Stream, LZ4MT_Buffer * in);
extern int Lz4Write(void *Stream, LZ4MT_Buffer * out);

namespace NCompress {
namespace NLZ4 {

struct DProps
{
  DProps() { clear (); }
  void clear ()
  {
    memset(this, 0, sizeof (*this));
    _ver_major = LZ4_VERSION_MAJOR;
    _ver_minor = LZ4_VERSION_MINOR;
    _level = 1;
  }

  Byte _ver_major;
  Byte _ver_minor;
  Byte _level;
  Byte _reserved[2];
};

class CDecoder:public ICompressCoder,
  public ICompressSetDecoderProperties2,
#ifndef NO_READ_FROM_CODER
  public ICompressSetInStream,
  public ICompressSetOutStreamSize,
  public ISequentialInStream,
#endif
  public CMyUnknownImp
{
  CMyComPtr < ISequentialInStream > _inStream;

  DProps _props;
  CCriticalSection cs;

  UInt64 _processedIn;
  UInt64 _processedOut;
  UInt64 _OutSize;
  UInt32 _inputSize;
  UInt32 _numThreads;

  HRESULT CDecoder::ErrorOut(size_t code);
  HRESULT CodeSpec(ISequentialInStream *inStream, ISequentialOutStream *outStream, ICompressProgressInfo *progress);
  HRESULT SetOutStreamSizeResume(const UInt64 *outSize);

public:

  MY_QUERYINTERFACE_BEGIN2 (ICompressCoder)
  MY_QUERYINTERFACE_ENTRY (ICompressSetDecoderProperties2)
#ifndef NO_READ_FROM_CODER
  MY_QUERYINTERFACE_ENTRY (ICompressSetInStream)
  MY_QUERYINTERFACE_ENTRY (ICompressSetOutStreamSize)
  MY_QUERYINTERFACE_ENTRY (ISequentialInStream)
#endif
  MY_QUERYINTERFACE_END

  MY_ADDREF_RELEASE
  STDMETHOD (Code)(ISequentialInStream *inStream, ISequentialOutStream *outStream, const UInt64 *inSize, const UInt64 *outSize, ICompressProgressInfo *progress);
  STDMETHOD (SetDecoderProperties2) (const Byte *data, UInt32 size);
  STDMETHOD (SetOutStreamSize) (const UInt64 *outSize);
  STDMETHODIMP CDecoder::SetNumberOfThreads(UInt32 numThreads);

#ifndef NO_READ_FROM_CODER
  STDMETHOD (SetInStream) (ISequentialInStream *inStream);
  STDMETHOD (ReleaseInStream) ();
  STDMETHOD (Read) (void *data, UInt32 size, UInt32 *processedSize);
  HRESULT CodeResume (ISequentialOutStream *outStream, const UInt64 *outSize, ICompressProgressInfo *progress);
  UInt64 GetInputProcessedSize () const { return _processedIn; }
#endif

  CDecoder();
  virtual ~CDecoder();
};

}}
