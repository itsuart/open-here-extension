#pragma once

#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define UNICODE

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef uintmax_t uint;
typedef intmax_t sint;
typedef USHORT u16;

#define ODS(x) OutputDebugStringW(x)
