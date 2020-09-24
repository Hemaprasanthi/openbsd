/* $OpenBSD: viomb.c,v 1.7 2020/09/04 13:10:16 bket Exp $	 */
/* $NetBSD: viomb.c,v 1.1 2011/10/30 12:12:21 hannken Exp $	 */

/*
 * Copyright (c) 2012 Talypov Dinar <dinar@i-nk.ru>
 * Copyright (c) 2010 Minoura Makoto.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/syslog.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/task.h>
#include <sys/pool.h>
#include <sys/sensors.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvmexp.h>
#include <sys/sysctl.h>
#include <sys/mount.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>
#include <dev/pci/virtio_pcireg.h>


extern struct uvmexp uvmexp;
extern struct bcachestats bcstats;

#if VIRTIO_PAGE_SIZE!=PAGE_SIZE
#error non-4K page sizes are not supported yet
#endif

#define	DEVNAME(sc)	sc->sc_dev.dv_xname
#if VIRTIO_DEBUG
#define VIOMBDEBUG(sc, format, args...)					  \
		do { printf("%s: " format, sc->sc_dev.dv_xname, ##args);} \
		while (0)
#else
#define VIOMBDEBUG(...)
#endif

/* flags used to specify kind of operation,
 * actually should be moved to virtiovar.h
 */
#define VRING_READ		0
#define VRING_WRITE		1

/* notify or don't notify */
#define VRING_NO_NOTIFY		0
#define VRING_NOTIFY		1

/* Configuration registers */
#define VIRTIO_BALLOON_CONFIG_NUM_PAGES	0	/* 32bit */
#define VIRTIO_BALLOON_CONFIG_ACTUAL	4	/* 32bit */

/* Feature bits */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST (1ULL<<0)
#define VIRTIO_BALLOON_F_STATS_VQ	(1ULL<<1)

#define VIOMB_STATS_MAX		  6   /* Maximum number of tags */
#define VIRTIO_BALLOON_S_SWAP_IN  0   /* Amount of memory swapped in */
#define VIRTIO_BALLOON_S_SWAP_OUT 1   /* Amount of memory swapped out */
#define VIRTIO_BALLOON_S_MAJFLT   2   /* Number of major faults */
#define VIRTIO_BALLOON_S_MINFLT   3   /* Number of minor faults (N/A) */
#define VIRTIO_BALLOON_S_MEMFREE  4   /* Total amount of free memory */
#define VIRTIO_BALLOON_S_MEMTOT   5   /* Total amount of memory */
#define VIRTIO_BALLOON_S_AVAIL    6   /* How much mem is free w/o swapping */
#define VIRTIO_BALLOON_S_CACHES   7   /* Current amout of mem in page cache */
#define VIRTIO_BALLOON_S_HTML_PGALLOC 8 /* HugeTLB page allocations (N/A) */
#define VIRTIO_BALLOON_S_HTML_PGFAIL  9 /* HugeTLB page failures (N/A) */

#define VIOMB_BUFSIZE 16

static const struct virtio_feature_name viomb_feature_names[] = {
	{VIRTIO_BALLOON_F_MUST_TELL_HOST, "TellHost"},
	{VIRTIO_BALLOON_F_STATS_VQ, "StatVQ"},
	{0, NULL}
};
#define PGS_PER_REQ	256	/* 1MB, 4KB/page */
#define VQ_INFLATE	0
#define VQ_DEFLATE	1
#define VQ_STATS	2

/*
 * This struct will be viewed by host and driver
 *
 */
struct balloon_req {
	bus_dmamap_t	 bl_dmamap;
	struct pglist	 bl_pglist;
	int		 bl_nentries;
	u_int32_t	*bl_pages;
};

struct virtio_balloon_stat {
	uint16_t tag;
	uint64_t val;
} __attribute__((packed));

/*
 * Holds the global state of the viomb driver
 * Just for driver
 */
