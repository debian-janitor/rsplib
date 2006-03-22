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
#include "rserpool.h"
#include "calcapppackets.h"
#include "statistics.h"
#include "rsputilities.h"
#include "netutilities.h"
#include "breakdetector.h"
#include "randomizer.h"
#include "loglevel.h"

#include <ext_socket.h>
#include <iostream>


using namespace std;


/* Exit immediately on Ctrl-C. No clean shutdown. */
/* #define FAST_BREAK */

struct Job
{
   Job*               Next;

   unsigned int       JobID;
   double             JobSize;

   unsigned long long CompleteTimeStamp;
   unsigned long long QueuingTimeStamp;
   unsigned long long StartupTimeStamp;
   unsigned long long AcceptTimeStamp;
};

class JobQueue
{
   public:
   JobQueue();
   ~JobQueue();

   void enqueue(Job* job);
   Job* dequeue();

   private:
   Job* FirstJob;
   Job* LastJob;
};


// ###### Constructor #######################################################
JobQueue::JobQueue()
{
   FirstJob = NULL;
   LastJob  = NULL;
}


// ###### Destructor ########################################################
JobQueue::~JobQueue()
{
   Job* currentJob = FirstJob;
   while(currentJob) {
      Job* nextJob =  currentJob->Next;
      delete currentJob;
      currentJob = nextJob;
   }
   FirstJob = NULL;
   LastJob  = NULL;
}


// ###### Enqueue job #######################################################
void JobQueue::enqueue(Job* job)
{
   job->Next             = NULL;
   job->QueuingTimeStamp = getMicroTime();
   job->StartupTimeStamp = 0ULL;

   if((FirstJob == NULL) && (LastJob == NULL)) {
      FirstJob = job;
      LastJob  = job;
   }
   else {
      LastJob ->Next = job;
      LastJob        = job;
   }
}


// ###### Dequeue job #######################################################
Job* JobQueue::dequeue()
{
   Job* job = FirstJob;
   if(FirstJob == LastJob) {
      FirstJob = NULL;
      LastJob  = NULL;
   }
   else {
      FirstJob = FirstJob -> Next;
   }
   if(job != NULL) {
     job->StartupTimeStamp = getMicroTime();
   }
   return(job);
}



unsigned long long KeepAliveTransmissionInterval = 5000000;
unsigned long long KeepAliveTimeoutInterval      = 5000000;
unsigned long long JobInterval                   = 15000000;
FILE*              VectorFH                      = NULL;
FILE*              ScalarFH                      = NULL;
unsigned int       VectorLine                    = 0;
double             JobSize                       = 5000000;
unsigned long long runtime                       = 24*60*60*1000000ULL;
double             TotalHandlingTime             = 0.0;
double             AverageHandlingTime           = 0.0;
double             TotalHandlingSpeed            = 0.0;
double             AverageHandlingSpeed          = 0.0;
double             AverageJobSize                = 0.0;
unsigned long long TotalJobInterval              = 0;
unsigned long long AverageJobInterval            = 0;
unsigned int       TotalJobsQueued               = 0;
unsigned int       TotalJobsStarted              = 0;
double             TotalJobSizeQueued            = 0.0;
double             TotalJobSizeStarted           = 0.0;
double             TotalJobSizeCompleted         = 0.0;
Statistics         StartupTimeStat;
Statistics         QueuingTimeStat;
Statistics         ProcessingTimeStat;
Statistics         HandlingTimeStat;
Statistics         HandlingSpeedStat;
Statistics         JobSizeStat;
Statistics         JobIntervalStat;

enum ProcessStatus {
   PS_Init       = 0,
   PS_Processing = 1,
   PS_Failover   = 2,
   PS_Failed     = 3,
   PS_Completed  = 4
};

struct Process {
   int                RSerPoolSocketDescriptor;
   ProcessStatus      Status;
   JobQueue           Queue;
   Job*               CurrentJob;
   const char*        ObjectName;

   size_t             TotalCalcAppRequests;
   size_t	      TotalCalcAppAccepted;
   size_t             TotalCalcAppRejected;
   size_t             TotalCalcAppKeepAlives;
   size_t             TotalCalcAppKeepAliveAcks;
   size_t             TotalCalcAppCompleted;

