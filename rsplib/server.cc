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
#include "standardservices.h"
#ifdef ENABLE_CSP
#include "componentstatusreporter.h"
#endif


#define SERVICE_ECHO     1
#define SERVICE_DISCARD  2
#define SERVICE_DAYTIME  3
#define SERVICE_CHARGEN  4

#define SERVICE_PINGPONG 5
#define SERVICE_FRACTAL  6


/* ###### Main program ################################################### */
int main(int argc, char** argv)
{
   struct rsp_info info;
   unsigned int    service    = SERVICE_ECHO;
   const char*     poolHandle = "EchoPool";

   memset(&info, 0, sizeof(info));
   for(int i = 1;i < argc;i++) {
      if(!(strncmp(argv[i], "-log" ,4))) {
         if(initLogging(argv[i]) == false) {
            exit(1);
         }
      }
#ifdef ENABLE_CSP
      if(!(strncmp(argv[i], "-csp" ,4))) {
         if(initComponentStatusReporter(&info, argv[i]) == false) {
            exit(1);
         }
      }
#endif
      else if(!(strncmp(argv[i], "-poolhandle=" ,12))) {
         poolHandle = (char*)&argv[i][12];
      }
      else if(!(strcmp(argv[i], "-echo"))) {
         service = SERVICE_ECHO;
      }
      else if(!(strcmp(argv[i], "-discard"))) {
         service = SERVICE_DISCARD;
      }
      else if(!(strcmp(argv[i], "-daytime"))) {
         service = SERVICE_DAYTIME;
      }
      else if(!(strcmp(argv[i], "-chargen"))) {
         service = SERVICE_CHARGEN;
      }
      else if(!(strcmp(argv[i], "-pingpong"))) {
         service = SERVICE_PINGPONG;
      }
      else if(!(strcmp(argv[i], "-fractal"))) {
         service = SERVICE_FRACTAL;
      }
   }
#ifdef ENABLE_CSP
   info.ri_csp_identifier = CID_COMPOUND(CID_GROUP_POOLELEMENT, 0);
#endif


   printf("Starting service ");
   if(service == SERVICE_ECHO) {
      printf("Echo");
   }
   else if(service == SERVICE_DISCARD) {
      printf("Discard");
   }
   else if(service == SERVICE_DAYTIME) {
      printf("Daytime");
   }
   else if(service == SERVICE_CHARGEN) {
      printf("Character Generator");
   }
   puts("...");


   beginLogging();

   if(service == SERVICE_ECHO) {
      EchoServer echoServer;
      echoServer.poolElement("Echo Server - Version 1.0",
                             "EchoPool", &info, NULL);
   }
   else if(service == SERVICE_DISCARD) {
      DiscardServer discardServer;
      discardServer.poolElement("Discard Server - Version 1.0",
                                "DiscardPool", &info, NULL);
   }
   else if(service == SERVICE_DAYTIME) {
      DaytimeServer discardServer;
      discardServer.poolElement("Daytime Server - Version 1.0",
                                "DaytimePool", &info, NULL);
   }
   else if(service == SERVICE_CHARGEN) {
      puts("Character Generator");
   }
   else if(service == SERVICE_PINGPONG) {
      PingPongServer::PingPongServerSettings settings;
      settings.FailureAfter = 0;
      for(int i = 1;i < argc;i++) {
         if(!(strncmp(argv[i], "-pppfailureafter=", 17))) {
            settings.FailureAfter = atol((const char*)&argv[i][17]);
         }
      }

      TCPLikeServer::poolElement("Ping Pong Server - Version 1.0",
                                 "PingPongPool", &info, NULL,
                                 (void*)&settings,
                                 PingPongServer::pingPongServerFactory);
   }
   else if(service == SERVICE_FRACTAL) {
      FractalGeneratorServer::FractalGeneratorServerSettings settings;
      settings.TestMode     = false;
      settings.FailureAfter = 0;
      for(int i = 1;i < argc;i++) {
         if(!(strcmp(argv[i], "-fgptestmode"))) {
            settings.TestMode = true;
         }
         else if(!(strncmp(argv[i], "-fgpfailureafter=", 17))) {
            settings.FailureAfter = atol((const char*)&argv[i][17]);
         }
      }

      TCPLikeServer::poolElement("Fractal Generator Server - Version 1.0",
                                 "FractalGeneratorPool", &info, NULL,
                                 (void*)&settings,
                                 FractalGeneratorServer::fractalGeneratorServerFactory);
   }
   puts("");

   finishLogging();
   return(0);
}