struct viomb_softc {
	struct device			sc_dev;
	struct virtio_softc		*sc_virtio;
	struct virtqueue		sc_vq[3];
	u_int32_t			sc_npages;
	u_int32_t			sc_actual;
	struct balloon_req		sc_req;
	struct taskq			*sc_taskq;
	struct taskq			*sc_stats_taskq;
	struct task			sc_task;
	struct task			sc_stats_task;
	struct pglist			sc_balloon_pages;
	struct ksensor			sc_sens[2];
	struct ksensordev		sc_sensdev;
	bus_dmamap_t        		sc_stats_dmamap;
	struct virtio_balloon_stat 	*sc_stats_buf;
};

int	viomb_match(struct device *, void *, void *);
void	viomb_attach(struct device *, struct device *, void *);
void	viomb_worker(void *);
void	viomb_stats_worker(void *);
void	viomb_inflate(struct viomb_softc *);
void	viomb_deflate(struct viomb_softc *);

void    viomb_stats(struct viomb_softc *);

int	viomb_config_change(struct virtio_softc *);
void	viomb_read_config(struct viomb_softc *);
int	viomb_vq_dequeue(struct virtqueue *);
int	viomb_vq_dequeue(struct virtqueue *);
int	viomb_inflate_intr(struct virtqueue *);
int	viomb_deflate_intr(struct virtqueue *);
int	viomb_stats_intr(struct virtqueue *);

int	viomb_stats_intr(struct virtqueue *);
void	get_memory_stats(struct viomb_softc *);

int viomb_stats_done_first;

struct cfattach viomb_ca = {
	sizeof(struct viomb_softc), viomb_match, viomb_attach
};

struct cfdriver viomb_cd = {
	NULL, "viomb", DV_DULL
};

int
viomb_match(struct device *parent, void *match, void *aux)
{
	struct virtio_softc *va = aux;
	if (va->sc_childdevid == PCI_PRODUCT_VIRTIO_BALLOON)
		return (1);
	return (0);
}

void
viomb_attach(struct device *parent, struct device *self, void *aux)
{
	printf("%s - attaching viomb cmpe\n",__func__);
	struct viomb_softc *sc = (struct viomb_softc *)self;
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	int i;

	if (vsc->sc_child != NULL) {
		printf("child already attached for %s; something wrong...\n",
		    parent->dv_xname);
		return;
	}

	/* fail on non-4K page size archs */
	if (VIRTIO_PAGE_SIZE != PAGE_SIZE){
		printf("non-4K page size arch found, needs %d, got %d\n",
		    VIRTIO_PAGE_SIZE, PAGE_SIZE);
		return;
	}

	// vsc is global
	sc->sc_virtio = vsc;
	vsc->sc_vqs = &sc->sc_vq[VQ_INFLATE];
	vsc->sc_nvqs = 0;
	vsc->sc_child = self;
	vsc->sc_ipl = IPL_BIO;
	vsc->sc_config_change = viomb_config_change;

	vsc->sc_driver_features = VIRTIO_BALLOON_F_MUST_TELL_HOST |
	    VIRTIO_BALLOON_F_STATS_VQ;
	if (virtio_negotiate_features(vsc, viomb_feature_names) != 0)
		goto err;

	if ((virtio_alloc_vq(vsc, &sc->sc_vq[VQ_INFLATE], VQ_INFLATE,
	     sizeof(u_int32_t) * PGS_PER_REQ, 1, "inflate") != 0))
		goto err;
	vsc->sc_nvqs++;
	if ((virtio_alloc_vq(vsc, &sc->sc_vq[VQ_DEFLATE], VQ_DEFLATE,
	     sizeof(u_int32_t) * PGS_PER_REQ, 1, "deflate") != 0))
		goto err;
	vsc->sc_nvqs++;
	if ((virtio_alloc_vq(vsc, &sc->sc_vq[VQ_STATS], VQ_STATS,
	     VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat), 1, "stats") != 0))
		goto err;
	vsc->sc_nvqs++;

	sc->sc_vq[VQ_INFLATE].vq_done = viomb_inflate_intr;
	sc->sc_vq[VQ_DEFLATE].vq_done = viomb_deflate_intr;
	sc->sc_vq[VQ_STATS].vq_done = viomb_stats_intr;
	virtio_start_vq_intr(vsc, &sc->sc_vq[VQ_INFLATE]);
	virtio_start_vq_intr(vsc, &sc->sc_vq[VQ_DEFLATE]);