   // ------ Timers ---------------------------------------------------------
   unsigned long long NextJobTimeStamp;
   unsigned long long KeepAliveTransmissionTimeStamp;
   unsigned long long KeepAliveTimeoutTimeStamp;
};


// ###### Send CalcAppRequest message #######################################
void sendCalcAppRequest(struct Process* process)
{
   cout << "New Job "<< endl;
   CalcAppMessage message;
   memset(&message, 0, sizeof(message));
   message.Type    = CALCAPP_REQUEST;
   message.Flags   = 0x00;
   message.Length  = htons(sizeof(message));
   message.JobID   = htonl(process->CurrentJob->JobID);
   message.JobSize = hton64((unsigned long long)rint(process->CurrentJob->JobSize));
   cout << "JobSize= "<< process->CurrentJob->JobSize << endl;

   // Set timeout for CalcAppAccept or CalcAppReject
   process->KeepAliveTimeoutTimeStamp = getMicroTime() + KeepAliveTimeoutInterval;

   ssize_t result = rsp_sendmsg(process->RSerPoolSocketDescriptor,
                                (void*)&message, sizeof(message), 0,
                                0, htonl(PPID_CALCAPP), 0, 0,
                                0);
   if(result <= 0) {
      process->Status = PS_Failed;
   }

   process->TotalCalcAppRequests++;
}


// ###### Send CalcAppKeepAlive message #####################################
void sendCalcAppKeepAlive(struct Process* process)
{
   struct CalcAppMessage message;
   memset(&message, 0, sizeof(message));
   message.Type   = CALCAPP_KEEPALIVE;
   message.Flags  = 0x00;
   message.Length = htons(sizeof(message));
   message.JobID  = htonl(process->CurrentJob->JobID);

   ssize_t result = rsp_sendmsg(process->RSerPoolSocketDescriptor,
                                (void*)&message, sizeof(message), 0,
                                0, htonl(PPID_CALCAPP), 0, 0,
                                0);
   if(result <= 0) {
      cerr << "WARNING: Unable to send CalcAppKeepAlive" << endl;
      process->Status = PS_Failover;
   }
   process->TotalCalcAppKeepAlives++;
}


// ###### Send CalcAppKeepAliveAck message ##################################
void sendCalcAppKeepAliveAck(struct Process* process)
{
   struct CalcAppMessage message;
   memset(&message, 0, sizeof(message));
   message.Type   = CALCAPP_KEEPALIVE_ACK;
   message.Flags  = 0x00;
   message.Length = htons(sizeof(message));
   message.JobID  = htonl(process->CurrentJob->JobID);

   ssize_t result = rsp_sendmsg(process->RSerPoolSocketDescriptor,
                                (void*)&message, sizeof(message), 0,
                                0, htonl(PPID_CALCAPP), 0, 0,
                                0);
   if(result <= 0) {
      cerr << "WARNING: Unable to send CalcAppKeepAliveAck" << endl;
      process->Status = PS_Failover;
   }
   process->TotalCalcAppKeepAliveAcks++;
}


// ###### Handle incoming CalcAppAccept message #############################
void handleCalcAppAccept(struct Process* process,
                         CalcAppMessage* message,
                         const size_t    size)
{
   if(process->CurrentJob->JobID != ntohl(message->JobID)) {
      cerr << "ERROR: CalcAppAccept for wrong job!" << endl;
      process->Status = PS_Failed;
      return;
   }

   cout << "Job " << process->CurrentJob->JobID << " accepted" << endl;
   process->CurrentJob->AcceptTimeStamp = getMicroTime();

   process->Status                         = PS_Processing;
   process->KeepAliveTimeoutTimeStamp      = ~0ULL;
   process->KeepAliveTransmissionTimeStamp = getMicroTime() + KeepAliveTransmissionInterval;

   process->TotalCalcAppAccepted++;
}


