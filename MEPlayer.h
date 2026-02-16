#pragma once

#include "mfmediaengine.h"

struct MediaEngineNotify : winrt::implements<MediaEngineNotify, IMFMediaEngineNotify>
{
    HRESULT EventNotify(
        _In_  DWORD event,
        _In_  DWORD_PTR param1,
        _In_  DWORD param2) noexcept override 
    {
        return S_OK;
    }
};

class MEPlayer
{
};