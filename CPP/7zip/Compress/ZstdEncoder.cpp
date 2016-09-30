// ZstdEncoder.cpp
// (C) 2016 Tino Reichardt

#include "StdAfx.h"
#include "ZstdEncoder.h"
#include "ZstdDecoder.h"

#ifndef EXTRACT_ONLY
namespace NCompress {
namespace NZSTD {

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
        Byte zstd_level = static_cast < Byte > (ZSTD_maxCLevel());
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
  ZSTDMT_CCtx *ctx = ZSTDMT_createCCtx(_numThreads, _props._level, _inputSize);
  if (!ctx)
      return S_FALSE;

  /* 3) compress */
  result = ZSTDMT_CompressCCtx(ctx, &rdwr);
  if (ZSTDMT_isError(result))
      return ErrorOut(result);

  /* 4) free resources */
  ZSTDMT_freeCCtx(ctx);
  return S_OK;
}

STDMETHODIMP CEncoder::SetNumberOfThreads(UInt32 numThreads)
{
  const UInt32 kNumThreadsMax = ZSTDMT_THREAD_MAX;
  if (numThreads < 1) numThreads = 1;
  if (numThreads > kNumThreadsMax) numThreads = kNumThreadsMax;
  _numThreads = numThreads;
  return S_OK;
}

HRESULT CEncoder::ErrorOut(size_t code)
{
  const char *strError = ZSTDMT_getErrorString(code);
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