//	virtio_start_vq_intr(vsc, &sc->sc_vq[VQ_STATS]);

	viomb_read_config(sc);
	TAILQ_INIT(&sc->sc_balloon_pages);

	if ((sc->sc_req.bl_pages = dma_alloc(sizeof(u_int32_t) * PGS_PER_REQ,
	    PR_NOWAIT|PR_ZERO)) == NULL) {
		printf("%s: Can't alloc DMA memory.\n", DEVNAME(sc));
		goto err;
	}
	if (bus_dmamap_create(vsc->sc_dmat, sizeof(u_int32_t) * PGS_PER_REQ,
			      1, sizeof(u_int32_t) * PGS_PER_REQ, 0,
			      BUS_DMA_NOWAIT, &sc->sc_req.bl_dmamap)) {
		printf("%s: dmamap creation failed.\n", DEVNAME(sc));
		goto err;
	}
	if (bus_dmamap_load(vsc->sc_dmat, sc->sc_req.bl_dmamap,
			    &sc->sc_req.bl_pages[0],
			    sizeof(uint32_t) * PGS_PER_REQ,
			    NULL, BUS_DMA_NOWAIT)) {
		printf("%s: dmamap load failed.\n", DEVNAME(sc));
		goto err_dmamap;
	}

	if ((sc->sc_stats_buf = dma_alloc(VIOMB_STATS_MAX *
	    sizeof(struct virtio_balloon_stat), PR_NOWAIT|PR_ZERO)) == NULL) {
		printf("%s: Can't alloc DMA memory.\n", DEVNAME(sc));
		goto err_dmamap;
	}
	if (bus_dmamap_create(vsc->sc_dmat,
	    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
	    1, VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat), 0,
	    BUS_DMA_NOWAIT, &sc->sc_stats_dmamap)) {
		printf("%s: dmamap creation failed.\n", DEVNAME(sc));
		goto err_dmamap;
	}
	if (bus_dmamap_load(vsc->sc_dmat, sc->sc_stats_dmamap,
	    sc->sc_stats_buf,
	    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
	    NULL, BUS_DMA_NOWAIT)) {
		printf("%s: dmamap load failed.\n", DEVNAME(sc));
		goto err_dmamap2;
	}

	sc->sc_taskq = taskq_create("viomb", 1, IPL_BIO, 0);
	if (sc->sc_taskq == NULL)
		goto err_dmamap2;
	task_set(&sc->sc_task, viomb_worker, sc);

	sc->sc_stats_taskq = taskq_create("viomb_stats", 1, IPL_BIO, 0);
	if (sc->sc_stats_taskq == NULL)
		goto err_dmamap2;
	task_set(&sc->sc_stats_task, viomb_stats_worker, sc);
	task_add(sc->sc_stats_taskq, &sc->sc_stats_task);

	strlcpy(sc->sc_sensdev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensdev.xname));
	strlcpy(sc->sc_sens[0].desc, "desired",
	    sizeof(sc->sc_sens[0].desc));
	sc->sc_sens[0].type = SENSOR_INTEGER;
	sensor_attach(&sc->sc_sensdev, &sc->sc_sens[0]);
	sc->sc_sens[0].value = sc->sc_npages << PAGE_SHIFT;

	strlcpy(sc->sc_sens[1].desc, "current",
	    sizeof(sc->sc_sens[1].desc));
	sc->sc_sens[1].type = SENSOR_INTEGER;
	sensor_attach(&sc->sc_sensdev, &sc->sc_sens[1]);
	sc->sc_sens[1].value = sc->sc_actual << PAGE_SHIFT;

	sensordev_install(&sc->sc_sensdev);

	printf("\n");
	return;