// ###### Handle incoming CalcAppReject message #############################
void handleCalcAppReject(struct Process* process,
                         CalcAppMessage* message,
                         const size_t    size)
{
   if(process->CurrentJob->JobID != ntohl(message->JobID)) {
      cerr << "ERROR: CalcAppReject for wrong job!" << endl;
      process->Status = PS_Failed;
      return;
   }

   cout << "Job " << process->CurrentJob->JobID << " rejected" << endl;
   process->Status                         = PS_Failover;
   process->KeepAliveTimeoutTimeStamp      = ~0ULL;
   process->KeepAliveTransmissionTimeStamp = ~0ULL;
   rsp_forcefailover(process->RSerPoolSocketDescriptor);

   /* No cookie for failover is available, therefore it is necessary
      to restart! */
   if(!rsp_has_cookie(process->RSerPoolSocketDescriptor)) {
      process->Status = PS_Init;
      usleep(1000000);
      sendCalcAppRequest(process);
   }

   process->TotalCalcAppRejected++;
}


// ###### Handle incoming CalcAppKeepAlive message ##########################
void handleCalcAppKeepAlive(struct Process* process,
                            CalcAppMessage* message,
                            const size_t    size)
{
   if(process->CurrentJob->JobID != ntohl(message->JobID)) {
      cerr << "ERROR: CalcAppKeepAlive for wrong job!" << endl;
      process->Status = PS_Failed;
      return;
   }
   sendCalcAppKeepAliveAck(process);
}


// ###### Handle incoming CalcAppKeepAliveAck message #######################
void handleCalcAppKeepAliveAck(struct Process* process,
                               CalcAppMessage* message,
                               const size_t    size)
{
   if(process->CurrentJob->JobID != ntohl(message->JobID)) {
      cerr << "ERROR: CalcAppKeepAliveAck for wrong job!" << endl;
      process->Status = PS_Failed;
      return;
   }

   process->Status                         = PS_Processing;
   process->KeepAliveTimeoutTimeStamp      = ~0ULL;
   process->KeepAliveTransmissionTimeStamp = getMicroTime() + KeepAliveTransmissionInterval;
}


// ###### Handle incoming CalcAppCompleted message ##########################
void handleCalcAppCompleted(struct Process* process,
                            CalcAppMessage* message,
                            const size_t    size)
{
   if(process->CurrentJob->JobID != ntohl(message->JobID)) {
      cerr << "ERROR: CalcAppCompleted for wrong job!" << endl;
      process->Status = PS_Failed;
      return;
   }

   cout << "Job " << process->CurrentJob->JobID << " completed" << endl;
   process->Status                         = PS_Completed;
   process->KeepAliveTimeoutTimeStamp      = ~0ULL;
   process->KeepAliveTransmissionTimeStamp = ~0ULL;
   process->CurrentJob->CompleteTimeStamp  = getMicroTime();

   process->TotalCalcAppCompleted++;

   const double queuingDelay    = (process->CurrentJob->StartupTimeStamp - process->CurrentJob->QueuingTimeStamp) / 1000000.0;
   const double startupDelay    = (process->CurrentJob->AcceptTimeStamp - process->CurrentJob->StartupTimeStamp) / 1000000.0;
   const double processingTime  = (process->CurrentJob->CompleteTimeStamp - process->CurrentJob->AcceptTimeStamp) / 1000000.0;
   const double processingSpeed = process->CurrentJob->JobSize / processingTime;
   const double handlingTime    = (queuingDelay + startupDelay + processingTime);
   const double handlingSpeed   = process->CurrentJob->JobSize / handlingTime;

   TotalHandlingTime     += handlingTime;
   TotalJobSizeCompleted += process->CurrentJob->JobSize;
   TotalJobInterval      += JobInterval;
   TotalHandlingSpeed    += handlingSpeed;

   StartupTimeStat.collect(startupDelay);
   QueuingTimeStat.collect(queuingDelay);
   ProcessingTimeStat.collect(processingTime);
   HandlingTimeStat.collect(handlingTime);
   HandlingSpeedStat.collect(handlingSpeed);
   JobSizeStat.collect(process->CurrentJob->JobSize);
   JobIntervalStat.collect(JobInterval / 1000000.0);

   cout << "Job #" << process->CurrentJob->JobID << ":" << endl
        << "   JobSize        = " << process->CurrentJob->JobSize << endl
        << "   JobInterval    = " << (JobInterval / 1000000.0) << " [s]" << endl
        << "   StartupDelay   = " << startupDelay   << " [s]" << endl
        << "   QueuingDelay   = " << queuingDelay   << " [s]" << endl
        << "   ProcessingTime = " << processingTime << " [s]"
        << " => ProcessingSpeed = " << processingSpeed << " [Calculations/s]" << endl
        << "   HandlingTime   = " << handlingTime << " [s]"
        << " => HandlingSpeed   = " << handlingSpeed << " [Calculations/s]" << endl;

   if(VectorLine == 0) {
      fprintf(VectorFH, "ObjectName "
                        "JobID JobSize JobInterval "
                        "QueuingDelay StartupDelay ProcessingTime "
                        "HandlingTime HandlingSpeed "
                        "QueuingTimeStamp StartupTimeStamp AcceptTimeStamp CompleteTimeStamp\n");
   }
   fprintf(VectorFH," %u %s %u %1.0f %1.6f %1.6f %1.6f %1.6f %1.6f %1.6f %1.6f %1.6f %1.6f %1.6f\n",
           ++VectorLine,
           process->ObjectName,
           process->CurrentJob->JobID, process->CurrentJob->JobSize,
           JobInterval / 1000000.0,
           queuingDelay, startupDelay, processingTime,
           handlingTime, handlingSpeed,
           process->CurrentJob->QueuingTimeStamp / 1000000.0,
           process->CurrentJob->StartupTimeStamp / 1000000.0,
           process->CurrentJob->AcceptTimeStamp / 1000000.0,
           process->CurrentJob->CompleteTimeStamp / 1000000.0);
   fflush(VectorFH);
}


