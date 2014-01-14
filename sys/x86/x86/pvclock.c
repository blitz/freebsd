/*-
 * Copyright (c) 2009 Adrian Chadd
 * Copyright (c) 2012 Spectra Logic Corporation
 * Copyright (c) 2014 Julian Stecklina <js@alien8.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <machine/cpu.h>
#include <machine/pvclock.h>

/*
 * Scale a 64-bit delta by scaling and multiplying by a 32-bit fraction,
 * yielding a 64-bit result.
 */
static inline uint64_t
scale_delta(uint64_t delta, uint32_t mul_frac, int shift)
{
	uint64_t product;

	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;

#if defined(__i386__)
	{
		uint32_t tmp1, tmp2;

		/**
		 * For i386, the formula looks like:
		 *
		 *   lower = (mul_frac * (delta & UINT_MAX)) >> 32
		 *   upper = mul_frac * (delta >> 32)
		 *   product = lower + upper
		 */
		__asm__ (
			"mul  %5       ; "
			"mov  %4,%%eax ; "
			"mov  %%edx,%4 ; "
			"mul  %5       ; "
			"xor  %5,%5    ; "
			"add  %4,%%eax ; "
			"adc  %5,%%edx ; "
			: "=A" (product), "=r" (tmp1), "=r" (tmp2)
			: "a" ((uint32_t)delta), "1" ((uint32_t)(delta >> 32)),
			  "2" (mul_frac) );
	}
#elif defined(__amd64__)
	{
		unsigned long tmp;

		__asm__ (
			"mulq %[mul_frac] ; shrd $32, %[hi], %[lo]"
			: [lo]"=a" (product), [hi]"=d" (tmp)
			: "0" (delta), [mul_frac]"rm"((uint64_t)mul_frac));
	}
#else
#error "pvclock: unsupported architecture"
#endif

	return (product);
}

uint64_t
pvclock_get_nsec(struct pvclock_time_info *tinfo)
{

	return ((scale_delta(rdtsc() - tinfo->tsc_timestamp,
                             tinfo->tsc_to_system_mul,
                             tinfo->tsc_shift))
                + tinfo->system_time);
}

uint32_t
pvclock_fetch_vcpu_tinfo(struct pvclock_time_info *dst,
                         struct pvclock_time_info *src)
{
        uint32_t nversion;

	do {
		dst->version = src->version;
		rmb();
		dst->tsc_timestamp = src->tsc_timestamp;
		dst->system_time = src->system_time;
		dst->tsc_to_system_mul = src->tsc_to_system_mul;
		dst->tsc_shift = src->tsc_shift;
		rmb();
                nversion = src->version;
                __compiler_membar();
	} while ((nversion & 1) | (dst->version ^ nversion));

	return (dst->version);
}

/* EOF */