err_dmamap2:
	bus_dmamap_destroy(vsc->sc_dmat, sc->sc_stats_dmamap);
err_dmamap:
	bus_dmamap_destroy(vsc->sc_dmat, sc->sc_req.bl_dmamap);
err:
	if (sc->sc_req.bl_pages)
		dma_free(sc->sc_req.bl_pages, sizeof(u_int32_t) * PGS_PER_REQ);
	if (sc->sc_stats_buf)
		dma_free(sc->sc_stats_buf,
		    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat));
	for (i = 0; i < vsc->sc_nvqs; i++)
		virtio_free_vq(vsc, &sc->sc_vq[i]);
	vsc->sc_nvqs = 0;
	vsc->sc_child = VIRTIO_CHILD_ERROR;

	return;
}

/*
 * Config change
 */
int
viomb_config_change(struct virtio_softc *vsc)
{
	struct viomb_softc *sc = (struct viomb_softc *)vsc->sc_child;

	task_add(sc->sc_taskq, &sc->sc_task);

	return (1);
}

void
viomb_worker(void *arg1)
{
	struct viomb_softc *sc = (struct viomb_softc *)arg1;
	int s;

	s = splbio();
	viomb_read_config(sc);
	if (sc->sc_npages > sc->sc_actual){
		printf("%s: inflating balloon from %u to %u.\n", __func__,
		   sc->sc_actual, sc->sc_npages);
		viomb_inflate(sc);
		}
	else if (sc->sc_npages < sc->sc_actual){
		printf("%s: deflating balloon from %u to %u.\n", __func__,
		   sc->sc_actual, sc->sc_npages);
		viomb_deflate(sc);
	}

	sc->sc_sens[0].value = sc->sc_npages << PAGE_SHIFT;
	sc->sc_sens[1].value = sc->sc_actual << PAGE_SHIFT;

	splx(s);
}

void
viomb_stats_worker(void *arg1)
{
	struct viomb_softc *sc = (struct viomb_softc *)arg1;
	int s, i;

	s = splbio();
	printf("%s: entered\n", __func__);
	printf("%s: getting memory statistics\n", __func__);
	viomb_stats(sc);
	for (i = 0; i < VIOMB_STATS_MAX; i++)
		printf("%s: stats[%d]: tag=0x%x val=0x%llx\n", __func__, i,
		    sc->sc_stats_buf[i].tag,
		    sc->sc_stats_buf[i].val);

	printf("%s: leaving\n", __func__);
	splx(s);

	// XXX investigate why there is a stuck irq without this?
	task_del(sc->sc_stats_taskq, &sc->sc_stats_task);
}

/*
 *
  * viomb_softc defined in viomb.c
  *
  * Parameter: sc is a global reference to the viomb driver state
  */
