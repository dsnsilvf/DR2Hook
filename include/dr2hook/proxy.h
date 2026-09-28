#pragma once

#include "common.h"
#include <dxgi.h>

#ifndef DXGI_ERROR_UNSUPPORTED
#define DXGI_ERROR_UNSUPPORTED static_cast<HRESULT>(0x887A0004L)
#endif

// Typedefs para as funções originais exportadas da dxgi.dll
using PFN_CreateDXGIFactory = HRESULT(WINAPI *)(REFIID riid, void **ppFactory);
using PFN_CreateDXGIFactory1 = HRESULT(WINAPI *)(REFIID riid, void **ppFactory);
using PFN_CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT Flags, REFIID riid,
                                                 void **ppFactory);
using PFN_DXGIGetDebugInterface1 = HRESULT(WINAPI *)(UINT Flags, REFIID riid,
                                                     void **pDebug);
using PFN_DXGIDeclareAdapterRemovalSupport = HRESULT(WINAPI *)();

namespace dr2hook {

bool InitializeProxy();
void ShutdownProxy();
FARPROC GetOriginalProc(const char *procName);
void EnsureProxyInitialized();

} // namespace dr2hook

using dr2hook::EnsureProxyInitialized;
using dr2hook::GetOriginalProc;
using dr2hook::InitializeProxy;
using dr2hook::ShutdownProxy;
