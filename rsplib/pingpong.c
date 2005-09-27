/*
 * The rsplib Prototype -- An RSerPool Implementation.
 * Copyright (C) 2005 by Thomas Dreibholz, dreibh@exp-math.uni-essen.de
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

#include "rserpool.h"
#include "loglevel.h"
#include "breakdetector.h"
#include "pingpongpackets.h"
#include "netutilities.h"
#ifdef ENABLE_CSP
#include "componentstatusreporter.h"
#include "randomizer.h"
#endif


/* ###### Main program ################################################### */
int main(int argc, char** argv)
{
   const char*                  poolHandle   = "PingPongPool";
   unsigned long long           pingInterval = 500000;
   union rserpool_notification* notification;
   struct rsp_info              info;
   struct rsp_sndrcvinfo        rinfo;
   char                         buffer[65536 + 4];
   char                         str[128];
   struct pollfd                ufds;
   struct Ping*                 ping;
   const struct Pong*           pong;
   unsigned long long           now;
   unsigned long long           nextPing;
   uint64_t                     messageNo;
   uint64_t                     lastReplyNo;
   uint64_t                     recvMessageNo;
   uint64_t                     recvReplyNo;
   unsigned long long           lastPing;
   ssize_t                      received;
   ssize_t                      sent;
   int                          result;
   int                          flags;
   int                          sd;
   int                          i;

   memset(&info, 0, sizeof(info));

   for(i = 1;i < argc;i++) {
      if(!(strncmp(argv[i], "-log" ,4))) {
         if(initLogging(argv[i]) == false) {
            exit(1);
         }
      }
#ifdef ENABLE_CSP
      else if(!(strncmp(argv[i], "-csp" ,4))) {
         if(initComponentStatusReporter(&info, argv[i]) == false) {
            exit(1);
         }
      }
#endif
      else if(!(strncmp(argv[i], "-poolhandle=" ,12))) {
         poolHandle = (char*)&argv[i][12];
      }
      else if(!(strncmp(argv[i], "-interval=" ,10))) {
         pingInterval = 1000ULL * atol((const char*)&argv[i][10]);
      }
      else {
         printf("ERROR: Bad argument %s\n", argv[i]);
         exit(1);
      }
   }
#ifdef ENABLE_CSP
   info.ri_csp_identifier = CID_COMPOUND(CID_GROUP_POOLUSER, random64());
#endif


   puts("Ping Pong Pool User - Version 1.0");
   puts("=================================\n");
   printf("Pool Handle   = %s\n", poolHandle);
   printf("Ping Interval = %Lums\n\n", pingInterval);


   beginLogging();

   if(rsp_initialize(&info) < 0) {
      logerror("Unable to initialize rsplib");
      exit(1);
   }

   sd = rsp_socket(0, SOCK_SEQPACKET, IPPROTO_SCTP);
   if(sd < 0) {
      logerror("Unable to create RSerPool socket");
      exit(1);
   }
   if(rsp_connect(sd, (unsigned char*)poolHandle, strlen(poolHandle)) < 0) {
      logerror("Unable to connect to pool element");
      exit(1);
   }

   messageNo =   1;
   lastReplyNo = 0;
   lastPing    = 0;
   installBreakDetector();
   while(!breakDetected()) {
      ufds.fd     = sd;
      ufds.events = POLLIN;
      nextPing = lastPing + pingInterval;
      now      = getMicroTime();
      result = rsp_poll(&ufds, 1,
                        (nextPing <= now) ? 0 : (int)((nextPing - now) / 1000));

      /* ###### Handle Pong message ###################################### */
      if(result > 0) {
         if(ufds.revents & POLLIN) {
            flags = 0;
            received = rsp_recvmsg(sd, (char*)&buffer, sizeof(buffer),
                                   &rinfo, &flags, 0);
            if(received > 0) {
               if(flags & MSG_RSERPOOL_NOTIFICATION) {
                  notification = (union rserpool_notification*)&buffer;
                  printf("\x1b[39;47mNotification: ");
                  rsp_print_notification(notification, stdout);
                  puts("\x1b[0m");
                  if((notification->rn_header.rn_type == RSERPOOL_FAILOVER) &&
                     (notification->rn_failover.rf_state == RSERPOOL_FAILOVER_NECESSARY)) {
                     puts("FAILOVER...");
                     rsp_forcefailover(sd);
                  }
               }
               else {
                  pong = (const struct Pong*)&buffer;
                  if((pong->Header.Type == PPPT_PONG) &&
                     (ntohs(pong->Header.Length) >= (ssize_t)sizeof(struct Pong))) {
                     recvMessageNo = ntoh64(pong->MessageNo);
                     recvReplyNo   = ntoh64(pong->ReplyNo);

                     printf("\x1b[34m");
                     printTimeStamp(stdout);
                     printf("Ack #%Lu from PE $%08x (reply #%Lu)\x1b[0m\n",
                           recvMessageNo, rinfo.rinfo_pe_id, recvReplyNo);

                     if(recvReplyNo - lastReplyNo != 1) {
                        printTimeStamp(stdout);
                        printf("*** Detected gap of %Ld! ***\n",
                               (int64)recvReplyNo - (int64)lastReplyNo);
                     }
                     lastReplyNo = recvReplyNo;
                  }
               }
            }
         }
      }

      /* ###### Send Ping message ###################################### */
      if(getMicroTime() - lastPing >= pingInterval) {
         lastPing = getMicroTime();

         snprintf((char*)&str,sizeof(str),
                  "Nachricht: %Lu, Zeitstempel: %llu",
                  messageNo, lastPing);
         size_t dataLength = strlen(str);

         ping = (struct Ping*)&buffer;
         ping->Header.Type   = PPPT_PING;
         ping->Header.Flags  = 0x00;
         ping->Header.Length = htons(sizeof(struct Ping) + dataLength);
         ping->MessageNo     = hton64(messageNo);
         memcpy(&ping->Data, str, dataLength);

         sent = rsp_sendmsg(sd, (char*)ping, sizeof(struct Ping) + dataLength, 0,
                            0, htonl(PPID_PPP), 0, 0, 0);
         if(sent > 0) {
            printf("Message #%Ld sent\n", messageNo);
            messageNo++;
         }
      }

   }

   puts("\x1b[0m\nTerminated!");
   rsp_close(sd);
   rsp_cleanup();
   finishLogging();
   return 0;
}