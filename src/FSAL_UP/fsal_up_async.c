/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @addtogroup fsal_up
 * @{
 */

/**
 * @file fsal_up_async.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Asynchrony wrappers for FSAL Upcall system
 *
 * This is not the most elegant design in history, but should be
 * reasonably efficient.  At present, we have to copy the key supplied
 * rather than saving a pointer.  Once version 2.1 comes out and we
 * can from the FSAL object to the cache entry with container of,
 * we'll be able to jump up, grab a ref on the cache entry, and just
 * store the pointer.
 *
 * Every async call requires one allocation and one queue into the
 * thread fridge.  We make the thread fridge a parameter, so an FSAL
 * that's expecting to shoot out lots and lots of upcalls can make one
 * holding several threads wide.
 *
 * Every async call takes a callback function and an argument, to
 * allow it to receive errors.  The callback function may be NULL if
 * the caller doesn't care.  This doesn't affect methods that may be
 * called asynchronously by upcall handlers like @c layoutreturn.
 *
 * Every async call takes a reference on the export, the queued action
 * returns it after execution.
 *
 * Every async call returns 0 on success and a POSIX error code on error.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "fsal_up.h"
#include "fsal_convert.h"
#include "sal_functions.h"
#include "pnfs_utils.h"

/* Invalidate */

struct invalidate_args {
	struct fsal_export *export;
	struct gsh_buffdesc obj;
	uint32_t flags;
	void (*cb)(void *, fsal_status_t);
	void *cb_arg;
	char key[];
};

static void queue_invalidate(struct fridgethr_context *ctx)
{
	struct invalidate_args *args = ctx->arg;
	fsal_status_t status;

	status = args->export->up_ops->invalidate(args->export, &args->obj,
						  args->flags);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

fsal_status_t up_async_invalidate(struct fridgethr *fr,
				  struct fsal_export *export,
			struct gsh_buffdesc *obj, uint32_t flags,
			void (*cb)(void *, fsal_status_t), void *cb_arg)
{
	struct invalidate_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct invalidate_args) + obj->len);

	args->export = export;
	args->flags = flags;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, obj->addr, obj->len);
	args->obj.addr = args->key;
	args->obj.len = obj->len;

	rc = fridgethr_submit(fr, queue_invalidate, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/* Update */

struct update_args {
	struct fsal_export *export;
	struct gsh_buffdesc obj;
	struct attrlist attr;
	uint32_t flags;
	void (*cb)(void *, fsal_status_t);
	void *cb_arg;
	char key[];
};

static void queue_update(struct fridgethr_context *ctx)
{
	struct update_args *args = ctx->arg;
	fsal_status_t status;

	status = args->export->up_ops->update(args->export, &args->obj,
					      &args->attr, args->flags);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

fsal_status_t up_async_update(struct fridgethr *fr,
			      struct fsal_export *export,
		    struct gsh_buffdesc *obj, struct attrlist *attr,
		    uint32_t flags, void (*cb)(void *, fsal_status_t),
		    void *cb_arg)
{
	struct update_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct update_args) + obj->len);

	args->export = export;
	args->attr = *attr;
	args->flags = flags;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, obj->addr, obj->len);
	args->obj.addr = args->key;
	args->obj.len = obj->len;

	rc = fridgethr_submit(fr, queue_update, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/* Lock grant */

struct lock_grant_args {
	struct fsal_export *export;
	struct gsh_buffdesc file;
	void *owner;
	fsal_lock_param_t lock_param;
	void (*cb)(void *, state_status_t);
	void *cb_arg;
	char key[];
};

static void queue_lock_grant(struct fridgethr_context *ctx)
{
	struct lock_grant_args *args = ctx->arg;
	state_status_t status;

	status = args->export->up_ops->lock_grant(args->export, &args->file,
						  args->owner,
						  &args->lock_param);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

fsal_status_t up_async_lock_grant(struct fridgethr *fr,
				  struct fsal_export *export,
			struct gsh_buffdesc *file, void *owner,
			fsal_lock_param_t *lock_param,
			void (*cb)(void *, state_status_t),
			void *cb_arg)
{
	struct lock_grant_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct lock_grant_args) + file->len);

	args->export = export;
	args->owner = owner;
	args->lock_param = *lock_param;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, file->addr, file->len);
	args->file.addr = args->key;
	args->file.len = file->len;

	rc = fridgethr_submit(fr, queue_lock_grant, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/* Lock avail */

struct lock_avail_args {
	struct fsal_export *export;
	struct gsh_buffdesc file;
	void *owner;
	fsal_lock_param_t lock_param;
	void (*cb)(void *, state_status_t);
	void *cb_arg;
	char key[];
};

static void queue_lock_avail(struct fridgethr_context *ctx)
{
	struct lock_avail_args *args = ctx->arg;
	state_status_t status;

	status = args->export->up_ops->lock_avail(args->export, &args->file,
						  args->owner,
						  &args->lock_param);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

fsal_status_t up_async_lock_avail(struct fridgethr *fr,
				  struct fsal_export *export,
			struct gsh_buffdesc *file, void *owner,
			fsal_lock_param_t *lock_param,
			void (*cb)(void *, state_status_t),
			void *cb_arg)
{
	struct lock_avail_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct lock_avail_args) + file->len);

	args->export = export;
	args->owner = owner;
	args->lock_param = *lock_param;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, file->addr, file->len);
	args->file.addr = args->key;
	args->file.len = file->len;

	rc = fridgethr_submit(fr, queue_lock_avail, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/* Layoutrecall */

struct layoutrecall_args {
	struct fsal_export *export;
	struct gsh_buffdesc handle;
	layouttype4 layout_type;
	bool changed;
	struct pnfs_segment segment;
	void *cookie;
	struct layoutrecall_spec spec;
	void (*cb)(void *, state_status_t);
	void *cb_arg;
	char data[];
};

static void queue_layoutrecall(struct fridgethr_context *ctx)
{
	struct layoutrecall_args *args = ctx->arg;
	state_status_t status;

	status = args->export->up_ops->layoutrecall(args->export, &args->handle,
						    args->layout_type,
						    args->changed,
						    &args->segment,
						    args->cookie,
						    args->spec.how
						    == layoutrecall_not_specced
						    ? NULL : &args->spec);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

fsal_status_t up_async_layoutrecall(struct fridgethr *fr,
				    struct fsal_export *export,
			  struct gsh_buffdesc *handle,
			  layouttype4 layout_type, bool changed,
			  const struct pnfs_segment *segment, void *cookie,
			  struct layoutrecall_spec *spec,
			  void (*cb)(void *, state_status_t),
			  void *cb_arg)
{
	struct layoutrecall_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct layoutrecall_args) + handle->len);

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, handle->addr, handle->len);
	args->handle.addr = args->data;
	args->handle.len = handle->len;

	args->layout_type = layout_type;
	args->changed = changed;
	args->segment = *segment;
	args->cookie = cookie;

	if (spec)
		args->spec = *spec;
	else
		args->spec.how = layoutrecall_not_specced;

	rc = fridgethr_submit(fr, queue_layoutrecall, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/* Notify Device */

struct notify_device_args {
	struct fsal_export *export;
	notify_deviceid_type4 notify_type;
	layouttype4 layout_type;
	struct pnfs_deviceid devid;
	bool immediate;
	void (*cb)(void *, state_status_t);
	void *cb_arg;
	char data[];
};

static void queue_notify_device(struct fridgethr_context *ctx)
{
	struct notify_device_args *args = ctx->arg;
	state_status_t status;

	status = args->export->up_ops->notify_device(args->notify_type,
						     args->layout_type,
						     args->devid,
						     args->immediate);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

fsal_status_t up_async_notify_device(struct fridgethr *fr,
			   struct fsal_export *export,
			   notify_deviceid_type4 notify_type,
			   layouttype4 layout_type,
			   struct pnfs_deviceid *devid,
			   bool immediate, void (*cb)(void *, state_status_t),
			   void *cb_arg)
{
	struct notify_device_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct notify_device_args));

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;
	args->notify_type = notify_type;
	args->layout_type = layout_type;
	args->devid = *devid;
	args->immediate = immediate;

	rc = fridgethr_submit(fr, queue_notify_device, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/* Delegrecall */

struct delegrecall_args {
	struct fsal_export *export;
	struct gsh_buffdesc handle;
	void (*cb)(void *, state_status_t);
	void *cb_arg;
	char key[];
};

static void queue_delegrecall(struct fridgethr_context *ctx)
{
	struct fsal_obj_handle *obj = ctx->arg;

	(void)delegrecall_impl(obj);
}

int async_delegrecall(struct fridgethr *fr, struct fsal_obj_handle *obj)
{
	int rc = fridgethr_submit(fr, queue_delegrecall, obj);
	return rc;
}

static void up_queue_delegrecall(struct fridgethr_context *ctx)
{

	struct delegrecall_args *args = ctx->arg;
	state_status_t status;

	status = args->export->up_ops->delegrecall(args->export, &args->handle);

	if (args->cb)
		args->cb(args->cb_arg, status);

	gsh_free(args);
}

/* XXX dang refcount exports in these? */
fsal_status_t up_async_delegrecall(struct fridgethr *fr,
				   struct fsal_export *export,
			 struct gsh_buffdesc *handle,
			 void (*cb)(void *, state_status_t),
			 void *cb_arg)
{
	struct delegrecall_args *args = NULL;
	int rc = 0;

	args = gsh_malloc(sizeof(struct delegrecall_args) + handle->len);

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->key, handle->addr, handle->len);
	args->handle.addr = args->key;
	args->handle.len = handle->len;

	rc = fridgethr_submit(fr, up_queue_delegrecall, args);

	if (rc != 0)
		gsh_free(args);

	return fsalstat(posix2fsal_error(rc), rc);
}

/** @} */
