#pragma once
// Import the upstream ABI declarations, without Metamod's unused legacy
// utility implementations (which expect AlliedModders' const-modified HLSDK).
// No engine, edict, or Metamod function table layout is redeclared here.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#pragma warning(push)
#pragma warning(disable: 4828) // Unmodified upstream edict.h has an old codepage comment.
#include "extdll.h"
#pragma warning(pop)
struct hudtextparms_s;
using hudtextparms_t = hudtextparms_s; // Only used in unused function pointer declarations.
#define SDK_UTIL_H
#define OSDEP_H
#define C_DLLEXPORT extern "C" __declspec(dllexport)
#include "meta_api.h"