// ###### Get time stamp for next job #######################################
unsigned long long getNextJobTime()
{
   return((unsigned long long)randomExpDouble(JobInterval));
}


// ###### Handle next job timer #############################################
void handleNextJobTimer(struct Process* process)
{
   static unsigned int jobID = 0;

   Job* job = new Job;
   CHECK(job != NULL);
   job->JobID   = ++jobID;
   job->JobSize = randomExpDouble(JobSize);

   process->Queue.enqueue(job);
   TotalJobsQueued++;
   TotalJobSizeQueued += job->JobSize;
   process->NextJobTimeStamp = getMicroTime() + getNextJobTime();
}


// ###### Handle KeepAlive Transmission timer ###############################
void handleKeepAliveTransmissionTimer(struct Process* process)
{
   process->KeepAliveTransmissionTimeStamp = ~0ULL;
   process->KeepAliveTimeoutTimeStamp      = getMicroTime() + KeepAliveTimeoutInterval;
   sendCalcAppKeepAlive(process);
}


// ###### Handle KeepAlive Timeout timer ####################################
bool handleKeepAliveTimeoutTimer(struct Process* process)
{
   cout << "Timeout -> Failover" << endl;

   rsp_forcefailover(process->RSerPoolSocketDescriptor);

   process->Status                         = PS_Processing;
   process->KeepAliveTimeoutTimeStamp      = ~0ULL;
   process->KeepAliveTransmissionTimeStamp = getMicroTime() + KeepAliveTransmissionInterval;

   /* No cookie for failover is available, therefore it is necessary
      to restart! */
   if(!rsp_has_cookie(process->RSerPoolSocketDescriptor)) {
      process->Status = PS_Init;
      sendCalcAppRequest(process);
   }

   return(true);
}


// ###### Handle failover ###################################################
void handleFailover(Process* process)
{
   puts("===== FAILOVER ======");
   process->Status = PS_Processing;
}


