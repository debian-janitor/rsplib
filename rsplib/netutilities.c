/*
 *  $Id$
 *
 * RSerPool implementation.
 *
 * Realized in co-operation between Siemens AG
 * and University of Essen, Institute of Computer Networking Technology.
 *
 * Acknowledgement
 * This work was partially funded by the Bundesministerium fr Bildung und
 * Forschung (BMBF) of the Federal Republic of Germany (F�derkennzeichen 01AK045).
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
 * Purpose: Network Utilities
 *
 */

#include "tdtypes.h"
#include "loglevel.h"
#include "netutilities.h"
#include "stringutilities.h"
#include "randomizer.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ext_socket.h>
#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#ifdef SOLARIS
#include <sys/sockio.h>
#endif


#ifdef SOLARIS
#define CMSG_SPACE(len) (_CMSG_HDR_ALIGN(sizeof(struct cmsghdr)) + _CMSG_DATA_ALIGN(len))
#define CMSG_LEN(len) (_CMSG_HDR_ALIGN(sizeof(struct cmsghdr)) + (len))
#endif

#ifdef HAVE_KERNEL_SCTP
#ifndef HAVE_SCTP_CONNECTX
#warning No sctp_connectx() available - Using only the first address!
int sctp_connectx(int                    sockfd,
                  const struct sockaddr* addrs,
                  int                    addrcnt)
{
   const struct sockaddr* bestScopedAddress = getBestScopedAddress(addrs, addrcnt);
   return(ext_connect(sockfd, bestScopedAddress, getSocklen(bestScopedAddress)));
}
#endif

