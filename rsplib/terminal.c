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
#ifdef ENABLE_CSP
#include "componentstatusreporter.h"
#include "randomizer.h"
#endif


/* ###### Main program ################################################### */
int main(int argc, char** argv)
{
   struct rsp_info       info;
   struct rsp_sndrcvinfo rinfo;
   char                  buffer[65536 + 4];
   char*                 poolHandle = "EchoPool";
   struct pollfd         ufds[2];
   ssize_t               received;
   ssize_t               sent;
   int                   result;
   int                   flags;
   int                   sd;
   int                   i;

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
      else {
         printf("ERROR: Bad argument %s\n", argv[i]);
         exit(1);
      }
   }
#ifdef ENABLE_CSP
   info.ri_csp_identifier = CID_COMPOUND(CID_GROUP_POOLUSER, random64());
#endif


   puts("RSerPool Terminal - Version 1.0");
   puts("===============================\n");
   printf("Pool Handle = %s\n\n", poolHandle);


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

   installBreakDetector();
   while(!breakDetected()) {
      ufds[0].fd     = STDIN_FILENO;
      ufds[0].events = POLLIN;
      ufds[1].fd     = sd;
      ufds[1].events = POLLIN;
      result = rsp_poll((struct pollfd*)&ufds, 2, -1);
      if(result > 0) {
         if(ufds[0].revents & POLLIN) {
            received = rsp_read(STDIN_FILENO, (char*)&buffer, sizeof(buffer));
            if(received > 0) {
               sent = rsp_write(sd, (char*)&buffer, received);
               if(sent < received) {
                  puts("Write Failure -> Failover!");
                  rsp_forcefailover(sd);
               }
            }
            else {
               break;
            }
         }
         if(ufds[1].revents & POLLIN) {
            flags = 0;
            received = rsp_recvmsg(sd, (char*)&buffer, sizeof(buffer),
                                   &rinfo, &flags, 0);
            if(received > 0) {
               if(flags & MSG_RSERPOOL_NOTIFICATION) {
                  printf("\x1b[39;47mNotification: ");
                  rsp_print_notification((union rserpool_notification*)&buffer, stdout);
                  puts("\x1b[0m");
                  // rsp_forcefailover(sd);
               }
               else {
                  for(i = 0;i < received;i++) {
                     if((unsigned char)buffer[i] < 30) {
                        buffer[i] = '.';
                     }
                  }
                  buffer[i] = 0x00;
                  printf("\x1b[34mfrom PE $%08x> %s\x1b[0m\n",
                         rinfo.rinfo_pe_id, buffer);
                  fflush(stdout);
               }
            }
            else {
               puts("Read Failure -> Failover!");
               // rsp_forcefailover(sd);
            }
         }
      }
   }

   puts("\nTerminated!");
   rsp_close(sd);
   rsp_cleanup();
   finishLogging();
   return 0;
}
