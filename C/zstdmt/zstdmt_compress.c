
/**
 * Copyright (c) 2016 Tino Reichardt
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 * You can contact the author at:
 * - zstdmt source repository: https://github.com/mcmilk/zstdmt
 */

#include <stdlib.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include "mem.h"
#include "threading.h"
#include "list.h"
#include "zstdmt.h"

/**
 * multi threaded zstd - multiple workers version
 *
 * - each thread works on his own
 * - no main thread which does reading and then starting the work
 * - needs a callback for reading / writing
 * - each worker does his:
 *   1) get read mutex and read some input
 *   2) release read mutex and do compression
 *   3) get write mutex and write result
 *   4) begin with step 1 again, until no input
 */

/* worker for compression */
typedef struct {
	ZSTDMT_CCtx *ctx;
	ZSTD_CStream *zctx;
	pthread_t pthread;
} cwork_t;

struct writelist;
struct writelist {
	size_t frame;
	ZSTDMT_Buffer out;
	struct list_head node;
};

struct ZSTDMT_CCtx_s {

	/* level: 1..22 */
	int level;

	/* threads: 1..ZSTDMT_THREAD_MAX */
	int threads;

	/* should be used for read from input */
	int inputsize;

	/* statistic */
	size_t insize;
	size_t outsize;
	size_t curframe;
	size_t frames;

	/* threading */
	cwork_t *cwork;

	/* reading input */
	pthread_mutex_t read_mutex;
	fn_read *fn_read;
	void *arg_read;

	/* writing output */
	pthread_mutex_t write_mutex;
	fn_write *fn_write;
	void *arg_write;

	/* lists for writing queue */
	struct list_head writelist_free;
	struct list_head writelist_busy;
	struct list_head writelist_done;
};

/* **************************************
 * Compression
 ****************************************/

ZSTDMT_CCtx *ZSTDMT_createCCtx(int threads, int level, int inputsize)
{
	ZSTDMT_CCtx *ctx;
	int t;

	/* allocate ctx */
	ctx = (ZSTDMT_CCtx *) malloc(sizeof(ZSTDMT_CCtx));
	if (!ctx)
		return 0;

	/* check threads value */
	if (threads < 1 || threads > ZSTDMT_THREAD_MAX)
		return 0;

	/* check level */
	if (level < 1 || level > ZSTDMT_LEVEL_MAX)
		return 0;

	/* calculate chunksize for one thread */
	if (inputsize)
		ctx->inputsize = inputsize;
	else {
		const int mb[] = {
			2, 2, 4, 4, 6, 6, 6,	/* 1 - 7 */
			8, 8, 8, 8, 8, 8, 8,	/* 8 - 14 */
			16, 16, 16, 16, 16, 16, 16, 16	/* 15 - 22 */
		};
		ctx->inputsize = 1024 * 1024 * mb[level - 1];
	}

	/* setup ctx */
	ctx->level = level;
	ctx->threads = threads;
	ctx->insize = 0;
	ctx->outsize = 0;
	ctx->frames = 0;
	ctx->curframe = 0;

	pthread_mutex_init(&ctx->read_mutex, NULL);
	pthread_mutex_init(&ctx->write_mutex, NULL);

	INIT_LIST_HEAD(&ctx->writelist_free);
	INIT_LIST_HEAD(&ctx->writelist_busy);
	INIT_LIST_HEAD(&ctx->writelist_done);

	ctx->cwork = (cwork_t *) malloc(sizeof(cwork_t) * threads);
	if (!ctx->cwork)
		goto err_cwork;

	for (t = 0; t < threads; t++) {
		cwork_t *w = &ctx->cwork[t];
		w->ctx = ctx;
		w->zctx = ZSTD_createCStream();
		if (!w->zctx)
			goto err_zctx;
	}

	return ctx;

 err_zctx:
	{
		int i;
		for (i = 0; i < t; i++) {
			cwork_t *w = &ctx->cwork[t];
			free(w->zctx);
		}
	}
	free(ctx->cwork);
 err_cwork:
	free(ctx);

	return 0;
}