#ifndef HAVE_SCTP_SEND
#warning No sctp_send() available - Using wrapper!
ssize_t sctp_send(int                           sd,
                  const void*                   data,
                  size_t                        len,
                  const struct sctp_sndrcvinfo* sinfo,
                  int                           flags)
{
   struct sctp_sndrcvinfo* sri;
   struct iovec            iov = { (char*)data, len };
   struct cmsghdr*         cmsg;
   size_t                  cmsglen = CMSG_SPACE(sizeof(struct sctp_sndrcvinfo));
   char                    cbuf[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
   struct msghdr msg = {
      NULL, 0,
      &iov, 1,
      cbuf, cmsglen,
      flags
   };

   cmsg = (struct cmsghdr*)CMSG_FIRSTHDR(&msg);
   cmsg->cmsg_len   = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
   cmsg->cmsg_level = IPPROTO_SCTP;
   cmsg->cmsg_type  = SCTP_SNDRCV;

   sri = (struct sctp_sndrcvinfo*)CMSG_DATA(cmsg);
   memcpy(sri, sinfo, sizeof(struct sctp_sndrcvinfo));
   return(ext_sendmsg(sd, &msg, msg.msg_flags));
}
#endif

#ifndef HAVE_SCTP_SENDX
#warning No sctp_send() available - Using only the first address!
ssize_t sctp_sendx(int                           sd,
                   const void*                   data,
                   size_t                        len,
                   const struct sockaddr*        addrs,
                   int                           addrcnt,
                   const struct sctp_sndrcvinfo* sinfo,
                   int                           flags)
{
   struct sctp_sndrcvinfo* sri;
   struct iovec            iov = { (char*)data, len };
   struct cmsghdr*         cmsg;
   size_t                  cmsglen = CMSG_SPACE(sizeof(struct sctp_sndrcvinfo));
   char                    cbuf[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
   struct msghdr msg = {
      NULL, 0,
      &iov, 1,
      cbuf, cmsglen,
      flags
   };

   const struct sockaddr* bestScopedAddress = getBestScopedAddress(addrs, addrcnt);
   msg.msg_name = (struct sockaddr*)bestScopedAddress;
   msg.msg_namelen = getSocklen(bestScopedAddress);

   cmsg = (struct cmsghdr*)CMSG_FIRSTHDR(&msg);
   cmsg->cmsg_len   = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
   cmsg->cmsg_level = IPPROTO_SCTP;
   cmsg->cmsg_type  = SCTP_SNDRCV;

   sri = (struct sctp_sndrcvinfo*)CMSG_DATA(cmsg);
   memcpy(sri, sinfo, sizeof(struct sctp_sndrcvinfo));

   return(ext_sendmsg(sd, &msg, msg.msg_flags));
}
#endif
#endif

#ifdef LINUX
#ifdef HAVE_KERNEL_SCTP

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#define LINUX_PROC_IPV6_FILE "/proc/net/if_inet6"

enum AddressScopingFlags {
   ASF_HideLoopback           = (1 << 0),
   ASF_HideLinkLocal          = (1 << 1),
   ASF_HideSiteLocal          = (1 << 2),
   ASF_HideLocal              = ASF_HideLoopback|ASF_HideLinkLocal|ASF_HideSiteLocal,
   ASF_HideAnycast            = (1 << 3),
   ASF_HideMulticast          = (1 << 4),
   ASF_HideBroadcast          = (1 << 5),
   ASF_HideReserved           = (1 << 6),
   ASF_Default                = ASF_HideBroadcast|ASF_HideMulticast|ASF_HideAnycast|ASF_HideReserved,
   ASF_HideAllExceptLoopback  = (1 << 7),
   ASF_HideAllExceptLinkLocal = (1 << 8),
   ASF_HideAllExceptSiteLocal = (1 << 9)
};


/* ###### Filter address ################################################# */
static bool filterAddress(union sockaddr_union* address,
                          unsigned int          flags)
{
   switch (address->sa.sa_family) {
      case AF_INET :
         if((IN_MULTICAST(ntohl(address->in.sin_addr.s_addr))         && (flags & ASF_HideMulticast)) ||
            (IN_EXPERIMENTAL(ntohl(address->in.sin_addr.s_addr))      && (flags & ASF_HideReserved))  ||
            (IN_BADCLASS(ntohl(address->in.sin_addr.s_addr))          && (flags & ASF_HideReserved))  ||
            ((INADDR_BROADCAST == ntohl(address->in.sin_addr.s_addr)) && (flags & ASF_HideBroadcast)) ||
            ((INADDR_LOOPBACK == ntohl(address->in.sin_addr.s_addr))  && (flags & ASF_HideLoopback))  ||
            ((INADDR_LOOPBACK != ntohl(address->in.sin_addr.s_addr))  && (flags & ASF_HideAllExceptLoopback))) {
            return(false);
         }
       break;
      case AF_INET6 :
        if((!IN6_IS_ADDR_LOOPBACK(&(address->in6.sin6_addr))  && (flags & ASF_HideAllExceptLoopback))  ||
           (IN6_IS_ADDR_LOOPBACK(&(address->in6.sin6_addr))   && (flags & ASF_HideLoopback))           ||
           (!IN6_IS_ADDR_LINKLOCAL(&(address->in6.sin6_addr)) && (flags & ASF_HideAllExceptLinkLocal)) ||
           (!IN6_IS_ADDR_SITELOCAL(&(address->in6.sin6_addr)) && (flags & ASF_HideAllExceptSiteLocal)) ||
           (IN6_IS_ADDR_LINKLOCAL(&(address->in6.sin6_addr))  && (flags & ASF_HideLinkLocal))          ||
           (IN6_IS_ADDR_SITELOCAL(&(address->in6.sin6_addr))  && (flags & ASF_HideSiteLocal))          ||
           (IN6_IS_ADDR_MULTICAST(&(address->in6.sin6_addr))  && (flags & ASF_HideMulticast))          ||
           (IN6_IS_ADDR_UNSPECIFIED(&(address->in6.sin6_addr)))) {
            return(false);
        }
       break;
      default:
         return(false);
       break;
   }
   return(true);
}


/* ###### Gather local addresses ######################################### */
static bool obtainLocalAddresses(union sockaddr_union** addressArray,
                                 size_t*                addresses,
                                 const unsigned int     flags)
{
   union sockaddr_union* localAddresses = NULL;
   struct sockaddr*         toUse;
   struct ifreq             local;
   struct ifreq*            ifrequest;
   struct ifreq*            nextif;
   struct ifconf            cf;
   char                     buffer[8192];
   int                      addedNets;
   char                     addrBuffer[256];
   char                     addrBuffer2[64];
   FILE*                    v6list;
   size_t pos           = 0;
   size_t copySize      = 0;
   size_t numAllocAddrs = 0;
   size_t numIfAddrs    = 0;
   size_t i;
   size_t j;
   size_t dup;
   int    fd;


   *addresses = 0;

   fd = socket(AF_INET,SOCK_DGRAM,0);
   if(fd < 0) {
      return(false);
   }

   /* Now gather the master address information */
   cf.ifc_buf = buffer;
   cf.ifc_len = sizeof(buffer);
   if(ioctl(fd, SIOCGIFCONF, (char *)&cf) == -1) {
       return(false);
   }

   numAllocAddrs = cf.ifc_len / sizeof(struct ifreq);
   ifrequest     = cf.ifc_req;
   numIfAddrs    = numAllocAddrs;

   addedNets = 0;
   v6list = fopen(LINUX_PROC_IPV6_FILE,"r");
   if (v6list != NULL) {
       while(fgets(addrBuffer,sizeof(addrBuffer),v6list) != NULL) {
           addedNets++;
       }
       fclose(v6list);
   }
   numAllocAddrs += addedNets;

   /* now allocate the appropriate memory */
   localAddresses = (union sockaddr_union*)calloc(numAllocAddrs,sizeof(union sockaddr_union));

   if(localAddresses == NULL) {
      close(fd);
      return(false);
   }

    pos = 0;
   /* Now we go through and pull each one */

   v6list = fopen(LINUX_PROC_IPV6_FILE,"r");
   if(v6list != NULL) {
      struct sockaddr_in6 sin6;
      memset((char *)&sin6,0,sizeof(sin6));
      sin6.sin6_family = AF_INET6;

      while(fgets(addrBuffer,sizeof(addrBuffer),v6list) != NULL) {
         memset(addrBuffer2,0,sizeof(addrBuffer2));
         strncpy(addrBuffer2,addrBuffer,4);
         addrBuffer2[4] = ':';
         strncpy(&addrBuffer2[5],&addrBuffer[4],4);
         addrBuffer2[9] = ':';
         strncpy(&addrBuffer2[10],&addrBuffer[8],4);
         addrBuffer2[14] = ':';
         strncpy(&addrBuffer2[15],&addrBuffer[12],4);
         addrBuffer2[19] = ':';
         strncpy(&addrBuffer2[20],&addrBuffer[16],4);
         addrBuffer2[24] = ':';
         strncpy(&addrBuffer2[25],&addrBuffer[20],4);
         addrBuffer2[29] = ':';
         strncpy(&addrBuffer2[30],&addrBuffer[24],4);
         addrBuffer2[34] = ':';
         strncpy(&addrBuffer2[35],&addrBuffer[28],4);

         if(inet_pton(AF_INET6,addrBuffer2,(void *)&sin6.sin6_addr)) {
            if(filterAddress((union sockaddr_union*)&sin6,flags) == true) {
               memcpy(&((localAddresses)[*addresses]),&sin6,sizeof(sin6));
               (*addresses)++;
            }
         }
      }
      fclose(v6list);
   }

   /* set to the start, i.e. buffer[0] */
   ifrequest = (struct ifreq *)&buffer[pos];

   for(i = 0;i < numIfAddrs;i++,ifrequest = nextif) {
      nextif = ifrequest + 1;
      toUse = &ifrequest->ifr_addr;

      memset(&local, 0, sizeof(local));
      memcpy(local.ifr_name, ifrequest->ifr_name, IFNAMSIZ);
      if(filterAddress((union sockaddr_union*)toUse, flags) == false) {
         continue;
      }

      if(ioctl(fd,SIOCGIFFLAGS,(char*)&local) < 0) {
         continue;
      }
      if(!(local.ifr_flags & IFF_UP)) {
         /* Device is down. */
         continue;
      }

      if(toUse->sa_family == AF_INET) {
         copySize = sizeof(struct sockaddr_in);
      }
      else if (toUse->sa_family == AF_INET6) {
         copySize = sizeof(struct sockaddr_in6);
      }
      else {
         continue;
      }


      /* Check for duplicates */
      if(*addresses) {
         dup = 0;
         /* scan for the dup */
         for(j = 0; j < *addresses; j++) {
            /* family's must match */
            if(((struct sockaddr*)&(localAddresses[j]))->sa_family != toUse->sa_family) {
                continue;
            }

            if(((struct sockaddr*)&(localAddresses[j]))->sa_family == AF_INET) {
               if(((struct sockaddr_in *)(toUse))->sin_addr.s_addr == ((struct sockaddr_in*)&(localAddresses[j]))->sin_addr.s_addr) {
                  /* set the flag and break, it is a dup */
                  dup = 1;
                  break;
               }
            } else {
                if(IN6_ARE_ADDR_EQUAL(&(((struct sockaddr_in6*)(toUse))->sin6_addr),
                                      &(((struct sockaddr_in6*)&(localAddresses[j]))->sin6_addr))) {
                    /* set the flag and break, it is a dup */
                    dup = 1;
                    break;
                }
            }
         }
         if(dup) {
            /* skip the duplicate name/address we already have it*/
            continue;
         }
      }

      /* copy address and family */
      memcpy(&localAddresses[*addresses],(char *)toUse,copySize);
      ((struct sockaddr*)&(localAddresses[*addresses]))->sa_family = toUse->sa_family;

      (*addresses)++;
   }

   *addressArray = localAddresses;
   close(fd);

   return(true);
}

#endif
#endif


#define MAX_AUTOSELECT_TRIALS 50000
#define MIN_AUTOSELECT_PORT   32768
#define MAX_AUTOSELECT_PORT   60000


#if 0
/* ###### Convert mixed IPv4/IPv6 addresses to IPv6-format ############### */
static struct sockaddr_in6* convertToIPv6(const struct sockaddr* addrs,
                                          const int              addrcnt)
{
   union sockaddr_union* addrArray = (union sockaddr_union*)addrs;
   struct sockaddr_in6*  newAddressArray;
   int                   i;

   newAddressArray = (struct sockaddr_in6*)malloc(sizeof(struct sockaddr_in6) * addrcnt);
   if(newAddressArray == NULL) {
      errno = ENOMEM;
      return(NULL);
   }

   for(i = 0;i < addrcnt;i++) {
      switch(addrArray->sa.sa_family) {
         case AF_INET:
            newAddressArray[i].sin6_family            = AF_INET6;
            newAddressArray[i].sin6_flowinfo          = 0;
            newAddressArray[i].sin6_port              = addrArray->in.sin_port;
            newAddressArray[i].sin6_addr.s6_addr32[0] = 0x0000;
            newAddressArray[i].sin6_addr.s6_addr32[1] = 0x0000;
            newAddressArray[i].sin6_addr.s6_addr32[2] = htonl(0x0000ffff);
            newAddressArray[i].sin6_addr.s6_addr32[3] = addrArray->in.sin_addr.s_addr;
#ifdef HAVE_SOCKLEN_T
            newAddressArray[i].sin6_socklen           = sizeof(struct sockaddr_in6);
#endif
            addrArray = (union sockaddr_union*)((long)addrArray + (long)sizeof(struct sockaddr_in));
          break;
         case AF_INET6:
            memcpy((void*)&newAddressArray[i], addrArray, sizeof(struct sockaddr_in6));
            addrArray = (union sockaddr_union*)((long)addrArray + (long)sizeof(struct sockaddr_in6));
          break;
         default:
            free(newAddressArray);
            errno = EPROTOTYPE;
            return(NULL);
          break;
      }
   }

   for(i = 0;i < addrcnt;i++) {
      printf("A%d=",i);
      fputaddress((struct sockaddr*)&newAddressArray[i], true, stdout);
      puts("");
   }

   return(newAddressArray);
}
#endif


/* ###### Wrapper for sctp_bindx() ######################################## */
static int my_sctp_bindx(int              sockfd,
                         struct sockaddr* addrs,
                         int              addrcnt,
                         int              flags)
{
#if 0
#warning Using Solaris-compatible sctp_bindx()!
   union sockaddr_union* addrArray = (union sockaddr_union*)addrs;
   struct sockaddr_in6*  newAddressArray;
   int                   result;

   result = sctp_bindx(sockfd, addrs, addrcnt, flags);
   return(result);
#else
   return(sctp_bindx(sockfd, addrs, addrcnt, flags));
#endif
}


/* ###### Wrapper for sctp_connectx() #################################### */
static int my_sctp_connectx(int              sockfd,
                            struct sockaddr* addrs,
                            int              addrcnt)
{
#if 0
#warning Using Solaris-compatible sctp_connectx()!
   struct sockaddr_in6*  newAddressArray;
   int                   result;

result=-1;
//   result = sctp_connectx(sockfd, addrs, addrcnt);
//   printf("1: sctp_connectx() %d errno=%d   inprogress=%d\n",result,errno,EINPROGRESS);
//   if((result < 0) && (errno == EPROTOTYPE)) {

      newAddressArray = convertToIPv6(addrs, addrcnt);
      if(newAddressArray) {
         result = sctp_connectx(sockfd, (struct sockaddr*)newAddressArray, addrcnt);
         printf("2: sctp_connectx() %d errno=%d   inprogress=%d\n",result,errno,EINPROGRESS);
         free(newAddressArray);
      }
      else {
         return(-1);
      }
//   }

   return(result);
#else
   int result = sctp_connectx(sockfd, addrs, addrcnt);
   return(result);
#endif
}


/* ###### Unpack sockaddr blocks to sockaddr_union array ################## */
union sockaddr_union* unpack_sockaddr(struct sockaddr* addrArray,
                                      const size_t     addrs)
{
   union sockaddr_union* newArray;
   size_t                i;

   newArray = (union sockaddr_union*)malloc(sizeof(union sockaddr_union) * addrs);
   if(newArray) {
      for(i = 0;i < addrs;i++) {
         switch(addrArray->sa_family) {
            case AF_INET:
               memcpy((void*)&newArray[i], addrArray, sizeof(struct sockaddr_in));
               addrArray = (struct sockaddr*)((long)addrArray + (long)sizeof(struct sockaddr_in));
             break;
            case AF_INET6:
               memcpy((void*)&newArray[i], addrArray, sizeof(struct sockaddr_in6));
               addrArray = (struct sockaddr*)((long)addrArray + (long)sizeof(struct sockaddr_in6));
             break;
            default:
               LOG_ERROR
               fprintf(stderr, "ERROR: unpack_sockaddr() - Unknown address type #%d\n",
                       addrArray->sa_family);
               LOG_END_FATAL
             break;
         }
      }
   }
   return(newArray);
}


/* ###### Pack sockaddr_union array to sockaddr blocks #################### */
struct sockaddr* pack_sockaddr_union(const union sockaddr_union* addrArray,
                                     const size_t                addrs)
{
   size_t           required = 0;
   size_t           i;
   struct sockaddr* a, *newArray;

   for(i = 0;i < addrs;i++) {
      switch(((struct sockaddr*)&addrArray[i])->sa_family) {
         case AF_INET:
            required += sizeof(struct sockaddr_in);
          break;
         case AF_INET6:
            required += sizeof(struct sockaddr_in6);
          break;
         default:
            LOG_ERROR
            fprintf(stderr, "ERROR: pack_sockaddr_union() - Unknown address type #%d\n",
                    ((struct sockaddr*)&addrArray[i])->sa_family);
            LOG_END_FATAL
          break;
      }
   }

   newArray = NULL;
   if(required > 0) {
      newArray = (struct sockaddr*)malloc(required);
      if(newArray == NULL) {
         return(NULL);
      }
      a = newArray;
      for(i = 0;i < addrs;i++) {
         switch(((struct sockaddr*)&addrArray[i])->sa_family) {
            case AF_INET:
               memcpy((void*)a, (void*)&addrArray[i], sizeof(struct sockaddr_in));
               a = (struct sockaddr*)((long)a + (long)sizeof(struct sockaddr_in));
             break;
            case AF_INET6:
               memcpy((void*)a, (void*)&addrArray[i], sizeof(struct sockaddr_in6));
               a = (struct sockaddr*)((long)a + (long)sizeof(struct sockaddr_in6));
             break;
         }
      }
   }
   return(newArray);
}


/* ###### Get local addresses from socket ################################ */
size_t getAddressesFromSocket(int sockfd, union sockaddr_union** addressArray)
{
   union sockaddr_union address;
   socklen_t            addressLength;
   ssize_t              addresses;
   ssize_t              result;
   ssize_t              i;

   LOG_VERBOSE4
   fputs("Getting transport addresses from socket...\n",stdlog);
   LOG_END

   addresses = getladdrsplus(sockfd, 0, addressArray);
   if(addresses < 1) {
      LOG_VERBOSE4
      logerror("getladdrsplus() failed, trying getsockname()");
      LOG_END

      addresses     = 0;
      *addressArray = NULL;
      addressLength = sizeof(address);
      result = ext_getsockname(sockfd,(struct sockaddr*)&address, &addressLength);
      if(result == 0) {
         LOG_VERBOSE4
         fputs("Successfully obtained address by getsockname()\n",stdlog);
         LOG_END

         *addressArray = duplicateAddressArray((const union sockaddr_union*)&address, 1);
         if(*addressArray != NULL) {
            addresses = 1;
         }
      }
      else {
         LOG_VERBOSE4
         logerror("getsockname() failed");
         LOG_END
      }
   }
   else {
      LOG_VERBOSE4
      fprintf(stdlog, "Obtained %d address(es)\n",addresses);
      LOG_END
   }

   LOG_VERBOSE4
   fprintf(stdlog, "Obtained addresses: %u\n", addresses);
   for(i = 0;i < addresses;i++) {
      fputaddress((const struct sockaddr*)&addressArray[i], true, stdlog);
      fputs("\n", stdlog);
   }
   LOG_END

   return((size_t)addresses);
}


/* ###### Gather local addresses ######################################### */
size_t gatherLocalAddresses(union sockaddr_union** addressArray)
{
   union sockaddr_union anyAddress;
   size_t               addresses = 0;
   int                  sd;

   string2address(checkIPv6() ? "[::]" : "0.0.0.0", &anyAddress);
   sd = ext_socket(checkIPv6() ? AF_INET6 : AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
   if(sd >= 0) {
      if(ext_bind(sd, (struct sockaddr*)&anyAddress, getSocklen(&anyAddress.sa)) == 0) {
         addresses = getAddressesFromSocket(sd, addressArray);
      }
      ext_close(sd);
   }
   return(addresses);
}


/* ###### Delete address array ########################################### */
void deleteAddressArray(union sockaddr_union* addressArray)
{
   if(addressArray != NULL) {
      free(addressArray);
   }
}


/* ###### Duplicate address array ######################################## */
union sockaddr_union* duplicateAddressArray(const union sockaddr_union* addressArray,
                                            const size_t                addresses)
{
   const size_t size = sizeof(union sockaddr_union) * addresses;

   union sockaddr_union* copy = (union sockaddr_union*)malloc(size);
   if(copy != NULL) {
      memcpy(copy,addressArray,size);
   }
   return(copy);
}


/* ###### Convert address to string ###################################### */
bool address2string(const struct sockaddr* address,
                    char*                  buffer,
                    const size_t           length,
                    const bool             port)
{
   struct sockaddr_in*  ipv4address;
   struct sockaddr_in6* ipv6address;
   char                 str[128];

   switch(address->sa_family) {
      case AF_INET:
         ipv4address = (struct sockaddr_in*)address;
         if(port) {
            snprintf(buffer, length,
                     "%s:%d", inet_ntoa(ipv4address->sin_addr), ntohs(ipv4address->sin_port));
         }
         else {
            snprintf(buffer, length, "%s", inet_ntoa(ipv4address->sin_addr));
         }
         return(true);
       break;
      case AF_INET6:
         ipv6address = (struct sockaddr_in6*)address;
         ipv6address->sin6_scope_id = 0;
         if(inet_ntop(AF_INET6, &ipv6address->sin6_addr, str, sizeof(str)) != NULL) {
            if(port) {
               snprintf(buffer, length,
                        "[%s]:%d", str, ntohs(ipv6address->sin6_port));
            }
            else {
               snprintf(buffer, length, "%s", str);
            }
            return(true);
         }
       break;
      case AF_UNSPEC:
         safestrcpy(buffer, "(unspecified)",length);
         return(true);
       break;
   }

   LOG_ERROR
   fprintf(stdlog, "Unsupported address family #%d\n",
           ((struct sockaddr*)address)->sa_family);
   LOG_END_FATAL
   return(false);
}


/* ###### Convert string to address ###################################### */
bool string2address(const char* string, union sockaddr_union* address)
{
   char                 host[128];
   char                 port[128];
   struct sockaddr_in*  ipv4address = (struct sockaddr_in*)address;
   struct sockaddr_in6* ipv6address = (struct sockaddr_in6*)address;
   char*                p1;
   int                  portNumber;

   struct addrinfo  hints;
   struct addrinfo* res;
   bool isNumeric;
   bool isIPv6;
   size_t hostLength;
   size_t i;

   if(strlen(string) > sizeof(host)) {
      LOG_ERROR
      fputs("String too long!\n",stdlog);
      LOG_END
      return(false);
   }
   strcpy((char*)&host,string);
   strcpy((char*)&port, "0");

   /* ====== Handle RFC2732-compliant addresses ========================== */
   if(string[0] == '[') {
      p1 = strindex(host,']');
      if(p1 != NULL) {
         if((p1[1] == ':') || (p1[1] == '!')) {
            strcpy((char*)&port, &p1[2]);
         }
         memmove((char*)&host, (char*)&host[1], (long)p1 - (long)host - 1);
         host[(long)p1 - (long)host - 1] = 0x00;
      }
   }

   /* ====== Handle standard address:port ================================ */
   else {
      p1 = strrindex(host,':');
      if(p1 == NULL) {
         p1 = strrindex(host,'!');
      }
      if(p1 != NULL) {
         p1[0] = 0x00;
         strcpy((char*)&port, &p1[1]);
      }
   }

   /* ====== Check port number =========================================== */
   if((sscanf(port, "%d", &portNumber) == 1) &&
      (portNumber < 0) &&
      (portNumber > 65535)) {
      return(false);
   }


   /* ====== Create address structure ==================================== */

   /* ====== Get information for host ==================================== */
   res        = NULL;
   isNumeric  = true;
   isIPv6     = false;
   hostLength = strlen(host);

   memset((char*)&hints,0,sizeof(hints));
   hints.ai_socktype = SOCK_DGRAM;
   hints.ai_family   = AF_UNSPEC;

   for(i = 0;i < hostLength;i++) {
      if(host[i] == ':') {
         isIPv6 = true;
         break;
      }
   }
   if(!isIPv6) {
      for(i = 0;i < hostLength;i++) {
         if(!(isdigit(host[i]) || (host[i] == '.'))) {
            isNumeric = false;
            break;
         }
       }
   }
   if(isNumeric) {
      hints.ai_flags = AI_NUMERICHOST;
   }

   if(getaddrinfo(host, NULL, &hints, &res) != 0) {
      return(false);
   }

   memset((char*)address,0,sizeof(union sockaddr_union));
   memcpy((char*)address,res->ai_addr,res->ai_addrlen);

   switch(ipv4address->sin_family) {
      case AF_INET:
         ipv4address->sin_port = htons(portNumber);
#ifdef HAVE_SIN_LEN
         ipv4address->sin_len  = sizeof(struct sockaddr_in);
#endif
       break;
      case AF_INET6:
         ipv6address->sin6_port = htons(portNumber);
#ifdef HAVE_SIN6_LEN
         ipv6address->sin6_len  = sizeof(struct sockaddr_in6);
#endif
       break;
      default:
         LOG_ERROR
         fprintf(stdlog, "Unsupported address family #%d\n",
                 ((struct sockaddr*)address)->sa_family);
         LOG_END_FATAL
       break;
   }

   freeaddrinfo(res);
   return(true);
}


/* ###### Print address ################################################## */
void fputaddress(const struct sockaddr* address, const bool port, FILE* fd)
{
   char str[128];

   address2string(address, (char*)&str, sizeof(str), port);
   fputs(str, fd);
}


/* ###### Get IPv4 address scope ######################################### */
static unsigned int scopeIPv4(const uint32_t* address)
{
    uint32_t a;
    uint8_t  b1;
    uint8_t  b2;

    /* Unspecified */
    if(*address == 0x00000000) {
       return(0);
    }

    /* Loopback */
    a = ntohl(*address);
    if((a & 0x7f000000) == 0x7f000000) {
       return(2);
    }

    /* Class A private */
    b1 = (uint8_t)(a >> 24);
    if(b1 == 10) {
       return(6);
    }

    /* Class B private */
    b2 = (uint8_t)((a >> 16) & 0x00ff);
    if((b1 == 172) && (b2 >= 16) && (b2 <= 31)) {
       return(6);
    }

    /* Class C private */
    if((b1 == 192) && (b2 == 168)) {
       return(6);
    }

    if(IN_MULTICAST(ntohl(*address))) {
       return(8);
    }
    return(10);
}


/* ###### Get IPv6 address scope ######################################### */
static unsigned int scopeIPv6(const struct in6_addr* address)
{
   if(IN6_IS_ADDR_V4MAPPED(address)) {
#if defined SOLARIS
      return(scopeIPv4(&address->_S6_un._S6_u32[3]));
#elif defined LINUX
      return(scopeIPv4(&address->s6_addr32[3]));
#else
      return(scopeIPv4(&address->__u6_addr.__u6_addr32[3]));
#endif
   }
   if(IN6_IS_ADDR_UNSPECIFIED(address)) {
      return(0);
   }
   else if(IN6_IS_ADDR_MC_NODELOCAL(address)) {
      return(1);
   }
   else if(IN6_IS_ADDR_LOOPBACK(address)) {
      return(2);
   }
   else if(IN6_IS_ADDR_MC_LINKLOCAL(address)) {
      return(3);
   }
   else if(IN6_IS_ADDR_LINKLOCAL(address)) {
      return(4);
   }
   else if(IN6_IS_ADDR_MC_SITELOCAL(address)) {
      return(5);
   }
   else if(IN6_IS_ADDR_SITELOCAL(address)) {
      return(6);
   }
   else if(IN6_IS_ADDR_MC_ORGLOCAL(address)) {
      return(7);
   }
   else if(IN6_IS_ADDR_MC_GLOBAL(address)) {
      return(8);
   }
   return(10);
}


/* ###### Get address scope ############################################## */
unsigned int getScope(const struct sockaddr* address)
{
   if(address->sa_family == AF_INET) {
      return(scopeIPv4((uint32_t*)&((struct sockaddr_in*)address)->sin_addr));
   }
   else if(address->sa_family == AF_INET6) {
      return(scopeIPv6(&((struct sockaddr_in6*)address)->sin6_addr));
   }
   else {
      LOG_ERROR
      fprintf(stdlog, "Unsupported address family #%d\n",
              address->sa_family);
      LOG_END_FATAL
   }
   return(0);
}


/* ###### Get one address of highest scope from address array ############ */
const struct sockaddr* getBestScopedAddress(const struct sockaddr* addrs,
                                            int                    addrcnt)
{
   const struct sockaddr* bestScopedAddress = addrs;
   unsigned int           bestScope         = getScope(addrs);
   unsigned int           newScope;
   const struct sockaddr* a;
   int                    i;

   LOG_VERBOSE4
   fputs("Finding best scope out of address set:\n", stdlog);
   a = addrs;
   for(i = 0;i < addrcnt;i++) {
      fputs("   - ", stdlog);
      fputaddress(a, true, stdlog);
      fprintf(stdlog, ", scope=%u\n", getScope(a));
      a = (const struct sockaddr*)((long)a + (long)getSocklen(a));
   }
   LOG_END

   a = addrs;
   for(i = 1;i < addrcnt;i++) {
      a = (const struct sockaddr*)((long)a + (long)getSocklen(a));
      newScope = getScope(a);
      if(newScope > bestScope) {
         bestScopedAddress = a;
         bestScope   = newScope;
      }
   }

   LOG_VERBOSE4
   fputs("Using address ", stdlog);
   fputaddress(bestScopedAddress, true, stdlog);
   fprintf(stdlog, ", scope=%u\n", bestScope);
   LOG_END
   return(bestScopedAddress);
}


/* ###### Compare addresses ############################################## */
int addresscmp(const struct sockaddr* a1, const struct sockaddr* a2, const bool port)
{
   uint16_t     p1, p2;
   unsigned int s1, s2;
   uint32_t     x1[4];
   uint32_t     x2[4];
   int          result;

   LOG_VERBOSE5
   fputs("Comparing ",stdlog);
   fputaddress(a1,port,stdlog);
   fputs(" and ",stdlog);
   fputaddress(a2,port,stdlog);
   fputs("\n",stdlog);
   LOG_END

   if( ((a1->sa_family == AF_INET) || (a1->sa_family == AF_INET6)) &&
       ((a2->sa_family == AF_INET) || (a2->sa_family == AF_INET6)) ) {
      s1 = 1000000 - getScope((struct sockaddr*)a1);
      s2 = 1000000 - getScope((struct sockaddr*)a2);
      if(s1 < s2) {
         LOG_VERBOSE5
         fprintf(stdlog, "Result: less-than, s1=%d s2=%d\n",s1,s2);
         LOG_END
         return(-1);
      }
      else if(s1 > s2) {
         LOG_VERBOSE5
         fprintf(stdlog, "Result: greater-than, s1=%d s2=%d\n",s1,s2);
         LOG_END
         return(1);
      }

      if(a1->sa_family == AF_INET6) {
         memcpy((void*)&x1, (void*)&((struct sockaddr_in6*)a1)->sin6_addr, 16);
      }
      else {
         x1[0] = 0;
         x1[1] = 0;
         x1[2] = htonl(0xffff);
         x1[3] = *((uint32_t*)&((struct sockaddr_in*)a1)->sin_addr);
      }

      if(a2->sa_family == AF_INET6) {
         memcpy((void*)&x2, (void*)&((struct sockaddr_in6*)a2)->sin6_addr, 16);
      }
      else {
         x2[0] = 0;
         x2[1] = 0;
         x2[2] = htonl(0xffff);
         x2[3] = *((uint32_t*)&((struct sockaddr_in*)a2)->sin_addr);
      }

      result = memcmp((void*)&x1,(void*)&x2,16);
      if(result != 0) {
         LOG_VERBOSE5
         if(result < 0) {
            fputs("Result: less-than\n",stdlog);
         }
         else {
            fputs("Result: greater-than\n",stdlog);
         }
         LOG_END
         return(result);
      }

      if(port) {
         p1 = getPort((struct sockaddr*)a1);
         p2 = getPort((struct sockaddr*)a2);
         if(p1 < p2) {
            LOG_VERBOSE5
            fputs("Result: less-than\n",stdlog);
            LOG_END
            return(-1);
         }
         else if(p1 > p2) {
            LOG_VERBOSE5
            fputs("Result: greater-than\n",stdlog);
            LOG_END
            return(1);
         }
      }
      LOG_VERBOSE5
      fputs("Result: equal\n",stdlog);
      LOG_END
      return(0);
   }

   LOG_ERROR
   fprintf(stdlog, "Unsupported address family comparision #%d / #%d\n",a1->sa_family,a2->sa_family);
   LOG_END_FATAL
   return(0);
}


/* ###### Get port ####################################################### */
uint16_t getPort(const struct sockaddr* address)
{
   if(address != NULL) {
      switch(address->sa_family) {
         case AF_INET:
            return(ntohs(((struct sockaddr_in*)address)->sin_port));
          break;
         case AF_INET6:
            return(ntohs(((struct sockaddr_in6*)address)->sin6_port));
          break;
         default:
            LOG_ERROR
            fprintf(stdlog, "Unsupported address family #%d\n",
                    address->sa_family);
            LOG_END_FATAL
          break;
      }
   }
   return(0);
}


/* ###### Set port ####################################################### */
bool setPort(struct sockaddr* address, uint16_t port)
{
   if(address != NULL) {
      switch(address->sa_family) {
         case AF_INET:
            ((struct sockaddr_in*)address)->sin_port = htons(port);
            return(true);
          break;
         case AF_INET6:
            ((struct sockaddr_in6*)address)->sin6_port = htons(port);
            return(true);
          break;
         default:
            LOG_ERROR
            fprintf(stdlog, "Unsupported address family #%d\n",
                    address->sa_family);
            LOG_END_FATAL
          break;
      }
   }
   return(false);
}


/* ###### Get address family ############################################# */
int getFamily(const struct sockaddr* address)
{
   if(address != NULL) {
      return(address->sa_family);
   }
   return(AF_UNSPEC);
}


/* ###### Set non-blocking mode ########################################## */
bool setBlocking(int fd)
{
   int flags = ext_fcntl(fd,F_GETFL,0);
   if(flags != -1) {
      flags &= ~O_NONBLOCK;
      if(ext_fcntl(fd,F_SETFL, flags) == 0) {
         return(true);
      }
   }
   return(false);
}


/* ###### Set blocking mode ############################################## */
bool setNonBlocking(int fd)
{
   int flags = ext_fcntl(fd,F_GETFL,0);
   if(flags != -1) {
      flags |= O_NONBLOCK;
      if(ext_fcntl(fd,F_SETFL, flags) == 0) {
         return(true);
      }
   }
   return(false);
}


/* ###### Get padding #################################################### */
size_t getPadding(const size_t size, const size_t alignment)
{
   size_t padding = alignment - (size % alignment);
   if(padding == alignment) {
      padding = 0;
   }
   return(padding);
}


/* ###### Convert 24-bit value to host byte-order ######################## */
uint32_t ntoh24(const uint32_t value)
{
#if BYTE_ORDER == LITTLE_ENDIAN
   const uint32_t a = ((uint8_t*)&value)[0];
   const uint32_t b = ((uint8_t*)&value)[1];
   const uint32_t c = ((uint8_t*)&value)[2];
   const uint32_t result = (a << 16) | (b << 8) | c;
#elif BYTE_ORDER == BIG_ENDIAN
   const uint32_t result = value;
#else
#error Byte order is not defined!
#endif
   return(result);
}


/* ###### Convert 24-bit value to network byte-order ##################### */
uint32_t hton24(const uint32_t value)
{
   return(ntoh24(value));
}


/* ###### Check for support of IPv6 ###################################### */
bool checkIPv6()
{
   int sd = socket(AF_INET6,SOCK_DGRAM,IPPROTO_UDP);
   if(sd >= 0) {
      close(sd);
      return(true);
   }
   return(false);
}


/* ###### bind()/bindx() wrapper ######################################### */
bool bindplus(int                   sockfd,
              union sockaddr_union* addressArray,
              const size_t          addresses)
{
   struct sockaddr*     packedAddresses;
   union sockaddr_union anyAddress;
   bool                 autoSelect;
   unsigned short       port;
   unsigned int         i;
   unsigned int         j;
   int                  result;

   if(checkIPv6()) {
      string2address("[::]:0", &anyAddress);
   }
   else {
      string2address("0.0.0.0:0", &anyAddress);
   }

   if((addresses > 0) && (getPort((struct sockaddr*)&addressArray[0]) == 0)) {
      autoSelect = true;
   }
   else {
      autoSelect = false;
   }

   LOG_VERBOSE4
   fprintf(stdlog, "Binding socket %d to addresses {", sockfd);
   for(i = 0;i < addresses;i++) {
      fputaddress((struct sockaddr*)&addressArray[i], false, stdlog);
      if(i + 1 < addresses) {
         fputs(" ", stdlog);
      }
   }
   fprintf(stdlog, "}, port %u ...\n", getPort((struct sockaddr*)&addressArray[0]));
   LOG_END

   for(i = 0;i < MAX_AUTOSELECT_TRIALS;i++) {
      if(addresses == 0) {
         port = MIN_AUTOSELECT_PORT + (random16() % (MAX_AUTOSELECT_PORT - MIN_AUTOSELECT_PORT));
         setPort((struct sockaddr*)&anyAddress,port);

         LOG_VERBOSE4
         fprintf(stdlog, "Trying port %d for \"any\" address...\n",port);
         LOG_END

         result = ext_bind(sockfd,(struct sockaddr*)&anyAddress, getSocklen((struct sockaddr*)&anyAddress));
         if(result == 0) {
            LOG_VERBOSE4
            fputs("Successfully bound\n",stdlog);
            LOG_END
            return(true);
         }
         else if(errno == EPROTONOSUPPORT) {
            LOG_VERBOSE4
            logerror("bind() failed");
            LOG_END
            return(false);
         }
      }
      else {
         if(autoSelect) {
            port = MIN_AUTOSELECT_PORT + (random16() % (MAX_AUTOSELECT_PORT - MIN_AUTOSELECT_PORT));
            for(j = 0;j < addresses;j++) {
               setPort((struct sockaddr*)&addressArray[j],port);
            }
            LOG_VERBOSE5
            fprintf(stdlog, "Trying port %d...\n",port);
            LOG_END
         }
         if(addresses == 1) {
            result = ext_bind(sockfd, (struct sockaddr*)&addressArray[0], getSocklen((struct sockaddr*)&addressArray[0]));
         }
         else {
            packedAddresses = pack_sockaddr_union(addressArray, addresses);
            if(packedAddresses) {
               result = my_sctp_bindx(sockfd, packedAddresses, addresses, SCTP_BINDX_ADD_ADDR);
               free(packedAddresses);
            }
            else {
               result = -1;
               errno = ENOMEM;
            }
         }

         if(result == 0) {
            LOG_VERBOSE4
            fputs("Successfully bound\n",stdlog);
            LOG_END
            return(true);
         }
         else if(errno == EPROTONOSUPPORT) {
            LOG_VERBOSE4
            logerror("bind() failed");
            LOG_END
            return(false);
         }
         if(!autoSelect) {
            return(false);
         }
      }
   }
   return(false);
}


/* ###### sendmsg() wrapper ############################################## */
#define MAX_TRANSPORTADDRESSES 32
int sendtoplus(int                      sockfd,
               const void*              buffer,
               const size_t             length,
               const int                flags,
               union sockaddr_union*    toaddrs,
               const size_t             toaddrcnt,
               const uint32_t           ppid,
               const sctp_assoc_t       assocID,
               const uint16_t           streamID,
               const uint32_t           timeToLive,
               const unsigned long long timeout)
{
   union sockaddr_union   addressArray[MAX_TRANSPORTADDRESSES];
   size_t                 addresses = 0;
   struct sctp_sndrcvinfo sri;
   struct timeval         selectTimeout;
   fd_set                 fdset;
   size_t                 i;
   int                    result;
   char*                  p;

   LOG_VERBOSE4
   fprintf(stdlog, "sendmsg(%d/A%u, %u bytes) PPID=$%08x streamID=%u flags=$%x toaddrs=%p toaddrcnt=%u...\n",
           sockfd, (unsigned int)assocID, (unsigned int)length, ppid, streamID, flags,toaddrs, (unsigned int)toaddrcnt);
   LOG_END

   setNonBlocking(sockfd);
   if((assocID != 0) || (ppid != 0) || (streamID != 0)) {
      memset(&sri, 0, sizeof(sri));
      sri.sinfo_assoc_id = assocID;
      sri.sinfo_stream   = streamID;
      sri.sinfo_ppid     = htonl(ppid);
      /* --- Already resetted to 0 ---
      sri.sinfo_flags    = 0;
      sri.sinfo_ssn      = 0;
      sri.sinfo_tsn      = 0;
      sri.sinfo_context  = 0;
      */
      sri.sinfo_timetolive = timeToLive;

      if(toaddrs) {
         p = (char*)&addressArray[0];
         addresses = min(toaddrcnt, MAX_TRANSPORTADDRESSES);
         for(i = 0;i < addresses;i++) {
            LOG_VERBOSE5
            fprintf(stdlog, "Address #%u is ", (unsigned int)i + 1);
            fputaddress(&toaddrs[i].sa, true, stdlog);
            fputs("\n", stdlog);
            LOG_END
            switch(toaddrs[i].sa.sa_family) {
               case AF_INET:
                  memcpy(p, &toaddrs[i].in, sizeof(struct sockaddr_in));
                  p = (char*)((long)p + sizeof(struct sockaddr_in));
                  break;
               case AF_INET6:
                  memcpy(p, &toaddrs[i].in6, sizeof(struct sockaddr_in6));
                  p = (char*)((long)p + sizeof(struct sockaddr_in6));
                  break;
               default:
                  LOG_ERROR
                  fputs("Bad address family\n", stdlog);
                  LOG_END_FATAL
                  break;
            }
         }
         LOG_VERBOSE5
         fputs("Calling sctp_sendx() with addresses...\n", stdlog);
         LOG_END
         result = sctp_sendx(sockfd, buffer, length,
                             (struct sockaddr*)&addressArray, addresses,
                             &sri, flags);
      }
      else {
         LOG_VERBOSE5
         fputs("Calling sctp_send() with AssocID...\n", stdlog);
         LOG_END
         result = sctp_send(sockfd, buffer, length,
                            &sri, flags);
      }
   }
   else {
      LOG_VERBOSE5
      fputs("Calling sendto()...\n", stdlog);
      LOG_END
      result = ext_sendto(sockfd, buffer, length, flags,
                          (struct sockaddr*)toaddrs,
                          (toaddrs != NULL) ? getSocklen((struct sockaddr*)toaddrs) : 0);
   }

   if((timeout > 0) && ((result < 0) && (errno == EWOULDBLOCK))) {
      LOG_VERBOSE4
      fprintf(stdlog, "sendmsg(%d/A%u) would block, waiting with timeout %lld [us]...\n",
              sockfd, (unsigned int)assocID, timeout);
      LOG_END

      FD_ZERO(&fdset);
      FD_SET(sockfd, &fdset);
      selectTimeout.tv_sec  = timeout / 1000000;
      selectTimeout.tv_usec = timeout % 1000000;
      result = ext_select(sockfd + 1,
                          NULL, &fdset, NULL,
                          &selectTimeout);
      if((result > 0) && FD_ISSET(sockfd, &fdset)) {
         LOG_VERBOSE4
         fprintf(stdlog, "retrying sendmsg(%d/A%u, %u bytes)...\n",
                 sockfd, (unsigned int)assocID, (unsigned int)length);
         LOG_END
         if((assocID != 0) || (ppid != 0) || (streamID != 0)) {
            if(toaddrs) {
               LOG_VERBOSE5
               fputs("Calling sctp_sendx() with addresses...\n", stdlog);
               LOG_END
               result = sctp_sendx(sockfd, buffer, length,
                                   (struct sockaddr*)&addressArray, addresses,
                                   &sri, flags);
            }
            else {
               LOG_VERBOSE5
               fputs("Calling sctp_send() with AssocID...\n", stdlog);
               LOG_END
               result = sctp_send(sockfd, buffer, length,
                                  &sri, flags);
            }
         }
         else {
            LOG_VERBOSE5
            fputs("Calling sctp_sendto()...\n", stdlog);
            LOG_END
            result = ext_sendto(sockfd, buffer, length, flags,
                              (struct sockaddr*)toaddrs, (toaddrs != NULL) ? getSocklen((struct sockaddr*)toaddrs) : 0);
         }
      }
   }

   LOG_VERBOSE4
   fprintf(stdlog, "sendmsg(%d/A%u) result=%d; %s\n",
           sockfd, (unsigned int)assocID, result, strerror(errno));
   LOG_END

   return(result);
}


/* ###### recvmsg() wrapper ############################################## */
int recvfromplus(int                      sockfd,
                 void*                    buffer,
                 size_t                   length,
                 int*                     flags,
                 struct sockaddr*         from,
                 socklen_t*               fromlen,
                 uint32_t*                ppid,
                 sctp_assoc_t*            assocID,
                 uint16_t*                streamID,
                 const unsigned long long timeout)
{
   struct sctp_sndrcvinfo* sri;
   struct iovec    iov = { (char*)buffer, length };
   struct cmsghdr* cmsg;
   size_t          cmsglen = CMSG_SPACE(sizeof(struct sctp_sndrcvinfo));
   char            cbuf[CMSG_SPACE(sizeof(struct sctp_sndrcvinfo))];
   struct msghdr msg = {
#ifdef __APPLE__
      (char*)from,
#else
      from,
#endif
      (fromlen != NULL) ? *fromlen : 0,
      &iov, 1,
      cbuf, cmsglen,
      *flags
   };
   struct timeval selectTimeout;
   fd_set         fdset;
   int            result;
   int            cc;

   if(ppid     != NULL) *ppid     = 0;
   if(streamID != NULL) *streamID = 0;
   if(assocID  != NULL) *assocID  = 0;

   LOG_VERBOSE5
   fprintf(stdlog, "recvmsg(%d, %u bytes)...\n",
           sockfd, (unsigned int)iov.iov_len);
   LOG_END

   setNonBlocking(sockfd);
   cc = ext_recvmsg(sockfd, &msg, *flags);
   if((timeout > 0) && ((cc < 0) && (errno == EWOULDBLOCK))) {
      LOG_VERBOSE5
      fprintf(stdlog, "recvmsg(%d) would block, waiting with timeout %lld [us]...\n",
              sockfd, timeout);
      LOG_END

      FD_ZERO(&fdset);
      FD_SET(sockfd, &fdset);
      selectTimeout.tv_sec  = timeout / 1000000;
      selectTimeout.tv_usec = timeout % 1000000;

      result = ext_select(sockfd + 1,
                          &fdset, NULL, NULL,
                          &selectTimeout);
      if((result > 0) && FD_ISSET(sockfd, &fdset)) {
         LOG_VERBOSE5
         fprintf(stdlog, "retrying recvmsg(%d, %u bytes)...\n",
                 sockfd, (unsigned int)iov.iov_len);
         LOG_END
#ifdef __APPLE__
         msg.msg_name       = (char*)from;
#else
         msg.msg_name       = from;
#endif
         msg.msg_namelen    = (fromlen != NULL) ? *fromlen : 0;
         iov.iov_base       = (char*)buffer;
         iov.iov_len        = length;
         msg.msg_iov        = &iov;
         msg.msg_iovlen     = 1;
         msg.msg_control    = cbuf;
         msg.msg_controllen = cmsglen;
         msg.msg_flags      = *flags;
         cc = ext_recvmsg(sockfd, &msg, *flags);
      }
      else {
         LOG_VERBOSE5
         fprintf(stdlog, "recvmsg(%d) timed out\n", sockfd);
         LOG_END
         cc    = -1;
         errno = EWOULDBLOCK;
      }
   }

   LOG_VERBOSE4
   fprintf(stdlog, "recvmsg(%d) result=%d data=%u/%u control=%u; %s\n",
           sockfd, cc, (unsigned int)iov.iov_len, (unsigned int)length, (unsigned int)msg.msg_controllen, (cc < 0) ? strerror(errno) : "");
   LOG_END

   if(cc < 0) {
      return(cc);
   }

   if((msg.msg_control != NULL) && (msg.msg_controllen > 0)) {
      cmsg = (struct cmsghdr*)CMSG_FIRSTHDR(&msg);
      if((cmsg != NULL) &&
         (cmsg->cmsg_len   == CMSG_LEN(sizeof(struct sctp_sndrcvinfo))) &&
         (cmsg->cmsg_level == IPPROTO_SCTP)                             &&
         (cmsg->cmsg_type  == SCTP_SNDRCV)) {
         sri = (struct sctp_sndrcvinfo*)CMSG_DATA(cmsg);
         if(ppid     != NULL) *ppid     = ntohl(sri->sinfo_ppid);
         if(streamID != NULL) *streamID = sri->sinfo_stream;
         if(assocID  != NULL) *assocID  = sri->sinfo_assoc_id;
         LOG_VERBOSE4
         fprintf(stdlog, "SCTP_SNDRCV: ppid=$%08x streamID=%u assocID=%u\n",
                 sri->sinfo_ppid, sri->sinfo_stream, (unsigned int)sri->sinfo_assoc_id);
         LOG_END
      }
   }
   if(fromlen != NULL) {
      *fromlen = msg.msg_namelen;
   }
   *flags = msg.msg_flags;

   return(cc);
}


/* ###### Get socklen for given address ################################## */
size_t getSocklen(const struct sockaddr* address)
{
   switch(address->sa_family) {
      case AF_INET:
         return(sizeof(struct sockaddr_in));
       break;
      case AF_INET6:
         return(sizeof(struct sockaddr_in6));
       break;
      default:
         LOG_ERROR
         fprintf(stdlog, "Unsupported address family #%d\n",
                 address->sa_family);
         LOG_END_FATAL
         return(sizeof(struct sockaddr));
       break;
   }
}


/* ###### Multicast group management ##################################### */
static bool multicastGroupMgt(int              sockfd,
                              struct sockaddr* address,
                              const char*      interface,
                              const bool       add)
{
   struct ip_mreq   mreq;
   struct ifreq     ifr;
   struct ipv6_mreq mreq6;
   int              result;

   if(address->sa_family == AF_INET) {
      mreq.imr_multiaddr = ((struct sockaddr_in*)address)->sin_addr;
      if(interface != NULL) {
         strcpy(ifr.ifr_name, interface);
         if(ext_ioctl(sockfd, SIOCGIFADDR, &ifr) != 0) {
            return(false);
         }
         mreq.imr_interface = ((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr;
      }
      else {
         memset((char*)&mreq.imr_interface, 0, sizeof(mreq.imr_interface));
      }
      /* BSD sets errno to EADDRINUSE if socket has already been joined to
         the multicast group. This is no real error. */
      result = ext_setsockopt(sockfd,IPPROTO_IP,
                              add ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP,
                              &mreq, sizeof(mreq));
      return((result == 0) || (errno == EADDRINUSE));
   }
   else if(address->sa_family == AF_INET6) {
      memcpy((char*)&mreq6.ipv6mr_multiaddr,
             (char*)&((struct sockaddr_in6*)address)->sin6_addr,
             sizeof(((struct sockaddr_in6*)address)->sin6_addr));
      if(interface != NULL) {
         mreq6.ipv6mr_interface = if_nametoindex(interface);
      }
      else {
         mreq6.ipv6mr_interface = 0;
      }
      result = ext_setsockopt(sockfd,IPPROTO_IPV6,
                              add ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP,
                              &mreq6, sizeof(mreq6));
      /* BSD sets errno to EADDRINUSE if socket has already been joined to
         the multicast group. This is no real error. */
      return((result == 0) || (errno == EADDRINUSE));
   }
   CHECK(false);
   return(false);
}


/* ###### Join or leave multicast group ################################## */
bool joinOrLeaveMulticastGroup(int                         sd,
                               const union sockaddr_union* groupAddress,
                               const bool                  add)
{
   return(multicastGroupMgt(sd, (struct sockaddr*)groupAddress, NULL, add));
}


/* ###### Set SO_REUSEADDR and SO_REUSEPORT (if supported) ############### */
bool setReusable(int sd, int on)
{
   if(ext_setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
      return(false);
   }
#if !defined (LINUX) && !defined (SOLARIS)
   if(ext_setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) != 0) {
      return(false);
   }
#endif
   return(true);
}


/* ###### Tune SCTP parameters ############################################ */
bool tuneSCTP(int sockfd, sctp_assoc_t assocID, struct TagItem* tags)
{
   struct sctp_rtoinfo     rtoinfo;
   struct sctp_paddrparams peerParams;
   struct sctp_assocparams assocParams;
   union sockaddr_union*   addrs;
   socklen_t               size;
   int                     i, n;
   bool                    result = true;

   size = (socklen_t)sizeof(rtoinfo);
   rtoinfo.srto_assoc_id = assocID;
   if(ext_getsockopt(sockfd, IPPROTO_SCTP, SCTP_RTOINFO, &rtoinfo, &size) == 0) {
      LOG_VERBOSE3
      fprintf(stdlog, "  Current RTO info of socket %d, assoc %u:\n", sockfd, (unsigned int)rtoinfo.srto_assoc_id);
      fprintf(stdlog, "  Initial SRTO: %u\n", rtoinfo.srto_initial);
      fprintf(stdlog, "  Min SRTO:     %u\n", rtoinfo.srto_min);
      fprintf(stdlog, "  Max SRTO:     %u\n", rtoinfo.srto_max);
      LOG_END

      rtoinfo.srto_min     = tagListGetData(tags, TAG_TuneSCTP_MinRTO,     rtoinfo.srto_min);
      rtoinfo.srto_max     = tagListGetData(tags, TAG_TuneSCTP_MaxRTO,     rtoinfo.srto_max);
      rtoinfo.srto_initial = tagListGetData(tags, TAG_TuneSCTP_InitialRTO, rtoinfo.srto_initial);

      if(ext_setsockopt(sockfd, IPPROTO_SCTP, SCTP_RTOINFO, &rtoinfo, (socklen_t)sizeof(rtoinfo)) < 0) {
         result = false;
         LOG_WARNING
         logerror("SCTP_RTOINFO");
         fprintf(stdlog, "Unable to update RTO info of socket %d, assoc %u\n", sockfd, (unsigned int)assocID);
         LOG_END
      }
      else {
         LOG_VERBOSE2
         fprintf(stdlog, "  New RTO info of socket %d, assoc %u:\n", sockfd, (unsigned int)rtoinfo.srto_assoc_id);
         fprintf(stdlog, "  Initial SRTO: %u\n", rtoinfo.srto_initial);
         fprintf(stdlog, "  Min SRTO:     %u\n", rtoinfo.srto_min);
         fprintf(stdlog, "  Max SRTO:     %u\n", rtoinfo.srto_max);
         LOG_END
      }
   }
   else {
      LOG_VERBOSE2
      logerror("Cannot get RTO info -> Skipping RTO info configuration");
      LOG_END
   }

   size = (socklen_t)sizeof(assocParams);
   assocParams.sasoc_assoc_id = assocID;
   if(ext_getsockopt(sockfd, IPPROTO_SCTP, SCTP_ASSOCINFO, &assocParams, &size) == 0) {
      LOG_VERBOSE3
      fprintf(stdlog, "Current Assoc info of socket %d, assoc %u:\n", sockfd, (unsigned int)assocParams.sasoc_assoc_id);
      fprintf(stdlog, "  AssocMaxRXT:       %u\n", assocParams.sasoc_asocmaxrxt);
      fprintf(stdlog, "  Peer Destinations: %u\n", assocParams.sasoc_number_peer_destinations);
      fprintf(stdlog, "  Peer RWND:         %u\n", assocParams.sasoc_peer_rwnd);
      fprintf(stdlog, "  Local RWND:        %u\n", assocParams.sasoc_local_rwnd);
      fprintf(stdlog, "  Cookie Life:       %u\n", assocParams.sasoc_cookie_life);
      LOG_END

      assocParams.sasoc_asocmaxrxt  = tagListGetData(tags, TAG_TuneSCTP_AssocMaxRXT, assocParams.sasoc_asocmaxrxt);
      assocParams.sasoc_local_rwnd  = tagListGetData(tags, TAG_TuneSCTP_LocalRWND,   assocParams.sasoc_local_rwnd);
      assocParams.sasoc_cookie_life = tagListGetData(tags, TAG_TuneSCTP_CookieLife,  assocParams.sasoc_cookie_life);

      if(ext_setsockopt(sockfd, IPPROTO_SCTP, SCTP_ASSOCINFO, &assocParams, (socklen_t)sizeof(assocParams)) < 0) {
         result = false;
         LOG_WARNING
         logerror("SCTP_ASSOCINFO");
         fprintf(stdlog, "Unable to update Assoc info of socket %d, assoc %u\n", sockfd, (unsigned int)assocID);
         LOG_END
      }
      else {
         LOG_VERBOSE2
         fprintf(stdlog, "New Assoc info of socket %d, assoc %u:\n", sockfd, (unsigned int)assocParams.sasoc_assoc_id);
         fprintf(stdlog, "  AssocMaxRXT:       %u\n", assocParams.sasoc_asocmaxrxt);
         fprintf(stdlog, "  Peer Destinations: %u\n", assocParams.sasoc_number_peer_destinations);
         fprintf(stdlog, "  Peer RWND:         %u\n", assocParams.sasoc_peer_rwnd);
         fprintf(stdlog, "  Local RWND:        %u\n", assocParams.sasoc_local_rwnd);
         fprintf(stdlog, "  Cookie Life:       %u\n", assocParams.sasoc_cookie_life);
         LOG_END
      }
   }
   else {
      LOG_VERBOSE2
      logerror("Cannot get Assoc info -> Skipping Assoc info configuration");
      LOG_END
   }

   n = getpaddrsplus(sockfd, assocID, &addrs);
   if(n >= 0) {
      for(i = 0;i < n;i++) {
         memset(&peerParams, 0, sizeof(peerParams));
         peerParams.spp_assoc_id = assocID;
         memcpy((void*)&(peerParams.spp_address),
                (const void*)&addrs[i], sizeof(union sockaddr_union));
         size = (socklen_t)sizeof(peerParams);

         if(sctp_opt_info(sockfd, assocID, SCTP_PEER_ADDR_PARAMS,
                          (void*)&peerParams, &size) == 0) {
            LOG_VERBOSE3
            fputs("Old peer parameters for address ", stdlog);
            fputaddress((struct sockaddr*)&(peerParams.spp_address), false, stdlog);
#ifdef HAVE_SPP_FLAGS
            fprintf(stdlog, " on socket %d, assoc %u: hb=%d maxrxt=%d flags=$%x\n",
                    sockfd, (unsigned int)assocID,
                    peerParams.spp_hbinterval, peerParams.spp_pathmaxrxt, peerParams.spp_flags);
#else
            fprintf(stdlog, " on socket %d, assoc %u: hb=%d maxrxt=%d\n",
                    sockfd, (unsigned int)assocID,
                    peerParams.spp_hbinterval, peerParams.spp_pathmaxrxt);
#endif
            LOG_END

            peerParams.spp_hbinterval = tagListGetData(tags, TAG_TuneSCTP_Heartbeat,  peerParams.spp_hbinterval);
#ifdef HAVE_SPP_FLAGS
            if(peerParams.spp_hbinterval > 0) {
               peerParams.spp_flags |= SPP_HB_ENABLE;
            }
            else {
               peerParams.spp_flags |= SPP_HB_DISABLE;
            }
#endif
            peerParams.spp_pathmaxrxt = tagListGetData(tags, TAG_TuneSCTP_PathMaxRXT, peerParams.spp_pathmaxrxt);;

            if(sctp_opt_info(sockfd, 0, SCTP_PEER_ADDR_PARAMS,
                             (void *)&peerParams, &size) < 0) {
               LOG_VERBOSE2
               fputs("Unable to set peer parameters for address ", stdlog);
               fputaddress((struct sockaddr*)&(peerParams.spp_address), false, stdlog);
               fprintf(stdlog, " on socket %d, assoc %u\n",
                       sockfd, (unsigned int)assocID);
               LOG_END
            }
            else {
               LOG_VERBOSE2
               fputs("New peer parameters for address ", stdlog);
               fputaddress((struct sockaddr*)&(peerParams.spp_address), false, stdlog);
#ifdef HAVE_SPP_FLAGS
                fprintf(stdlog, " on socket %d, assoc %u: hb=%d maxrxt=%d flags=$%x\n",
                        sockfd, (unsigned int)assocID,
                        peerParams.spp_hbinterval, peerParams.spp_pathmaxrxt, peerParams.spp_flags);
#else
                fprintf(stdlog, " on socket %d, assoc %u: hb=%d maxrxt=%d\n",
                        sockfd, (unsigned int)assocID,
                        peerParams.spp_hbinterval, peerParams.spp_pathmaxrxt);
#endif
               LOG_END
            }
         }
         else {
            LOG_VERBOSE2
            fputs("Unable to get peer parameters for address ", stdlog);
            fputaddress((struct sockaddr*)&(peerParams.spp_address), false, stdlog);
            fprintf(stdlog, " on socket %d, assoc %u\n",
                    sockfd, (unsigned int)assocID);
            LOG_END
         }
      }
      free(addrs);
   }
   else {
      result = false;
      LOG_VERBOSE2
      fputs("No peer addresses -> skipping peer parameters\n", stdlog);
      LOG_END
   }

   return(result);
}


/* ###### Establish connection ########################################### */
int connectplus(int                   sockfd,
                union sockaddr_union* addressArray,
                const size_t          addresses)
{
   struct sockaddr* packedAddresses;
   int              result;

   packedAddresses = pack_sockaddr_union(addressArray, addresses);
   if(packedAddresses) {
      result = my_sctp_connectx(sockfd, packedAddresses, addresses);
      free(packedAddresses);
   }
   else {
      result = -ENOMEM;
   }
   return(result);
}


/* ###### Create socket and establish connection ######################### */
int establish(const int                socketDomain,
              const int                socketType,
              const int                socketProtocol,
              union sockaddr_union*    addressArray,
              const size_t             addresses,
              const unsigned long long timeout)
{
   fd_set               fdset;
   struct timeval       to;
   struct sockaddr*     packedAddresses;
   union sockaddr_union peerAddress;
   socklen_t            peerAddressLen;
   int                  result;
   int                  sockfd;
   size_t               i;

   LOG_VERBOSE
   fprintf(stdlog, "Trying to establish connection via socket(%d,%d,%d)\n",
           socketDomain, socketType, socketProtocol);
   LOG_END

   sockfd = ext_socket(socketDomain, socketType, socketProtocol);
   if(sockfd >= 0) {
      setNonBlocking(sockfd);

      LOG_VERBOSE2
      fprintf(stdlog, "Trying to establish association from socket %d to address(es) {", sockfd);
      for(i = 0;i < addresses;i++) {
         fputaddress((struct sockaddr*)&addressArray[i], false, stdlog);
         if(i + 1 < addresses) {
            fputs(" ", stdlog);
         }
      }
      fprintf(stdlog, "}, port %u...\n", getPort((struct sockaddr*)&addressArray[0]));
      LOG_END

      if(socketProtocol == IPPROTO_SCTP) {
         packedAddresses = pack_sockaddr_union(addressArray, addresses);
         if(packedAddresses) {
            result = my_sctp_connectx(sockfd, packedAddresses, addresses);
            free(packedAddresses);
         }
         else {
            errno = ENOMEM;
            return(-1);
         }
      }
      else {
         if(addresses != 1) {
            LOG_ERROR
            fputs("Multiple addresses are only valid for SCTP\n", stdlog);
            LOG_END
            return(-1);
         }
         result = ext_connect(sockfd, (struct sockaddr*)&addressArray[0],
                              getSocklen((struct sockaddr*)&addressArray[0]));
      }
      if( (((result < 0) && (errno == EINPROGRESS)) || (result >= 0)) ) {
         FD_ZERO(&fdset);
         FD_SET(sockfd, &fdset);
         to.tv_sec  = timeout / 1000000;
         to.tv_usec = timeout % 1000000;
         LOG_VERBOSE2
         fprintf(stdlog, "Waiting for association establishment with timeout %lld [us]...\n",
                 ((unsigned long long)to.tv_sec * (unsigned long long)1000000) + (unsigned long long)to.tv_usec);
         LOG_END
         result = ext_select(sockfd + 1, NULL, &fdset, NULL, &to);
         LOG_VERBOSE2
         fprintf(stdlog, "result=%d\n", result);
         LOG_END
         if(result > 0) {
            peerAddressLen = sizeof(peerAddress);
            if(ext_getpeername(sockfd, (struct sockaddr*)&peerAddress, &peerAddressLen) >= 0) {
               LOG_VERBOSE2
               fputs("Successfully established connection to address(es) {", stdlog);
               for(i = 0;i < addresses;i++) {
                  fputaddress((struct sockaddr*)&addressArray[i], false, stdlog);
                  if(i + 1 < addresses) {
                     fputs(" ", stdlog);
                  }
               }
               fprintf(stdlog, "}, port %u\n", getPort((struct sockaddr*)&addressArray[0]));
               LOG_END
               return(sockfd);
            }
            else {
               LOG_VERBOSE2
               logerror("peername");
               fputs("Connection establishment to address(es) {", stdlog);
               for(i = 0;i < addresses;i++) {
                  fputaddress((struct sockaddr*)&addressArray[i], false, stdlog);
                  if(i + 1 < addresses) {
                     fputs(" ", stdlog);
                  }
               }
               fprintf(stdlog, "}, port %u failed\n", getPort((struct sockaddr*)&addressArray[0]));
               LOG_END
            }
         }
         else {
            LOG_VERBOSE2
            fputs("select() call timed out\n", stdlog);
            LOG_END
         }
      }

      LOG_VERBOSE2
      fputs("connect()/connectx() failed\n", stdlog);
      LOG_END
      ext_close(sockfd);
   }

   LOG_VERBOSE
   fputs("Connection establishment failed\n", stdlog);
   LOG_END
   return(-1);
}


/* ###### Get local addresses ############################################### */
size_t getladdrsplus(const int              fd,
                     const sctp_assoc_t     assocID,
                     union sockaddr_union** addressArray)
{
   struct sockaddr* packedAddresses;
#ifdef SOLARIS
   int addrs = sctp_getladdrs(fd, assocID, (void**)&packedAddresses);
#else
   int addrs = sctp_getladdrs(fd, assocID, &packedAddresses);
#endif
#ifdef LINUX
#ifdef HAVE_KERNEL_SCTP
   uint16_t port;
   size_t   addrs2;
   size_t   j;
#endif
#endif
   int    i;

   if(addrs > 0) {
#ifdef LINUX
#ifdef HAVE_KERNEL_SCTP
#warning Using getladdrs() INADDR_ANY bugfix for lksctp!
      if((addrs == 1) &&
         ( ( (((struct sockaddr*)packedAddresses)->sa_family == AF_INET) &&
                (((struct sockaddr_in*)packedAddresses)->sin_addr.s_addr == 0) ) ||
             ( (((struct sockaddr*)packedAddresses)->sa_family == AF_INET6) &&
                (IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6*)packedAddresses)->sin6_addr))))) {
         port = getPort((struct sockaddr*)packedAddresses);
         sctp_freeladdrs(packedAddresses);

         if(obtainLocalAddresses(addressArray, &addrs2, ASF_HideMulticast) == true) {
            for(j = 0;j < addrs2;j++) {
               setPort(&((*addressArray)[j].sa), port);
            }
            return(addrs2);
         }
         return(0);
      }
#endif
#endif

      *addressArray = unpack_sockaddr(packedAddresses, addrs);
      sctp_freeladdrs(packedAddresses);

      LOG_VERBOSE5
      fprintf(stdlog, "getladdrsplus() - Number of addresses: %u\n", (unsigned int)addrs);
      for(i = 0;i < addrs;i++) {
         fprintf(stdlog, " - #%u: ", (unsigned int)i);
         fputaddress(&(*addressArray)[i].sa, true, stdlog);
         fputs("\n", stdlog);
      }
      LOG_END
      return((size_t)addrs);
   }
   return(0);
}


/* ###### Get peer addresses ############################################# */
size_t getpaddrsplus(const int              fd,
                     const sctp_assoc_t     assocID,
                     union sockaddr_union** addressArray)
{
   struct sockaddr* packedAddresses;
#ifdef SOLARIS
   int addrs = sctp_getpaddrs(fd, assocID, (void **)&packedAddresses);
#else
   int addrs = sctp_getpaddrs(fd, assocID, &packedAddresses);
#endif
   if(addrs > 0) {
      *addressArray = unpack_sockaddr(packedAddresses, addrs);
      sctp_freepaddrs(packedAddresses);
      return((size_t)addrs);
   }
   return(0);
}


/* ###### Send SCTP ABORT ################################################ */
int sendabort(int sockfd, sctp_assoc_t assocID)
{

printf("\n######### SEND ABORT:  %d A%d\n\n",sockfd,assocID);

   return(sendtoplus(sockfd, NULL, 0, SCTP_ABORT,
                     NULL, 0,
                     0, assocID, 0, 0, 0));
}


/* ###### Send SCTP SHUTDOWN ############################################# */
int sendshutdown(int sockfd, sctp_assoc_t assocID)
{
   return(sendtoplus(sockfd, NULL, 0, SCTP_EOF,
                     NULL, 0,
                     0, assocID, 0, 0, 0));
}


/* ###### Convert byte order of 64 bit value ############################# */
static uint64_t byteswap64(const uint64_t value)
{
#if (__BYTE_ORDER == __LITTLE_ENDIAN)
   const uint32_t a = (uint32_t)(value >> 32);
   const uint32_t b = (uint32_t)(value & 0xffffffff);
   return( (int64_t)((a << 24) | ((a & 0x0000ff00) << 8) |
           ((a & 0x00ff0000) >> 8) | (a >> 24)) |
           ((int64_t)((b << 24) | ((b & 0x0000ff00) << 8) |
           ((b & 0x00ff0000) >> 8) | (b >> 24)) << 32) );
#elif (__BYTE_ORDER == __BIG_ENDIAN)
   return(value);
#else
#error Byte order undefined!
#endif
}


/* ###### Convert byte order of 64 bit value ############################# */
uint64_t hton64(const uint64_t value)
{
   return(byteswap64(value));
}


/* ###### Convert byte order of 64 bit value ############################# */
uint64_t ntoh64(const uint64_t value)
{
   return(byteswap64(value));
}