// ###### Handle session events #############################################
void handleEvents(Process*    process,
                  const short sessionEvents)
{
   struct rsp_sndrcvinfo        rinfo;
   union rserpool_notification* notification;
   char                         buffer[4096];
   ssize_t                      received;
   int                          flags;

   flags = 0;
   received = rsp_recvmsg(process->RSerPoolSocketDescriptor,
                          (char*)&buffer, sizeof(buffer),
                          &rinfo, &flags,
                          0);
   if(received > 0) {
      if(flags & MSG_RSERPOOL_NOTIFICATION) {
         notification = (union rserpool_notification*)&buffer;
         printf("Notification: ");
         rsp_print_notification(notification, stdout);
         puts("");
         if((notification->rn_header.rn_type == RSERPOOL_FAILOVER) &&
            (notification->rn_failover.rf_state == RSERPOOL_FAILOVER_NECESSARY)) {
            puts("--- FAILOVER ---");
            rsp_forcefailover(process->RSerPoolSocketDescriptor);
            /* No cookie for failover is available, therefore it is necessary
               to restart! */
            if(!rsp_has_cookie(process->RSerPoolSocketDescriptor)) {
               process->Status = PS_Init;
               sendCalcAppRequest(process);
            }
         }
      }
      else if(flags & MSG_RSERPOOL_COOKIE_ECHO) {
      }
      else {
         if(received >= (ssize_t)sizeof(CalcAppMessage)) {
            CalcAppMessage* response = (CalcAppMessage*)&buffer;
            switch(response->Type) {
               case CALCAPP_ACCEPT:
                  handleCalcAppAccept(process, response, sizeof(response));
                break;
               case CALCAPP_REJECT:
                  handleCalcAppReject(process, response, sizeof(response));
                break;
               case CALCAPP_KEEPALIVE:
                  handleCalcAppKeepAlive(process, response, sizeof(response));
                break;
               case CALCAPP_KEEPALIVE_ACK:
                  handleCalcAppKeepAliveAck(process, response, sizeof(response));
                break;
               case CALCAPP_COMPLETE:
                  handleCalcAppCompleted(process, response, sizeof(response));
                break;
               default:
                  cerr << "ERROR: Unknown message type " << response->Type << endl;
                break;
            }
         }
         else {
            cerr << "ERROR: Received too short CalcApp message (" << received << " bytes)" << endl;
         }
      }
   }
}


// ###### Handle timers #####################################################
void handleTimer(Process* process)
{
   const unsigned long long now = getMicroTime();
   if(process->KeepAliveTransmissionTimeStamp <= now) {
      handleKeepAliveTransmissionTimer(process);
      process->KeepAliveTransmissionTimeStamp = ~0ULL;
   }
   if(process->KeepAliveTimeoutTimeStamp <= now) {
      handleKeepAliveTimeoutTimer(process);
      process->KeepAliveTimeoutTimeStamp = ~0ULL;
   }
   if(process->NextJobTimeStamp <= now) {
      handleNextJobTimer(process);
   }
   if(process->NextJobTimeStamp <= now) {
      handleNextJobTimer(process);
   }
}