void
viomb_inflate(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = (struct virtio_softc *)sc->sc_virtio;
	struct balloon_req *b;
	struct vm_page *p;
	struct virtqueue *vq = &sc->sc_vq[VQ_INFLATE];
	u_int32_t nvpages;
	int slot, error, i = 0, j = 0;

	nvpages = sc->sc_npages - sc->sc_actual;
	if (nvpages > PGS_PER_REQ)
		nvpages = PGS_PER_REQ;
	b = &sc->sc_req;

	TAILQ_INIT(&b->bl_pglist);

	printf("%d: bl_pglist is empty\n", TAILQ_EMPTY(&b->bl_pglist));

	/*  API call for allocating pages
	 *
	 *  - creates bl_pglist of size nvpages
	 *  - allocates memory for the entire bl_pglist
	 *  - bl_pglist will be the list of free pages to give to host
	 */
	if ((error = uvm_pglistalloc(nvpages * PAGE_SIZE, 0,
				     //dma_constraint.ucr_high,
					 -1, //or uint64_t or 500 megs
				     0, 0, &b->bl_pglist, nvpages,
				     UVM_PLA_NOWAIT))) {
		printf("%s unable to allocate %u physmem pages,"
		    "error %d\n", DEVNAME(sc), nvpages, error);
		return;
	}

	b->bl_nentries = nvpages;

	TAILQ_FOREACH(p, &b->bl_pglist, pageq)
		printf("%s: page %d # 0x%llx \n", __func__, j++, (uint64_t)p->phys_addr);

	TAILQ_FOREACH(p, &b->bl_pglist, pageq)
		b->bl_pages[i++] = p->phys_addr / VIRTIO_PAGE_SIZE;

	KASSERT(i == nvpages);

	if ((virtio_enqueue_prep(vq, &slot)) > 0) {
		printf("%s:virtio_enqueue_prep() vq_num %d\n",
		       DEVNAME(sc), vq->vq_num);
		goto err;
	}
	if (virtio_enqueue_reserve(vq, slot, 1)) {
		printf("%s:virtio_enqueue_reserve vq_num %d\n",
		       DEVNAME(sc), vq->vq_num);
		goto err;
	}

	bus_dmamap_sync(vsc->sc_dmat, b->bl_dmamap, 0,
			sizeof(u_int32_t) * nvpages, BUS_DMASYNC_PREWRITE);
	virtio_enqueue_p(vq, slot, b->bl_dmamap, 0,
			 sizeof(u_int32_t) * nvpages, VRING_READ);

	//sc->sc_actual = sc->sc_actual + nvpages;
	printf("virtio_write_device_config_4 updating actual: %d \n", sc->sc_actual);
	// virtio_write_device_config_4(vsc, VIRTIO_BALLOON_CONFIG_ACTUAL,
	// 	sc->sc_actual);

	virtio_enqueue_commit(vsc, vq, slot, VRING_NOTIFY);
	return;
err:
	uvm_pglistfree(&b->bl_pglist);
	printf("%s, err: vq->vq_num: %u \n", __func__, vq->vq_num);
	return;
}

void
viomb_deflate(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = (struct virtio_softc *)sc->sc_virtio;
	struct balloon_req *b;
	struct vm_page *p;
	struct virtqueue *vq = &sc->sc_vq[VQ_DEFLATE];
	u_int64_t nvpages;
	int i, slot;

	nvpages = sc->sc_actual - sc->sc_npages;
	if (nvpages > PGS_PER_REQ)
		nvpages = PGS_PER_REQ;
	b = &sc->sc_req;
	b->bl_nentries = nvpages;

	TAILQ_INIT(&b->bl_pglist);
	for (i = 0; i < nvpages; i++) {
		p = TAILQ_FIRST(&sc->sc_balloon_pages);
		if (p == NULL){
		    b->bl_nentries = i - 1;
		    break;
		}
		TAILQ_REMOVE(&sc->sc_balloon_pages, p, pageq);
		TAILQ_INSERT_TAIL(&b->bl_pglist, p, pageq);
		b->bl_pages[i] = p->phys_addr / VIRTIO_PAGE_SIZE;
	}

	if (virtio_enqueue_prep(vq, &slot)) {
		printf("%s:virtio_get_slot(def) vq_num %d\n",
		       DEVNAME(sc), vq->vq_num);
		goto err;
	}
	if (virtio_enqueue_reserve(vq, slot, 1)) {
		printf("%s:virtio_enqueue_reserve() vq_num %d\n",
		       DEVNAME(sc), vq->vq_num);
		goto err;
	}
	bus_dmamap_sync(vsc->sc_dmat, b->bl_dmamap, 0,
	    sizeof(u_int32_t) * nvpages, BUS_DMASYNC_PREWRITE);
	virtio_enqueue_p(vq, slot, b->bl_dmamap, 0,
	    sizeof(u_int32_t) * nvpages, VRING_READ);

	if (!virtio_has_feature(vsc, VIRTIO_BALLOON_F_MUST_TELL_HOST))
		uvm_pglistfree(&b->bl_pglist);

	virtio_enqueue_commit(vsc, vq, slot, VRING_NOTIFY);
	return;
err:
	TAILQ_CONCAT(&sc->sc_balloon_pages, &b->bl_pglist, pageq);
	return;
}

