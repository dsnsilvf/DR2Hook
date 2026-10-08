#include "dr2hook/session_audio.h"
#include "dr2hook/logger.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

std::atomic<int> g_wanted{-1}; // 1 mudo, 0 com som
std::atomic<int> g_running{0};
std::mutex g_applyMutex;

template <typename T> void Release(T *&p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

// Sessão padrão (GUID nulo) do dispositivo de saída padrão: a mesma em que o
// XAudio2 abre os streams.
HRESULT ApplyMute(bool muted) {
  IMMDeviceEnumerator *enumerator = nullptr;
  IMMDevice *device = nullptr;
  IAudioSessionManager *manager = nullptr;
  ISimpleAudioVolume *volume = nullptr;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&enumerator));
  if (SUCCEEDED(hr)) {
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  }
  if (SUCCEEDED(hr)) {
    hr = device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void **>(&manager));
  }
  if (SUCCEEDED(hr)) {
    hr = manager->GetSimpleAudioVolume(nullptr, FALSE, &volume);
  }
  if (SUCCEEDED(hr)) {
    hr = volume->SetMute(muted ? TRUE : FALSE, nullptr);
  }
  Release(volume);
  Release(manager);
  Release(device);
  Release(enumerator);
  return hr;
}

void ApplyThread() {
  {
    std::lock_guard<std::mutex> lock(g_applyMutex);
    const bool muted = g_wanted.load() == 1;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const HRESULT hr = ApplyMute(muted);
    if (SUCCEEDED(co)) {
      CoUninitialize();
    }
    if (SUCCEEDED(hr)) {
      Logger::Info(muted ? "SessionAudio: som do jogo mudo."
                         : "SessionAudio: som do jogo de volta.");
    } else {
      char msg[96];
      std::snprintf(msg, sizeof msg,
                    "SessionAudio: falhou ao %s o som (hr=0x%08lx).",
                    muted ? "mutar" : "devolver",
                    static_cast<unsigned long>(hr));
      Logger::Warn(msg);
    }
  }
  g_running.fetch_sub(1);
}

#endif

} // namespace

void SessionAudio::SetMuted(bool muted) {
#if defined(_WIN32)
  g_wanted.store(muted ? 1 : 0);
  g_running.fetch_add(1);
  try {
    std::thread(ApplyThread).detach();
  } catch (...) {
    g_running.fetch_sub(1);
  }
#else
  (void)muted;
#endif
}

void SessionAudio::Shutdown() {
#if defined(_WIN32)
  for (int i = 0; i < 200 && g_running.load() > 0; ++i) {
    Sleep(10);
  }
#endif
}

} // namespace dr2hook
