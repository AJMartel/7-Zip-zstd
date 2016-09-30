// (C) 2016 Tino Reichardt

#include "StdAfx.h"
#include "Lz4Encoder.h"
#include "Lz4Decoder.h"

#ifndef EXTRACT_ONLY
namespace NCompress {
namespace NLZ4 {

CEncoder::CEncoder():
  _processedIn(0),
  _processedOut(0),
  _inputSize(0),
  _numThreads(NWindows::NSystem::GetNumberOfProcessors())
{
  _props.clear();
  CriticalSection_Init(&cs);
}

CEncoder::~CEncoder()
{
  CriticalSection_Delete(&cs);
}

STDMETHODIMP CEncoder::SetCoderProperties(const PROPID * propIDs, const PROPVARIANT * coderProps, UInt32 numProps)
{
  _props.clear();

  for (UInt32 i = 0; i < numProps; i++)
  {
    const PROPVARIANT & prop = coderProps[i];
    PROPID propID = propIDs[i];
    UInt32 v = (UInt32)prop.ulVal;
    switch (propID)
    {
    case NCoderPropID::kLevel:
      {
        if (prop.vt != VT_UI4)
          return E_INVALIDARG;

        /* level 1..22 */
        _props._level = static_cast < Byte > (prop.ulVal);
        Byte zstd_level = static_cast < Byte > (LZ4MT_LEVEL_MAX);
        if (_props._level > zstd_level)
          _props._level = zstd_level;

        break;
      }
    case NCoderPropID::kNumThreads:
      {
        SetNumberOfThreads(v);
        break;
      }
    default:
      {
        break;
      }
    }
  }

  _processedIn = 0;
  _processedOut = 0;

  return S_OK;
}

STDMETHODIMP CEncoder::WriteCoderProperties(ISequentialOutStream * outStream)
{
  return WriteStream(outStream, &_props, sizeof (_props));
}

STDMETHODIMP CEncoder::Code(ISequentialInStream *inStream,
  ISequentialOutStream *outStream, const UInt64 * /* inSize */ ,
  const UInt64 * /* outSize */ , ICompressProgressInfo *progress)
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
  LZ4MT_CCtx *ctx = LZ4MT_createCCtx(_numThreads, _props._level, _inputSize);
  if (!ctx)
      return S_FALSE;

  /* 3) compress */
  result = LZ4MT_CompressCCtx(ctx, &rdwr);
  if (LZ4MT_isError(result))
      return ErrorOut(result);

  /* 4) free resources */
  LZ4MT_freeCCtx(ctx);
  return S_OK;
}

STDMETHODIMP CEncoder::SetNumberOfThreads(UInt32 numThreads)
{
  const UInt32 kNumThreadsMax = LZ4MT_THREAD_MAX;
  if (numThreads < 1) numThreads = 1;
  if (numThreads > kNumThreadsMax) numThreads = kNumThreadsMax;
  _numThreads = numThreads;
  return S_OK;
}

HRESULT CEncoder::ErrorOut(size_t code)
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
}}
#endif
