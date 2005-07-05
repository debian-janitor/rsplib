/*
 * An Efficient RSerPool Pool Handlespace Management Implementation
 * Copyright (C) 2004-2005 by Thomas Dreibholz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Contact: dreibh@exp-math.uni-essen.de
 *
 */

#ifndef POOLHANDLESPACECHECKSUM_H
#define POOLHANDLESPACECHECKSUM_H


#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef uint32_t HandlespaceChecksumType;

#define INITIAL_HANDLESPACE_CHECKSUM 0x00000000


HandlespaceChecksumType handlespaceChecksumAdd(const HandlespaceChecksumType a,
                                               const HandlespaceChecksumType b);
HandlespaceChecksumType handlespaceChecksumSub(const HandlespaceChecksumType a,
                                               const HandlespaceChecksumType b);
HandlespaceChecksumType handlespaceChecksumCompute(HandlespaceChecksumType checksum,
                                                   const char*             buffer,
                                                   size_t                  size);

#ifdef __cplusplus
}
#endif

#endif
