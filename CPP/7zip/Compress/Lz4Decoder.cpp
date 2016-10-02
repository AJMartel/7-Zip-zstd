// (C) 2016 Tino Reichardt

#include "StdAfx.h"
#include "Lz4Decoder.h"

int Lz4Read(void *arg, LZ4MT_Buffer * in)
{
  struct Lz4Stream *x = (struct Lz4Stream*)arg;
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

int Lz4Write(void *arg, LZ4MT_Buffer * out)
{
  struct Lz4Stream *x = (struct Lz4Stream*)arg;
  UInt32 todo = (UInt32)out->size;
  UInt32 done = 0;

  while (todo != 0)
  {
    UInt32 block;
    HRESULT res = x->outStream->Write((char*)out->buf + done, todo, &block);
    done += block;
    if (res == k_My_HRESULT_WritingWasCut)
      break;
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
namespace NLZ4 {

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
  const UInt32 kNumThreadsMax = LZ4MT_THREAD_MAX;
  if (numThreads < 1) numThreads = 1;
  if (numThreads > kNumThreadsMax) numThreads = kNumThreadsMax;
  _numThreads = numThreads;
  return S_OK;
}

HRESULT CDecoder::ErrorOut(size_t code)
{
  const char *strError = LZ4MT_getErrorString(code);
  size_t strErrorLen = strlen(strError) + 1;
  wchar_t *wstrError = (wchar_t *)MyAlloc(sizeof(wchar_t) * strErrorLen);

  if (!wstrError)
    return E_FAIL;

  mbstowcs(wstrError, strError, strErrorLen - 1);
  MessageBoxW(0, wstrError, L"7-Zip ZStandard", MB_ICONERROR | MB_OK);
  MyFree(wstrError);

  return E_FAIL;
}

HRESULT CDecoder::SetOutStreamSizeResume(const UInt64 * outSize)
{
  printf("%s: %d\n", __FUNCTION__, (int)outSize);
  _processedOut = 0;

  return S_OK;
}

STDMETHODIMP CDecoder::SetOutStreamSize(const UInt64 * outSize)
{
  _processedIn = 0;
  printf("%s: %d\n", __FUNCTION__, (int)outSize);
  RINOK(SetOutStreamSizeResume(outSize));

  return S_OK;
}

HRESULT CDecoder::CodeSpec(ISequentialInStream * inStream,
  ISequentialOutStream * outStream, ICompressProgressInfo * progress)
{
  LZ4MT_RdWr_t rdwr;
  size_t result;

  struct Lz4Stream Rd;
  Rd.cs = &cs;
  Rd.progress = progress;
  Rd.inStream = inStream;
  Rd.processedIn = &_processedIn;
  Rd.processedOut = &_processedOut;

  struct Lz4Stream Wr;
  Wr.cs = &cs;
  Wr.progress = progress;
  Wr.outStream = outStream;
  Wr.processedIn = &_processedIn;
  Wr.processedOut = &_processedOut;

  /* 1) setup read/write functions */
  rdwr.fn_read = ::Lz4Read;
  rdwr.fn_write = ::Lz4Write;
  rdwr.arg_read = (void *)&Rd;
  rdwr.arg_write = (void *)&Wr;

  /* 2) create compression context */
  LZ4MT_DCtx *ctx = LZ4MT_createDCtx(_numThreads, _inputSize);
  if (!ctx)
      return S_FALSE;

  /* 3) compress */
  result = LZ4MT_DecompressDCtx(ctx, &rdwr);
  if (LZ4MT_isError(result))
      return ErrorOut(result);

  /* 4) free resources */
  LZ4MT_freeDCtx(ctx);
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