// ###### Run process #######################################################
void runProcess(const char* poolHandle, const char* objectName, unsigned long long startupDelayStamp)
{
   Process process;
   process.ObjectName                     = objectName;
   process.RSerPoolSocketDescriptor       = -1;
   process.NextJobTimeStamp               = getMicroTime() + (unsigned long long)randomExpDouble(JobInterval);
   process.KeepAliveTransmissionTimeStamp = ~0ULL;
   process.KeepAliveTimeoutTimeStamp      = ~0ULL;
   process.TotalCalcAppRequests           = 0;
   process.TotalCalcAppRejected           = 0;
   process.TotalCalcAppAccepted           = 0;
   process.TotalCalcAppCompleted          = 0;

   // Wait for first job
   usleep(getNextJobTime());
   handleNextJobTimer(&process);

   process.CurrentJob = process.Queue.dequeue();
   while(process.CurrentJob != NULL) {
      TotalJobsStarted++;
      TotalJobSizeStarted += process.CurrentJob->JobSize;
      process.Status = PS_Init;

      process.RSerPoolSocketDescriptor = rsp_socket(0, SOCK_SEQPACKET, IPPROTO_SCTP);
      if(process.RSerPoolSocketDescriptor >= 0) {
         if(rsp_connect(process.RSerPoolSocketDescriptor,
                        (unsigned char*)poolHandle, strlen(poolHandle)) == 0) {
            sendCalcAppRequest(&process);

            while((process.Status != PS_Failed) &&
                  (process.Status != PS_Completed)) {

               /* ====== Call rsp_poll() ================================= */
               struct pollfd ufds;
               ufds.fd     = process.RSerPoolSocketDescriptor;
               ufds.events = POLLIN;

               unsigned long long now    = getMicroTime();
               unsigned long long nextTimer = now + 500000;
               nextTimer = min(nextTimer, process.NextJobTimeStamp);
               nextTimer = min(nextTimer, process.KeepAliveTransmissionTimeStamp);
               nextTimer = min(nextTimer, process.KeepAliveTimeoutTimeStamp);
               unsigned long long timeout = (nextTimer >= now) ? (nextTimer - now) : 1;

               int result = rsp_poll(&ufds, 1, (int)(timeout / 1000ULL));

               /* ====== Handle timers =================================== */
               handleTimer(&process);
               if(getMicroTime() - startupDelayStamp >= runtime) {
                  goto finished;
               }

               /* ====== Handle results of ext_select() ================== */
               if((result > 0) && (ufds.revents & POLLIN)) {
                  handleEvents(&process, ufds.revents);
               }
               if(result < 0) {
                  if(errno != EINTR) {
                     perror("rsp_poll() failed");
                  }
                  goto finished;
               }
               if(breakDetected()) {
                  rsp_close(process.RSerPoolSocketDescriptor);
                  process.RSerPoolSocketDescriptor = -1;
                  goto finished;
               }

               if(process.Status == PS_Failover) {
                  handleFailover(&process);
               }
            }

            if(process.Status == PS_Completed) {
               delete process.CurrentJob;
               process.CurrentJob = process.Queue.dequeue();
            }
         }
         else {
            perror("rsp_connect() failed");
         }

         rsp_close(process.RSerPoolSocketDescriptor);
         process.RSerPoolSocketDescriptor = -1;
      }
      else {
         perror("rsp_socket() failed");
         return;
      }

      while(process.CurrentJob == NULL) {
         unsigned long long now       = getMicroTime();
         unsigned long long nextTimer = now + 500000;
         nextTimer = min(nextTimer, process.NextJobTimeStamp);
         unsigned long long timeout = (nextTimer >= now) ? (nextTimer - now) : 1;
         usleep((unsigned int)(timeout / 1000ULL));
         handleTimer(&process);
         if(breakDetected()) {
            goto finished;
         }
         process.CurrentJob = process.Queue.dequeue();
      }

   }

finished:
   fprintf(ScalarFH, "scalar \"%s\" \"StartupTime     \"mean=%f \n", objectName, StartupTimeStat.mean());
   fprintf(ScalarFH, "scalar \"%s\" \"QueueingTime    \"mean=%f \n", objectName, QueuingTimeStat.mean());
   fprintf(ScalarFH, "scalar \"%s\" \"ProcessingTime  \"mean=%f \n", objectName, ProcessingTimeStat.mean());
   fprintf(ScalarFH, "scalar \"%s\" \"HandlingTime    \"mean=%f \n", objectName, HandlingTimeStat.mean());
   fprintf(ScalarFH, "scalar \"%s\" \"HandlingSpeed   \"mean=%f \n", objectName, HandlingSpeedStat.mean());
   fprintf(ScalarFH, "scalar \"%s\" \"JobSize         \"mean=%f \n", objectName, JobSizeStat.mean());
   fprintf(ScalarFH, "scalar \"%s\" \"JobInterval     \"mean=%f \n", objectName, JobIntervalStat.mean());

   fprintf(ScalarFH, "scalar \"%s\" \"Total CalcAppRequests     \"%u \n", objectName, process.TotalCalcAppRequests);
   fprintf(ScalarFH, "scalar \"%s\" \"Total CalcAppAccepts      \"%u \n", objectName, process.TotalCalcAppAccepted);
   fprintf(ScalarFH, "scalar \"%s\" \"Total CalcAppRejects      \"%u \n", objectName, process.TotalCalcAppRejected);

   fprintf(ScalarFH, "scalar \"%s\" \"Total Jobs Queued\" %u \n", objectName, TotalJobsQueued);
   fprintf(ScalarFH, "scalar \"%s\" \"Total Jobs Started\" %u \n", objectName, TotalJobsStarted);
   fprintf(ScalarFH, "scalar \"%s\" \"Total Jobs Completed \" %u \n", objectName, process.TotalCalcAppCompleted);
   fprintf(ScalarFH, "scalar \"%s\" \"Total Job Size Queued\" %1.6f \n", objectName, TotalJobSizeQueued);
   fprintf(ScalarFH, "scalar \"%s\" \"Total Job Size Started\" %1.6f \n", objectName, TotalJobSizeStarted);
   fprintf(ScalarFH, "scalar \"%s\" \"Total Job Size Completed\" %1.6f \n", objectName, TotalJobSizeCompleted);

   if(process.CurrentJob) {
      delete process.CurrentJob;
      process.CurrentJob = NULL;
   }
}


