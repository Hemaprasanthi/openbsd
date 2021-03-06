/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Mitchell Horne <mhorne@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#if 0
#include <machine/md_var.h>
#endif
#include <machine/sbi.h>

/* SBI Implementation-Specific Definitions */
#define	OPENSBI_VERSION_MAJOR_OFFSET	16
#define	OPENSBI_VERSION_MINOR_MASK	0xFFFF

u_long sbi_spec_version;
u_long sbi_impl_id;
u_long sbi_impl_version;

static struct sbi_ret
sbi_get_spec_version(void)
{
	return (SBI_CALL0(SBI_EXT_ID_BASE, SBI_BASE_GET_SPEC_VERSION));
}

static struct sbi_ret
sbi_get_impl_id(void)
{
	return (SBI_CALL0(SBI_EXT_ID_BASE, SBI_BASE_GET_IMPL_ID));
}

static struct sbi_ret
sbi_get_impl_version(void)
{
	return (SBI_CALL0(SBI_EXT_ID_BASE, SBI_BASE_GET_IMPL_VERSION));
}

static struct sbi_ret
sbi_get_mvendorid(void)
{
	return (SBI_CALL0(SBI_EXT_ID_BASE, SBI_BASE_GET_MVENDORID));
}


static struct sbi_ret
sbi_get_marchid(void)
{
	return (SBI_CALL0(SBI_EXT_ID_BASE, SBI_BASE_GET_MARCHID));
}

static struct sbi_ret
sbi_get_mimpid(void)
{
	return (SBI_CALL0(SBI_EXT_ID_BASE, SBI_BASE_GET_MIMPID));
}

void
sbi_print_version(void)
{
	u_int major;
	u_int minor;

	/* For legacy SBI implementations. */
	if (sbi_spec_version == 0) {
		printf("SBI: Unknown (Legacy) Implementation\n");
		printf("SBI Specification Version: 0.1\n");
		return;
	}

	switch (sbi_impl_id) {
	case (SBI_IMPL_ID_BBL):
		printf("SBI: Berkely Boot Loader %u\n", sbi_impl_version);
		break;
	case (SBI_IMPL_ID_OPENSBI):
		major = sbi_impl_version >> OPENSBI_VERSION_MAJOR_OFFSET;
		minor = sbi_impl_version & OPENSBI_VERSION_MINOR_MASK;
		printf("SBI: OpenSBI v%u.%u\n", major, minor);
		break;
	default:
		printf("SBI: Unrecognized Implementation: %u\n", sbi_impl_id);
		break;
	}

	major = (sbi_spec_version & SBI_SPEC_VERS_MAJOR_MASK) >>
	    SBI_SPEC_VERS_MAJOR_OFFSET;
	minor = (sbi_spec_version & SBI_SPEC_VERS_MINOR_MASK);
	printf("SBI Specification Version: %u.%u\n", major, minor);
}

void
sbi_init(void)
{
	struct sbi_ret sret;

	/*
	 * Get the spec version. For legacy SBI implementations this will
	 * return an error, otherwise it is guaranteed to succeed.
	 */
	sret = sbi_get_spec_version();
	if (sret.error != 0) {
		/* We are running a legacy SBI implementation. */
		sbi_spec_version = 0;
		return;
	}

	/* Set the SBI implementation info. */
	sbi_spec_version = sret.value;
	sbi_impl_id = sbi_get_impl_id().value;
	sbi_impl_version = sbi_get_impl_version().value;

	// XXX Move somewhere accessible -- md_var.h?
	register_t mvendorid;
	register_t marchid;
	register_t mimpid;
	/* Set the hardware implementation info. */
	mvendorid = sbi_get_mvendorid().value;
	marchid = sbi_get_marchid().value;
	mimpid = sbi_get_mimpid().value;

	/*
	 * Probe for legacy extensions. Currently we rely on all of them
	 * to be implemented, but this is not guaranteed by the spec.
	 */
	KASSERTMSG(sbi_probe_extension(SBI_SET_TIMER) != 0,
	    "SBI doesn't implement sbi_set_timer()");
	KASSERTMSG(sbi_probe_extension(SBI_CONSOLE_PUTCHAR) != 0,
	    "SBI doesn't implement sbi_console_putchar()");
	KASSERTMSG(sbi_probe_extension(SBI_CONSOLE_GETCHAR) != 0,
	    "SBI doesn't implement sbi_console_getchar()");
	KASSERTMSG(sbi_probe_extension(SBI_CLEAR_IPI) != 0,
	    "SBI doesn't implement sbi_clear_ipi()");
	KASSERTMSG(sbi_probe_extension(SBI_SEND_IPI) != 0,
	    "SBI doesn't implement sbi_send_ipi()");
	KASSERTMSG(sbi_probe_extension(SBI_REMOTE_FENCE_I) != 0,
	    "SBI doesn't implement sbi_remote_fence_i()");
	KASSERTMSG(sbi_probe_extension(SBI_REMOTE_SFENCE_VMA) != 0,
	    "SBI doesn't implement sbi_remote_sfence_vma()");
	KASSERTMSG(sbi_probe_extension(SBI_REMOTE_SFENCE_VMA_ASID) != 0,
	    "SBI doesn't implement sbi_remote_sfence_vma_asid()");
	KASSERTMSG(sbi_probe_extension(SBI_SHUTDOWN) != 0,
	    "SBI doesn't implement sbi_shutdown()");
}
