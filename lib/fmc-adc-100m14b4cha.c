/*
 * The ADC library for the specific card
 *
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <linux/zio-user.h>

#include "fmcadc-lib.h"
#include "fmcadc-lib-int.h"

#define ZIO_DEV_PATH "/dev/zio"
#define ZIO_SYS_PATH "/sys/bus/zio/devices"

#define FMCADC_NCHAN 4

/* * * * * * * * * *  Library Operations Implementation * * * * * * * * * * */
int fmcadc_zio_stop_acquisition(struct fmcadc_dev *dev,
				       unsigned int flags);

struct fmcadc_dev *fmcadc_zio_open(const struct fmcadc_board_type *dev,
					  unsigned int dev_id,
					  unsigned int details)
{
	struct __fmcadc_dev_zio *fa = NULL;
	struct stat st;
	char *syspath, *devpath, fname[128];
	int udev_zio_dir = 1;

	if (strlen(dev->devname) > 12) {
		fprintf(stderr,
				"%s: name \"%s\" is too long. ZIO's name are 12byte\n",
				__func__, dev->devname);
		return NULL ;
	}

	/* check if device exists by looking in ZIO sysfs */
	asprintf(&syspath, "%s/%s-%04x", ZIO_SYS_PATH, dev->devname, dev_id);
	if (stat(syspath, &st)) {
		goto out_fa_stat;
	}

	/* Check where are ZIO char devices x*/
	if (stat(ZIO_DEV_PATH, &st)) {
		/*
		 * ZIO driver are not in /dev/zio, but in /dev with all other
		 * drivers
		 */
		udev_zio_dir = 0;
	}
	asprintf(&devpath, "%s/%s-%04x", (udev_zio_dir ? ZIO_DEV_PATH : "/dev"),
			dev->devname, dev_id);

	/* Path exists, so device is there */

	fa = calloc(1, sizeof(*fa));
	if (!fa) {
		goto out_fa_alloc;
	}
	fa->sysbase = syspath;
	fa->devbase = devpath;
	fa->cset = details;

	/* Open char devices */
	sprintf(fname, "%s-0-i-ctrl", fa->devbase);
	fa->fdc = open(fname, O_RDONLY);
	sprintf(fname, "%s-0-i-data", fa->devbase);
	fa->fdd = open(fname, O_RDONLY);
	if (fa->fdc < 0 || fa->fdd < 0)
	    goto out_fa_open;

	fa->gid.board = dev;

	/* Finally, support verbose operation */
	if (getenv("LIB_FMCADC_VERBOSE"))
		fa->flags |= FMCADC_FLAG_VERBOSE;

	return (void *) &fa->gid;

out_fa_open:
	free(fa);
out_fa_alloc:
	free(devpath);
out_fa_stat:
	free(syspath);

	return NULL ;
}

struct fmcadc_dev *fmcadc_zio_open_by_lun(char *name, int lun)
{
	/* TODO implement*/
	return NULL ;
}
int fmcadc_zio_close(struct fmcadc_dev *dev)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);

	/* If char device are open, close it */
	if (fa->fdc >= 0)
		close(fa->fdc);
	fa->fdc = -1;
	if (fa->fdd >= 0)
		close(fa->fdd);
	fa->fdd = -1;

	/* Stop active acquisition */
	fmcadc_zio_stop_acquisition(dev, 0);

	free(fa->sysbase);
	free(fa->devbase);
	free(fa);
	return 0;
}
/* Handle acquisition */
int fmcadc_zio_start_acquisition(struct fmcadc_dev *dev,
		unsigned int flags, struct timeval *timeout)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);
	uint32_t cmd;
	fd_set set;
	int err;

	if (fa->fdc < 0) {
		errno = EIO;
		return -1;
	}

	cmd = 1;
	err = fa_zio_sysfs_set(fa, "cset0/fsm-command", &cmd);
	if (err) {
		/*
		 * It returns error when we cannot start
		 */
		return err;
	}

	/* So, first sample and blocking read. Wait.. */
	FD_ZERO(&set);
	FD_SET(fa->fdc, &set);
	err = select(fa->fdc + 1, &set, NULL, NULL, timeout);
	switch (err) {
	case 0:
		errno = EAGAIN;
		return -1;
	case 1:
		return 0;
	default:
		return err;
	}
}
int fmcadc_zio_stop_acquisition(struct fmcadc_dev *dev,
		unsigned int flags)
{
	struct __fmcadc_dev_zio *fa = to_dev_zio(dev);
	uint32_t cmd = 2;

	return fa_zio_sysfs_set(fa, "cset0/fsm-command", &cmd);
}



