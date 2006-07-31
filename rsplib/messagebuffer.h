/*
 * The rsplib Prototype -- An RSerPool Implementation.
 * Copyright (C) 2005-2006 by Thomas Dreibholz, dreibh@exp-math.uni-essen.de
 *
 * $Id$
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Contact: rsplib-discussion@sctp.de
 *          dreibh@iem.uni-due.de
 *
 */

#ifndef MESSAGEBUFFER_H
#define MESSAGEBUFFER_H


#include "tdtypes.h"

#include <ext_socket.h>


#ifdef __cplusplus
extern "C" {
#endif


struct MessageBuffer
{
   char*  Buffer;
   size_t BufferSize;
   size_t BufferPos;
   bool   UseEOR;
};


#define MBRead_Error   -1
#define MBRead_Partial -2


/**
  * Constructor.
  *
  * @param bufferSize Size of buffer to be allocated.
  * @param useEOR true to use MSG_EOR (SCTP), false otherwise (TCP, UDP).
  * @return MessageBuffer object or NULL in case of error.
  */
struct MessageBuffer* messageBufferNew(size_t bufferSize, const bool useEOR);


/**
  * Destructor.
  *
  * @param messageBuffer MessageBuffer.
  */
void messageBufferDelete(struct MessageBuffer* messageBuffer);


/**
  * Read message.
  *
  * @param messageBuffer MessageBuffer.
  * @param sockfd Socket descriptor.
  * @param flags Reference to store recvmsg() flags.
  * @param from Reference to store source address to or NULL if not necessary.
  * @param fromlen Reference to store source address length to or NULL if not necessary.
  * @param ppid Reference to store SCTP Payload Protocol Identifier to.
  * @param assocID Reference to store SCTP Association ID to.
  * @param streamID Reference to store SCTP Stream ID to.
  * @param timeToLive Reference to store SCTP Time To Live to.
  * @param timeout Timeout for receiving data.
  * @param Bytes read or MBRead_Error in case of error or MBRead_Partial in case of partial read!
  */

ssize_t messageBufferRead(struct MessageBuffer*    messageBuffer,
                          int                      sockfd,
                          int*                     flags,
                          struct sockaddr*         from,
                          socklen_t*               fromlen,
                          uint32_t*                ppid,
                          sctp_assoc_t*            assocID,
                          uint16_t*                streamID,
                          const unsigned long long timeout);

#ifdef __cplusplus
}
#endif

#endif