void
viomb_stats(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = (struct virtio_softc *)sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq[VQ_STATS];
	int slot;

	printf("%s: entered\n", __func__);

	if ((virtio_enqueue_prep(vq, &slot)) > 0) {
		printf("%s:virtio_enqueue_prep() vq_num %d\n",
		       DEVNAME(sc), vq->vq_num);
		return;
	}

	if (virtio_enqueue_reserve(vq, slot, 1)) {
		printf("%s:virtio_enqueue_reserve vq_num %d\n",
		       DEVNAME(sc), vq->vq_num);
		return;
	}

	printf("%s: got slot=%d\n", __func__, slot);

	if (viomb_stats_done_first) {
		get_memory_stats(sc);
		bus_dmamap_sync(vsc->sc_dmat, sc->sc_stats_dmamap, 0,
		    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
		    BUS_DMASYNC_PREWRITE);

		virtio_enqueue_p(vq, slot,  sc->sc_stats_dmamap, 0,
		    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
		    VRING_WRITE);
	} else {
		viomb_stats_done_first = 1;
		printf("%s: empty buf (first time)\n", __func__);
		memset(sc->sc_stats_buf, 0,
		    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat));

		bus_dmamap_sync(vsc->sc_dmat, sc->sc_stats_dmamap, 0,
		    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
		    BUS_DMASYNC_PREWRITE);

		virtio_enqueue_p(vq, slot,  sc->sc_stats_dmamap, 0,
		    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
		    VRING_WRITE);
	}

	virtio_enqueue_commit(vsc, vq, slot, VRING_NOTIFY);

	printf("%s: leaving\n", __func__);
}

void
viomb_read_config(struct viomb_softc *sc)
{
	struct virtio_softc *vsc = (struct virtio_softc *)sc->sc_virtio;
	u_int32_t reg;

	/* these values are explicitly specified as little-endian */
	reg = virtio_read_device_config_4(vsc, VIRTIO_BALLOON_CONFIG_NUM_PAGES);
	sc->sc_npages = letoh32(reg);
	reg = virtio_read_device_config_4(vsc, VIRTIO_BALLOON_CONFIG_ACTUAL);
	sc->sc_actual = letoh32(reg);
	VIOMBDEBUG(sc, "sc->sc_npages %u, sc->sc_actual %u\n",
		   sc->sc_npages, sc->sc_actual);
}

int
viomb_vq_dequeue(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viomb_softc *sc = (struct viomb_softc *)vsc->sc_child;
	int r, slot;

	r = virtio_dequeue(vsc, vq, &slot, NULL);
	if (r != 0) {
		printf("%s: dequeue failed, errno %d\n", DEVNAME(sc), r);
		return(r);
	}
	virtio_dequeue_commit(vq, slot);
	return(0);
}

int
viomb_inflate_intr(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viomb_softc *sc = (struct viomb_softc *)vsc->sc_child;
	struct balloon_req *b;
	u_int64_t nvpages;

	if (viomb_vq_dequeue(vq))
		return(1);

	b = &sc->sc_req;
	nvpages = b->bl_nentries;
	bus_dmamap_sync(vsc->sc_dmat, b->bl_dmamap, 0,
			sizeof(u_int32_t) * nvpages,
			BUS_DMASYNC_POSTWRITE);
	TAILQ_CONCAT(&sc->sc_balloon_pages, &b->bl_pglist, pageq);
	VIOMBDEBUG(sc, "updating sc->sc_actual from %u to %llu\n",
		   sc->sc_actual, sc->sc_actual + nvpages);

	virtio_write_device_config_4(vsc, VIRTIO_BALLOON_CONFIG_ACTUAL,
				     sc->sc_actual + nvpages);

	viomb_read_config(sc);

	/* if we have more work to do, add it to the task list */
	if (sc->sc_npages > sc->sc_actual)
		task_add(sc->sc_taskq, &sc->sc_task);

	return (1);
}

