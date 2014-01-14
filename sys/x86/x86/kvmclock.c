/*-
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
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/pcpu.h>
#include <machine/clock.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/pvclock.h>
#include <vm/vm.h>
#include <vm/pmap.h>

/* These are the 'new' versions of these MSRs. We don't bother
 * implementing the old MSR locations. */
#define MSR_KVM_WALL_CLOCK  0x4b564d00
#define MSR_KVM_SYSTEM_TIME 0x4b564d01

/* Prototypes */

static unsigned kvmclock_get_timecount(struct timecounter *tc);

/* Globals */

static struct timecounter tsc_timecounter = {
	kvmclock_get_timecount,	/* get_timecount */
	0,                      /* no poll_pps */
	~0u,                    /* counter_mask */
	1000000000,             /* frequency (nanoseconds) */
	"KVMCLOCK",             /* name */
	1000,                   /* quality */
};


DPCPU_DEFINE(struct pvclock_time_info, vcpu_time_info);

/* Implementation */

static unsigned
kvmclock_get_timecount(struct timecounter *tc __unused)
{
        struct pvclock_time_info cur;
        pvclock_fetch_vcpu_tinfo(DPCPU_PTR(vcpu_time_info), &cur);
        return pvclock_get_nsec(&cur);
}

static void
init_KVMCLOCK_tc(void)
{
	if ((cpu_feature2 & CPUID2_HV) == 0)
                return;

        unsigned regs[4];
        do_cpuid(0x40000001, regs);

        if ((regs[0] & (1 <<  3)) == 0 /* pvclock not available */ ||
            (regs[0] & (1 << 24)) == 0 /* pvclock not monotonic across CPUs */)
                return;

        printf("KVM-style paravirtualized clock detected.\n");

        unsigned cpu;
        CPU_FOREACH(cpu) {
                wrmsr(MSR_KVM_SYSTEM_TIME,
                      1 /* Enable */ | vtophys(DPCPU_ID_PTR(cpu, vcpu_time_info)));
        }

        tc_init(&tsc_timecounter);
}

SYSINIT(kvmclock_tc, SI_SUB_SMP, SI_ORDER_ANY, init_KVMCLOCK_tc, NULL);

/* EOF */