// ###### Main program ######################################################
int main(int argc, char** argv)
{
   unsigned long long startupDelayStamp = getMicroTime();
   struct             rsp_info info;
   char*              poolHandle     = "CalcAppPool";
   char*              objectName     = "scenario.calcapppooluser[0]";
   char*              vectorFileName = "calcapppooluser.vec";
   char*              scalarFileName = "calcapppooluser.sca";
   int                i;

   memset(&info, 0, sizeof(info));

#ifdef ENABLE_CSP
   info.ri_csp_identifier = CID_COMPOUND(CID_GROUP_POOLUSER, 0);
#endif
   for(i = 1;i < argc;i++) {
      if(!(strncmp(argv[i],"-ph=",4))) {
         poolHandle = (char*)&argv[i][4];
      }
      else if(!(strncmp(argv[i], "-object=" ,8))) {
         objectName = (char*)&argv[i][8];
      }
      else if(!(strncmp(argv[i], "-vector=" ,8))) {
         vectorFileName = (char*)&argv[i][8];
      }
      else if(!(strncmp(argv[i], "-scalar=" ,8))) {
         scalarFileName = (char*)&argv[i][8];
      }
      else if(!(strncmp(argv[i], "-jobinterval=" ,13))) {
         JobInterval = atol((char*)&argv[i][13]);
      }
      else if(!(strncmp(argv[i], "-jobsize=" ,9))) {
         JobSize = atol((char*)&argv[i][9]);
      }
      else if(!(strncmp(argv[i], "-runtime=" ,9))) {
         runtime 	= atol((char*)&argv[i][9]);
      }
      else if(!(strncmp(argv[i],"-log",4))) {
         if(initLogging(argv[i]) == false) {
            exit(1);
         }
      }
#ifdef ENABLE_CSP
      else if(!(strncmp(argv[i], "-csp" ,4))) {
puts("CSP!");
         if(initComponentStatusReporter(&info, argv[i]) == false) {
            exit(1);
         }
      }
#endif
      else if(!(strncmp(argv[i], "-registrar=", 11))) {
         if(addStaticRegistrar(&info, (char*)&argv[i][11]) < 0) {
            fprintf(stderr, "ERROR: Bad registrar setting %s\n", argv[i]);
            exit(1);
         }
      }
      else {
         printf("ERROR: Bad arguments %s!\n", argv[i]);
         exit(1);
      }
   }
#ifdef ENABLE_CSP
   if(CID_OBJECT(info.ri_csp_identifier) == 0ULL) {
      info.ri_csp_identifier = CID_COMPOUND(CID_GROUP_POOLUSER, random64());
   }
#endif


   cout << "CalcApp Client - Version 1.0" << endl
        << "============================" << endl << endl
        << "Object       = " << objectName << endl
        << "Vector File  = " << vectorFileName << endl
        << "Scalar File  = " << scalarFileName << endl
        << "Job Interval = " << (JobInterval / 1000000.0) << " [s]" << endl
        << "Job Size     = " << JobSize << " [Calculations]" << endl
        << endl;


   beginLogging();
   if(rsp_initialize(&info) < 0) {
      logerror("Unable to initialize rsplib");
      exit(1);
   }

   VectorFH = fopen(vectorFileName, "w");
   if(VectorFH == NULL) {
      cout << " Unable to open output file " << vectorFileName << endl;
      finishLogging();
   }

   ScalarFH = fopen(scalarFileName, "w");
   if(ScalarFH == NULL) {
      cout << " Unable to open output file " << scalarFileName << endl;
      finishLogging();
   }
   fprintf(ScalarFH, "run 1 \"scenario\"\n");

#ifndef FAST_BREAK
   installBreakDetector();
#endif


   runProcess(poolHandle, objectName, startupDelayStamp);


   fclose(ScalarFH);
   fclose(VectorFH);

   finishLogging();
   rsp_cleanup();
   puts("\nTerminated!");
   return(0);
}