#pragma once

// conflicts with boost without these
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <atomic>
#include <condition_variable>
#include <d3d11_4.h>
#include <filesystem>
#include <functional>
#include <mfmediaengine.h>
#include <winrt/Windows.Foundation.h>