int
viomb_deflate_intr(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viomb_softc *sc = (struct viomb_softc *)vsc->sc_child;
	struct balloon_req *b;
	u_int64_t nvpages;

	if (viomb_vq_dequeue(vq))
		return(1);

	b = &sc->sc_req;
	nvpages = b->bl_nentries;
	bus_dmamap_sync(vsc->sc_dmat, b->bl_dmamap, 0,
			sizeof(u_int32_t) * nvpages,
			BUS_DMASYNC_POSTWRITE);

	if (virtio_has_feature(vsc, VIRTIO_BALLOON_F_MUST_TELL_HOST))
		uvm_pglistfree(&b->bl_pglist);

	VIOMBDEBUG(sc, "updating sc->sc_actual from %u to %llu\n",
		sc->sc_actual, sc->sc_actual - nvpages);
	virtio_write_device_config_4(vsc, VIRTIO_BALLOON_CONFIG_ACTUAL,
				     sc->sc_actual - nvpages);
	viomb_read_config(sc);

	/* if we have more work to do, add it to tasks list */
	if (sc->sc_npages < sc->sc_actual)
		task_add(sc->sc_taskq, &sc->sc_task);

	return(1);
}

/*
 * viomb_stats_intr
 *
 * Interrupt handler for when device wants memory statistics from driver
 */
int
viomb_stats_intr(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viomb_softc *sc = (struct viomb_softc *)vsc->sc_child;

	if (viomb_vq_dequeue(vq))
		return(1);

	bus_dmamap_sync(vsc->sc_dmat, sc->sc_stats_dmamap, 0,
	    VIOMB_STATS_MAX * sizeof(struct virtio_balloon_stat),
	    BUS_DMASYNC_POSTWRITE);

	task_add(sc->sc_stats_taskq, &sc->sc_stats_task);

	return(1);
}

/*
 * get_memory_stats
 *
 * The host/VMM has requested new stats to be sent. Gather these from
 * UVM and other places and update our local array of tags/values in
 * the softc.
 *
 * Parameters:
 *  sc: Our softc
 */
void
get_memory_stats(struct viomb_softc *sc)
{
	struct virtio_balloon_stat *s;

	s = (struct virtio_balloon_stat *)sc->sc_stats_buf;

	s[0].tag = VIRTIO_BALLOON_S_SWAP_IN;
	s[0].val = uvmexp.swpginuse * PAGE_SIZE;
	VIOMBDEBUG("%s: swapped in : %lld bytes\n", __func__, s[0].val);

	s[1].tag = VIRTIO_BALLOON_S_SWAP_OUT;
	s[1].val = uvmexp.pgswapout * PAGE_SIZE;
	VIOMBDEBUG("%s: swapped out : %lld bytes\n", __func__, s[1].val);

	s[2].tag = VIRTIO_BALLOON_S_MAJFLT;
	s[2].val = uvmexp.faults;
	VIOMBDEBUG("%s: faults : %lld\n", __func__, s[2].val);

	s[3].tag = VIRTIO_BALLOON_S_MEMFREE;
	s[3].val = uvmexp.free * PAGE_SIZE;
	VIOMBDEBUG("%s: free : %lld bytes\n", __func__, s[3].val);

	s[4].tag = VIRTIO_BALLOON_S_MEMTOT;
	s[4].val = uvmexp.npages * PAGE_SIZE;
	VIOMBDEBUG("%s: total : %lld bytes\n", __func__, s[4].val);

	s[5].tag = VIRTIO_BALLOON_S_CACHES;
	s[5].val = bcstats.numbufpages * PAGE_SIZE;
	VIOMBDEBUG("%s: cache : %lld  bytes\n", __func__, s[5].val);
}
