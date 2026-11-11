/*
 * Streamerdesk ASMR Suite - atomicos minimos portables (MSVC, GCC, Clang, MinGW).
 * Evita util/threading.h de OBS, cuyas variantes de Windows usan intrinsecos
 * exclusivos de MSVC y no compilan con MinGW/clang.
 */
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
typedef volatile long sd_atomic_long;
static __inline long sd_atomic_load(sd_atomic_long *p)
{
	return _InterlockedOr(p, 0);
}
static __inline void sd_atomic_store(sd_atomic_long *p, long v)
{
	_InterlockedExchange(p, v);
}
static __inline long sd_atomic_inc(sd_atomic_long *p)
{
	return _InterlockedIncrement(p);
}
#else
typedef volatile long sd_atomic_long;
static inline long sd_atomic_load(sd_atomic_long *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void sd_atomic_store(sd_atomic_long *p, long v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}
static inline long sd_atomic_inc(sd_atomic_long *p)
{
	return __atomic_add_fetch(p, 1, __ATOMIC_ACQ_REL);
}
#endif
