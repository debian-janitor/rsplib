/*
 *  $Id: randomizer.h,v 1.1 2004/07/13 09:12:09 dreibh Exp $
 *
 * RSerPool implementation.
 *
 * Realized in co-operation between Siemens AG
 * and University of Essen, Institute of Computer Networking Technology.
 *
 * Acknowledgement
 * This work was partially funded by the Bundesministerium für Bildung und
 * Forschung (BMBF) of the Federal Republic of Germany (Förderkennzeichen 01AK045).
 * The authors alone are responsible for the contents.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * There are two mailinglists available at http://www.sctp.de/rserpool.html
 * which should be used for any discussion related to this implementation.
 *
 * Contact: rsplib-discussion@sctp.de
 *          dreibh@exp-math.uni-essen.de
 *
 * Purpose: Randomizer
 *
 */


#ifndef RANDOMIZER_H
#define RANDOMIZER_H

#ifdef FreeBSD
#include <sys/types.h>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif



/**
  * Get 8-bit random value.
  *
  * @return Random value.
  */
uint8_t random8();

/**
  * Get162-bit random value.
  *
  * @return Random value.
  */
uint16_t random16();

/**
  * Get 32-bit random value.
  *
  * @return Random value.
  */
uint32_t random32();

/**
  * Get 64-bit random value.
  *
  * @return Random value.
  */
uint64_t random64();



#ifdef __cplusplus
}
#endif


#endif