/**
 * pt_write - queue for compressed output
 */
static size_t pt_write(ZSTDMT_CCtx * ctx, struct writelist *wl)
{
	struct list_head *entry;

	/* move the entry to the done list */
	list_move(&wl->node, &ctx->writelist_done);

	/* the entry isn't the currently needed, return...  */
	if (wl->frame != ctx->curframe)
		return 0;

 again:
	/* check, what can be written ... */
	list_for_each(entry, &ctx->writelist_done) {
		wl = list_entry(entry, struct writelist, node);
		if (wl->frame == ctx->curframe) {
			int rv = ctx->fn_write(ctx->arg_write, &wl->out);
			if (rv == -1)
				return ERROR(write_fail);
			ctx->outsize += wl->out.size;
			ctx->curframe++;
			list_move(entry, &ctx->writelist_free);
			goto again;
		}
	}

	return 0;
}

static void *pt_compress(void *arg)
{
	cwork_t *w = (cwork_t *) arg;
	ZSTDMT_CCtx *ctx = w->ctx;
	struct writelist *wl;
	size_t result;
	ZSTDMT_Buffer in;

	/* inbuf is constant */
	in.size = ctx->inputsize;
	in.buf = malloc(in.size);
	if (!in.buf)
		return (void *)ERROR(memory_allocation);

	for (;;) {
		struct list_head *entry;
		ZSTDMT_Buffer *out;
		int rv;

		/* allocate space for new output */
		pthread_mutex_lock(&ctx->write_mutex);
		if (!list_empty(&ctx->writelist_free)) {
			/* take unused entry */
			entry = list_first(&ctx->writelist_free);
			wl = list_entry(entry, struct writelist, node);
			wl->out.size = ZSTD_compressBound(ctx->inputsize) + 12;
			list_move(entry, &ctx->writelist_busy);
		} else {
			/* allocate new one */
			wl = (struct writelist *)
			    malloc(sizeof(struct writelist));
			if (!wl) {
				pthread_mutex_unlock(&ctx->write_mutex);
				free(in.buf);
				return (void *)ERROR(memory_allocation);
			}
			wl->out.size = ZSTD_compressBound(ctx->inputsize) + 12;;
			wl->out.buf = malloc(wl->out.size);
			if (!wl->out.buf) {
				pthread_mutex_unlock(&ctx->write_mutex);
				free(in.buf);
				return (void *)ERROR(memory_allocation);
			}
			list_add(&wl->node, &ctx->writelist_busy);
		}
		pthread_mutex_unlock(&ctx->write_mutex);
		out = &wl->out;

		/* read new input */
		pthread_mutex_lock(&ctx->read_mutex);
		in.size = ctx->inputsize;
		rv = ctx->fn_read(ctx->arg_read, &in);
		if (rv == -1) {
			pthread_mutex_unlock(&ctx->read_mutex);
			result = ERROR(read_fail);
			goto error;
		}

		/* eof */
		if (in.size == 0) {
			free(in.buf);
			pthread_mutex_unlock(&ctx->read_mutex);

			pthread_mutex_lock(&ctx->write_mutex);
			list_move(&wl->node, &ctx->writelist_free);
			pthread_mutex_unlock(&ctx->write_mutex);

			goto okay;
		}
		ctx->insize += in.size;
		wl->frame = ctx->frames++;
		pthread_mutex_unlock(&ctx->read_mutex);

		/* compress whole frame */
		result = ZSTD_initCStream(w->zctx, ctx->level);
		if (ZSTD_isError(result)) {
			zstdmt_errcode = result;
			wl->out.buf = (void *)ZSTD_getErrorName(result);
			goto error;
		}

		{
			ZSTD_inBuffer input;
			input.src = in.buf;
			input.size = in.size;
			input.pos = 0;

			ZSTD_outBuffer output;
			output.dst = (char *)out->buf + 12;
			output.size = out->size - 12;
			output.pos = 0;

			result = ZSTD_compressStream(w->zctx, &output, &input);
			if (ZSTD_isError(result)) {
				/* user can lookup that code */
				zstdmt_errcode = result;
				result = ERROR(compression_library);
				goto error;
			}

			if (input.size != input.pos) {
				result = ERROR(frame_compress);
				goto error;
			}

			result = ZSTD_endStream(w->zctx, &output);
			if (ZSTD_isError(result)) {
				zstdmt_errcode = result;
				result = ERROR(compression_library);
				goto error;
			}

			result = output.pos;
		}

		/* write skippable frame */
		MEM_writeLE32((unsigned char *)out->buf + 0,
			      ZSTDMT_MAGIC_SKIPPABLE);
		MEM_writeLE32((unsigned char *)out->buf + 4, 4);
		MEM_writeLE32((unsigned char *)out->buf + 8, (U32)result);
		out->size = result + 12;

		/* write result */
		pthread_mutex_lock(&ctx->write_mutex);
		result = pt_write(ctx, wl);
		pthread_mutex_unlock(&ctx->write_mutex);
		if (ZSTDMT_isError(result))
			goto error;
	}

 okay:
	return 0;
 error:
	pthread_mutex_lock(&ctx->write_mutex);
	list_move(&wl->node, &ctx->writelist_free);
	pthread_mutex_unlock(&ctx->write_mutex);
	return (void *)result;
}

