// ZstdDecoder.cpp
// (C) 2016 Tino Reichardt

#include "StdAfx.h"
#include "ZstdDecoder.h"

int ZstdRead(void *arg, ZSTDMT_Buffer * in)
{
  struct ZstdStream *x = (struct ZstdStream*)arg;
  size_t size = in->size;

  HRESULT res = ReadStream(x->inStream, in->buf, &size);
  if (res != S_OK)
    return -1;

  in->size = size;

  CriticalSection_Enter(x->cs);
  *x->processedIn += size;
  x->progress->SetRatioInfo(x->processedIn, x->processedOut);
  CriticalSection_Leave(x->cs);

  return 0;
}

int ZstdWrite(void *arg, ZSTDMT_Buffer * out)
{
  struct ZstdStream *x = (struct ZstdStream*)arg;
  UInt32 todo = (UInt32)out->size;
  UInt32 done = 0;

  while (todo != 0)
  {
    UInt32 block;
    HRESULT res = x->outStream->Write((char*)out->buf + done, todo, &block);
    done += block;
    if (res == k_My_HRESULT_WritingWasCut)
      break;
    // printf("write() todo=%d block=%d res=%d\n", todo, block, res);
    if (res != S_OK)
      return -1;
    if (block == 0)
      return E_FAIL;
    todo -= block;
  }

  CriticalSection_Enter(x->cs);
  *x->processedOut += done;
  x->progress->SetRatioInfo(x->processedIn, x->processedOut);
  CriticalSection_Leave(x->cs);

  return 0;
}

namespace NCompress {
namespace NZSTD {

CDecoder::CDecoder():
  _processedIn(0),
  _processedOut(0),
  _inputSize(0),
  _numThreads(NWindows::NSystem::GetNumberOfProcessors())
{
  _props.clear();
  CriticalSection_Init(&cs);
}

CDecoder::~CDecoder()
{
  CriticalSection_Delete(&cs);
}

STDMETHODIMP CDecoder::SetDecoderProperties2(const Byte * prop, UInt32 size)
{
  DProps *pProps = (DProps *)prop;

  if (size != sizeof(DProps))
    return E_FAIL;

  memcpy(&_props, pProps, sizeof (DProps));

  return S_OK;
}

STDMETHODIMP CDecoder::SetNumberOfThreads(UInt32 numThreads)
{
  const UInt32 kNumThreadsMax = ZSTDMT_THREAD_MAX;
  if (numThreads < 1) numThreads = 1;
  if (numThreads > kNumThreadsMax) numThreads = kNumThreadsMax;
  _numThreads = numThreads;
  return S_OK;
}

HRESULT CDecoder::ErrorOut(size_t code)
{
  const char *strError = ZSTDMT_getErrorString(code);
  size_t strErrorLen = strlen(strError) + 1;
  wchar_t *wstrError = (wchar_t *)MyAlloc(sizeof(wchar_t) * strErrorLen);
  if (!wstrError)
    return S_FALSE;

  mbstowcs(wstrError, strError, strErrorLen - 1);
  MessageBoxW(0, wstrError, L"7-Zip ZStandard", MB_ICONERROR | MB_OK);
  MyFree(wstrError);

  return S_FALSE;
}

HRESULT CDecoder::SetOutStreamSizeResume(const UInt64 * /*outSize*/)
{
  _processedOut = 0;

  return S_OK;
}

STDMETHODIMP CDecoder::SetOutStreamSize(const UInt64 * outSize)
{
  _processedIn = 0;
  RINOK(SetOutStreamSizeResume(outSize));

  return S_OK;
}

HRESULT CDecoder::CodeSpec(ISequentialInStream * inStream,
  ISequentialOutStream * outStream, ICompressProgressInfo * progress)
{
  ZSTDMT_RdWr_t rdwr;
  size_t result;

  struct ZstdStream Rd;
  Rd.cs = &cs;
  Rd.progress = progress;
  Rd.inStream = inStream;
  Rd.processedIn = &_processedIn;
  Rd.processedOut = &_processedOut;

  struct ZstdStream Wr;
  Wr.cs = &cs;
  Wr.progress = progress;
  Wr.outStream = outStream;
  Wr.processedIn = &_processedIn;
  Wr.processedOut = &_processedOut;

  /* 1) setup read/write functions */
  rdwr.fn_read = ::ZstdRead;
  rdwr.fn_write = ::ZstdWrite;
  rdwr.arg_read = (void *)&Rd;
  rdwr.arg_write = (void *)&Wr;

  /* 2) create compression context */
  ZSTDMT_DCtx *ctx = ZSTDMT_createDCtx(_numThreads, _inputSize);
  if (!ctx)
      return S_FALSE;

  /* 3) compress */
  result = ZSTDMT_DecompressDCtx(ctx, &rdwr);
  if (ZSTDMT_isError(result))
      return ErrorOut(result);

  /* 4) free resources */
  ZSTDMT_freeDCtx(ctx);
  return S_OK;
}

STDMETHODIMP CDecoder::Code(ISequentialInStream * inStream, ISequentialOutStream * outStream,
  const UInt64 * /*inSize */, const UInt64 *outSize, ICompressProgressInfo * progress)
{
  SetOutStreamSize(outSize);
  return CodeSpec(inStream, outStream, progress);
}

#ifndef NO_READ_FROM_CODER
STDMETHODIMP CDecoder::SetInStream(ISequentialInStream * inStream)
{
  _inStream = inStream;
  return S_OK;
}

STDMETHODIMP CDecoder::ReleaseInStream()
{
  _inStream.Release();

  return S_OK;
}

STDMETHODIMP CDecoder::Read(void *data, UInt32 /*size*/, UInt32 *processedSize)
{
  if (processedSize)
    *processedSize = 0;

  MessageBoxW(0, L"read", L"7-Zip ZStandard", MB_ICONERROR | MB_OK);

  data = 0;
  return E_FAIL;
}

HRESULT CDecoder::CodeResume(ISequentialOutStream * outStream, const UInt64 * outSize, ICompressProgressInfo * progress)
{
  RINOK(SetOutStreamSizeResume(outSize));
  return CodeSpec(_inStream, outStream, progress);
}
#endif

}}
