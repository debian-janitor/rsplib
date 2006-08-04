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

#include "tdtypes.h"
#include "messagebuffer.h"
#include "netutilities.h"
#include "loglevel.h"


/* ###### Constructor #################################################### */
struct MessageBuffer* messageBufferNew(size_t bufferSize, const bool useEOR)
{
   struct MessageBuffer* messageBuffer = (struct MessageBuffer*)malloc(sizeof(struct MessageBuffer));
   if(messageBuffer) {
      messageBuffer->Buffer = (char*)malloc(bufferSize);
      if(messageBuffer->Buffer == NULL) {
         free(messageBuffer);
         return(NULL);
      }
      messageBuffer->BufferSize = bufferSize;
      messageBuffer->BufferPos  = 0;
      messageBuffer->UseEOR     = useEOR;
   }
   return(messageBuffer);
}


/* ###### Destructor ##################################################### */
void messageBufferDelete(struct MessageBuffer* messageBuffer)
{
   if(messageBuffer->Buffer) {
      free(messageBuffer->Buffer);
      messageBuffer->Buffer = NULL;
   }
   messageBuffer->BufferSize = 0;
   messageBuffer->BufferPos  = 0;
   free(messageBuffer);
}


/* ###### Read message ################################################### */
ssize_t messageBufferRead(struct MessageBuffer*    messageBuffer,
                          int                      sockfd,
                          int*                     flags,
                          struct sockaddr*         from,
                          socklen_t*               fromlen,
                          uint32_t*                ppid,
                          sctp_assoc_t*            assocID,
                          uint16_t*                streamID,
                          const unsigned long long timeout)
{
   ssize_t result;
   /* ssize_t i; */

   LOG_VERBOSE4
   fprintf(stdlog, "Reading into message buffer from socket %d: offset=%u, max=%u\n",
           sockfd, messageBuffer->BufferPos, messageBuffer->BufferSize);
   LOG_END
   result = recvfromplus(sockfd,
                         (char*)&messageBuffer->Buffer[messageBuffer->BufferPos],
                         messageBuffer->BufferSize - messageBuffer->BufferPos,
                         flags, from, fromlen, ppid, assocID, streamID, timeout);
   LOG_VERBOSE4
   fprintf(stdlog, "Read result for socket %d is %d, EOR=%s, NOTIFICATION=%s, useEOR=%s\n",
           sockfd, result,
           (*flags & MSG_EOR) ? "yes" : "no",
           (*flags & MSG_NOTIFICATION) ? "yes" : "no",
           (messageBuffer->UseEOR == true) ? "yes" : "no");
   LOG_END
   if(result > 0) {
      messageBuffer->BufferPos += (size_t)result;
      if( (messageBuffer->UseEOR) && (!(*flags & MSG_EOR)) ) {
         LOG_VERBOSE4
         fprintf(stdlog, "Partially read %d bytes on socket %d\n", result, sockfd);
         LOG_END
         result = MBRead_Partial;
      }
      else {
         LOG_VERBOSE4
         fprintf(stdlog, "Partially read %d bytes on socket %d, message of %u bytes completed\n",
                 result, sockfd, messageBuffer->BufferPos);
         LOG_END
         /*
         LOG_VERBOSE5
         for(i = 0;i < result;i++) {
            printf("%02x ", ((unsigned char*)messageBuffer->Buffer)[i]);
         }
         puts("");
         LOG_END
         */
         result = messageBuffer->BufferPos;
         messageBuffer->BufferPos = 0;
      }
   }
   else if(result < 0) {
      result = MBRead_Error;
   }
   return(result);
}


/* ###### Reset message buffer ########################################### */
void messageBufferReset(struct MessageBuffer* messageBuffer)
{
   messageBuffer->BufferPos = 0;
}


/* ###### Does message buffer contain a fragment? ######################## */
bool messageBufferHasPartial(struct MessageBuffer* messageBuffer)
{
   return(messageBuffer->BufferPos > 0);
}