size_t ZSTDMT_CompressCCtx(ZSTDMT_CCtx * ctx, ZSTDMT_RdWr_t * rdwr)
{
	int t;

	if (!ctx)
		return ERROR(compressionParameter_unsupported);

	/* init reading and writing functions */
	ctx->fn_read = rdwr->fn_read;
	ctx->fn_write = rdwr->fn_write;
	ctx->arg_read = rdwr->arg_read;
	ctx->arg_write = rdwr->arg_write;

	/* start all workers */
	for (t = 0; t < ctx->threads; t++) {
		cwork_t *w = &ctx->cwork[t];
		pthread_create(&w->pthread, NULL, pt_compress, w);
	}

	/* wait for all workers */
	for (t = 0; t < ctx->threads; t++) {
		cwork_t *w = &ctx->cwork[t];
		void *p;
		pthread_join(w->pthread, &p);
		if (p)
			return (size_t) p;
	}

	/* clean up lists */
	while (!list_empty(&ctx->writelist_free)) {
		struct writelist *wl;
		struct list_head *entry;
		entry = list_first(&ctx->writelist_free);
		wl = list_entry(entry, struct writelist, node);
		free(wl->out.buf);
		list_del(&wl->node);
		free(wl);
	}

	return 0;
}

/* returns current uncompressed data size */
size_t ZSTDMT_GetInsizeCCtx(ZSTDMT_CCtx * ctx)
{
	if (!ctx)
		return 0;

	return ctx->insize;
}

/* returns the current compressed data size */
size_t ZSTDMT_GetOutsizeCCtx(ZSTDMT_CCtx * ctx)
{
	if (!ctx)
		return 0;

	return ctx->outsize;
}

/* returns the current compressed frames */
size_t ZSTDMT_GetFramesCCtx(ZSTDMT_CCtx * ctx)
{
	if (!ctx)
		return 0;

	return ctx->curframe;
}

void ZSTDMT_freeCCtx(ZSTDMT_CCtx * ctx)
{
	int t;

	if (!ctx)
		return;

	for (t = 0; t < ctx->threads; t++) {
		cwork_t *w = &ctx->cwork[t];
		ZSTD_freeCStream(w->zctx);
	}
	free(ctx->cwork);
	free(ctx);
	ctx = 0;

	return;
}
