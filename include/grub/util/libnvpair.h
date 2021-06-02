/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GRUB_LIBNVPAIR_UTIL_HEADER
#define GRUB_LIBNVPAIR_UTIL_HEADER 1

#include <config.h>

#ifdef HAVE_LIBNVPAIR_H
#include <libnvpair.h>
#else /* ! HAVE_LIBNVPAIR_H */

#include <stdio.h>	/* FILE */

typedef enum {
        DATA_TYPE_DONTCARE = -1,
        DATA_TYPE_UNKNOWN = 0,
        DATA_TYPE_BOOLEAN,
	DATA_TYPE_BYTE,
        DATA_TYPE_INT16,
        DATA_TYPE_UINT16,
	DATA_TYPE_INT32,
        DATA_TYPE_UINT32,
        DATA_TYPE_INT64,
	DATA_TYPE_UINT64,
        DATA_TYPE_STRING,
        DATA_TYPE_BYTE_ARRAY,
        DATA_TYPE_INT16_ARRAY,
	DATA_TYPE_UINT16_ARRAY,
        DATA_TYPE_INT32_ARRAY,
        DATA_TYPE_UINT32_ARRAY,
        DATA_TYPE_INT64_ARRAY,
	DATA_TYPE_UINT64_ARRAY,
        DATA_TYPE_STRING_ARRAY,
        DATA_TYPE_HRTIME,
	DATA_TYPE_NVLIST,
        DATA_TYPE_NVLIST_ARRAY,
        DATA_TYPE_BOOLEAN_VALUE,
        DATA_TYPE_INT8,
	DATA_TYPE_UINT8,
        DATA_TYPE_BOOLEAN_ARRAY,
        DATA_TYPE_INT8_ARRAY,
#if !defined(_KERNEL) && !defined(_STANDALONE)
        DATA_TYPE_UINT8_ARRAY,
        DATA_TYPE_DOUBLE
#else
     	DATA_TYPE_UINT8_ARRAY
#endif
} data_type_t;

typedef void nvlist_t;
typedef void nvpair_t;

/* nvlist pack encoding */
#define NV_ENCODE_NATIVE        0
#define NV_ENCODE_XDR           1

/* nvlist persistent unique name flags, stored in nvl_nvflags */
#define NV_UNIQUE_NAME          0x1
#define NV_UNIQUE_NAME_TYPE     0x2

int nvlist_alloc (nvlist_t **, unsigned int, int);
void nvlist_free (nvlist_t *);
int nvlist_size (nvlist_t *, size_t *, int);

int nvlist_lookup_string (nvlist_t *, const char *, char **);
int nvlist_lookup_nvlist (nvlist_t *, const char *, nvlist_t **);
int nvlist_lookup_nvlist_array (nvlist_t *, const char *, nvlist_t ***, unsigned int *);
int nvlist_lookup_uint64 (nvlist_t *, const char *, u_int64_t *);

nvpair_t *nvlist_next_nvpair (nvlist_t *, nvpair_t *);
char *nvpair_name (nvpair_t *);
data_type_t nvpair_type (nvpair_t *);
int nvpair_value_string (nvpair_t *, char **);

int nvlist_add_string (nvlist_t *, const char *, const char *);
int nvlist_add_uint64 (nvlist_t *, const char *, u_int64_t);

#endif /* ! HAVE_LIBNVPAIR_H */

#endif
