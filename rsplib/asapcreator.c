/*
 *  $Id: asapcreator.c,v 1.1 2004/07/13 09:12:09 dreibh Exp $
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
 * Purpose: ASAP Creator
 *
 */


#include "tdtypes.h"
#include "loglevel.h"
#include "utilities.h"
#include "netutilities.h"
#include "asapcreator.h"

#include <netinet/in.h>
#include <ext_socket.h>


/* ###### Begin message ################################################## */
static bool beginMessage(struct ASAPMessage* message,
                         const uint8_t       type,
                         const uint8_t       flags)
{
   struct asap_header* header = (struct asap_header*)getSpace(message,sizeof(struct asap_header));
   if(header == NULL) {
      return(false);
   }

   header->ah_type   = type;
   header->ah_flags  = flags;
   header->ah_length = 0xffff;
   return(true);
}


/* ###### Finish message ################################################# */
static bool finishMessage(struct ASAPMessage* message)
{
   char*  pad;
   size_t padding = getPadding(message->Position,4);

   struct asap_header* header = (struct asap_header*)message->Buffer;
   if(message->BufferSize >= sizeof(struct asap_header)) {
      header->ah_length = htons((uint16_t)message->Position);

      pad = (char*)getSpace(message,padding);
      memset(pad,0,padding);
      return(true);
   }
   return(false);
}


/* ###### Begin TLV ###################################################### */
static bool beginTLV(struct ASAPMessage* message,
                     size_t*             tlvPosition,
                     const uint16_t      type)
{
   struct asap_tlv_header* header;

   *tlvPosition = message->Position;
   header = (struct asap_tlv_header*)getSpace(message,sizeof(struct asap_tlv_header));
   if(header == NULL) {
      return(false);
   }

   header->atlv_type   = htons(type);
   header->atlv_length = 0xffff;
   return(true);
}


/* ###### Finish TLV ##################################################### */
static bool finishTLV(struct ASAPMessage* message,
                      const size_t        tlvPosition)
{
   struct asap_tlv_header* header  = (struct asap_tlv_header*)&message->Buffer[tlvPosition];
   size_t                  length  = message->Position - tlvPosition;
   size_t                  padding = getPadding(length,4);
   char*                   pad;

   if(message->BufferSize >= sizeof(struct asap_tlv_header)) {
      header->atlv_length = htons(length);

      pad = (char*)getSpace(message,padding);
      memset(pad,0,padding);
      return(true);
   }
   return(false);
}


/* ###### Create pool handle parameter ################################### */
static bool createPoolHandleParameter(struct ASAPMessage*      message,
                                      const struct PoolHandle* poolHandle)
{
   char*    handle;
   size_t tlvPosition = 0;

   if(poolHandle == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginTLV(message,&tlvPosition,ATT_POOL_HANDLE) == false) {
      return(false);
   }

   handle = (char*)getSpace(message,poolHandle->Length);
   if(handle == NULL) {
      return(false);
   }
   memcpy(handle,(char*)&poolHandle->Handle,poolHandle->Length);

   return(finishTLV(message,tlvPosition));
}


/* ###### Create address parameter ####################################### */
static bool createAddressParameter(struct ASAPMessage* message,
                                   struct sockaddr*    address)
{
   size_t               tlvPosition = 0;
   char*                output;
   struct sockaddr_in*  in;
#ifdef HAVE_IPV6
   struct sockaddr_in6* in6;
#endif

   switch(address->sa_family) {
      case AF_INET:
         if(beginTLV(message,&tlvPosition,ATT_IPv4_ADDRESS) == false) {
            return(false);
         }
         in = (struct sockaddr_in*)address;
         output = (char*)getSpace(message,4);
         if(output == NULL) {
            return(false);
         }
         memcpy(output,&in->sin_addr,4);
       break;
#ifdef HAVE_IPV6
      case AF_INET6:
         if(beginTLV(message,&tlvPosition,ATT_IPv6_ADDRESS) == false) {
            return(false);
         }
         in6 = (struct sockaddr_in6*)address;
         output = (char*)getSpace(message,16);
         if(output == NULL) {
            return(false);
         }
         memcpy(output,&in6->sin6_addr,16);
       break;
#endif
      default:
         LOG_ERROR
         fputs("Unknown address family\n", stdlog);
         LOG_END
         return(false);
       break;
   }

   return(finishTLV(message,tlvPosition));
}


/* ###### Create transport parameter ###################################### */
static bool createTransportParameter(struct ASAPMessage*      message,
                                     struct TransportAddress* transportAddress,
                                     const uint16_t           transportUse)
{
   size_t                              tlvPosition = 0;
   struct asap_udptransportparameter*  utp;
   struct asap_tcptransportparameter*  ttp;
   struct asap_sctptransportparameter* stp;
   uint16_t                            type;
   size_t                              i;

   if(transportAddress == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   switch(transportAddress->Protocol) {
      case IPPROTO_SCTP:
         type = ATT_SCTP_TRANSPORT;
       break;
      case IPPROTO_TCP:
         type = ATT_TCP_TRANSPORT;
       break;
      case IPPROTO_UDP:
         type = ATT_UDP_TRANSPORT;
       break;
      default:
         LOG_ERROR
         fprintf(stdlog,"Unknown protocol #%d\n",transportAddress->Protocol);
         LOG_END
         return(false);
       break;
   }

   if(beginTLV(message,&tlvPosition,type) == false) {
      return(false);
   }

   switch(type) {
      case ATT_SCTP_TRANSPORT:
         stp = (struct asap_sctptransportparameter*)getSpace(message,sizeof(struct asap_sctptransportparameter));
         if(stp == NULL) {
            return(false);
         }
         stp->stp_port          = htons(transportAddress->Port);
         stp->stp_transport_use = htons(transportUse);
       break;

      case ATT_TCP_TRANSPORT:
         ttp = (struct asap_tcptransportparameter*)getSpace(message,sizeof(struct asap_tcptransportparameter));
         if(ttp == NULL) {
            return(false);
         }
         ttp->ttp_port          = htons(transportAddress->Port);
         ttp->ttp_transport_use = htons(transportUse);
       break;

      case ATT_UDP_TRANSPORT:
         utp = (struct asap_udptransportparameter*)getSpace(message,sizeof(struct asap_udptransportparameter));
         if(utp == NULL) {
            return(false);
         }
         utp->utp_port     = htons(transportAddress->Port);
         utp->utp_reserved = 0;
       break;
   }

   for(i = 0;i < transportAddress->Addresses;i++) {
      if(createAddressParameter(message,(struct sockaddr*)&transportAddress->AddressArray[i]) == false) {
         return(false);
      }
      if((i > 0) && (type != ATT_SCTP_TRANSPORT)) {
         LOG_ERROR
         fputs("Multiple addresses for non-multihomed protocol\n", stdlog);
         LOG_END
         return(false);
      }
   }


   return(finishTLV(message,tlvPosition));
}


/* ###### Create policy parameter ######################################## */
static bool createPolicyParameter(struct ASAPMessage* message,
                                  struct PoolPolicy*  policyInfo)
{
   size_t                                    tlvPosition = 0;
   struct asap_policy_roundrobin*            rr;
   struct asap_policy_weighted_roundrobin*   wrr;
   struct asap_policy_leastused*             lu;
   struct asap_policy_leastused_degradation* lud;
   struct asap_policy_random*                rd;
   struct asap_policy_weighted_random*       wrd;

   if(beginTLV(message,&tlvPosition,ATT_POOL_POLICY) == false) {
      return(false);
   }

   if(policyInfo == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   switch(policyInfo->Type) {
      case PPT_RANDOM:
          rd = (struct asap_policy_random*)getSpace(message,sizeof(struct asap_policy_random));
          if(rd == NULL) {
             return(false);
          }
          rd->pp_rd_policy = policyInfo->Type;
          rd->pp_rd_pad    = 0;
       break;
      case PPT_WEIGHTED_RANDOM:
          wrd = (struct asap_policy_weighted_random*)getSpace(message,sizeof(struct asap_policy_weighted_random));
          if(wrd == NULL) {
             return(false);
          }
          wrd->pp_wrd_policy = policyInfo->Type;
          wrd->pp_wrd_weight = hton24(policyInfo->Weight);
       break;
      case PPT_ROUNDROBIN:
          rr = (struct asap_policy_roundrobin*)getSpace(message,sizeof(struct asap_policy_roundrobin));
          if(rr == NULL) {
             return(false);
          }
          rr->pp_rr_policy = policyInfo->Type;
          rr->pp_rr_pad    = 0;
       break;
      case PPT_WEIGHTED_ROUNDROBIN:
          wrr = (struct asap_policy_weighted_roundrobin*)getSpace(message,sizeof(struct asap_policy_weighted_roundrobin));
          if(wrr == NULL) {
             return(false);
          }
          wrr->pp_wrr_policy = policyInfo->Type;
          wrr->pp_wrr_weight = hton24(policyInfo->Weight);
       break;
      case PPT_LEASTUSED:
          lu = (struct asap_policy_leastused*)getSpace(message,sizeof(struct asap_policy_leastused));
          if(lu == NULL) {
             return(false);
          }
          lu->pp_lu_policy = policyInfo->Type;
          lu->pp_lu_load   = hton24(policyInfo->Load);
       break;
      case PPT_LEASTUSED_DEGRADATION:
          lud = (struct asap_policy_leastused_degradation*)getSpace(message,sizeof(struct asap_policy_leastused_degradation));
          if(lud == NULL) {
             return(false);
          }
          lud->pp_lud_policy = policyInfo->Type;
          lud->pp_lud_load   = hton24(policyInfo->Load);
       break;
      default:
         LOG_ERROR
         fprintf(stdlog,"Unknown policy #$%02x\n",policyInfo->Type);
         LOG_END
         return(false);
       break;
   }

   return(finishTLV(message,tlvPosition));
}


/* ###### Create pool element parameter ################################## */
static bool createPoolElementParameter(struct ASAPMessage* message,
                                       struct PoolElement* poolElement)
{
   size_t                            tlvPosition = 0;
   struct TransportAddress*          transportAddress;
   struct asap_poolelementparameter* pep;
   GList*                            list;

   if(poolElement == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginTLV(message,&tlvPosition,ATT_POOL_ELEMENT) == false) {
      return(false);
   }

   pep = (struct asap_poolelementparameter*)getSpace(message,sizeof(struct asap_poolelementparameter));
   if(pep == NULL) {
      return(false);
   }

   pep->pep_identifier   = htonl(poolElement->Identifier);
   pep->pep_homeserverid = htonl(poolElement->HomeENRPServerID);
   pep->pep_reg_life     = htonl(poolElement->RegistrationLife);

   list = g_list_first(poolElement->TransportAddressList);
   while(list != NULL) {
      transportAddress = (struct TransportAddress*)list->data;
      if(createTransportParameter(message, transportAddress,
                                  (poolElement->HasControlChannel) ? UTP_DATA_PLUS_CONTROL : UTP_DATA_ONLY) == false) {
         return(false);
      }
      list = g_list_next(list);
   }

   if(createPolicyParameter(message,poolElement->Policy) == false) {
      return(false);
   }

   return(finishTLV(message,tlvPosition));
}


/* ###### Create pool element identifier parameter ####################### */
static bool createPoolElementIdentifierParameter(struct ASAPMessage*         message,
                                                 const PoolElementIdentifier poolElementIdentifier)
{
   uint32_t* identifier;
   size_t    tlvPosition;

   if(beginTLV(message,&tlvPosition,ATT_POOL_ELEMENT_IDENTIFIER) == false) {
      return(false);
   }

   identifier = (uint32_t*)getSpace(message,sizeof(uint32_t));
   if(identifier == NULL) {
      return(false);
   }
   *identifier = htonl(poolElementIdentifier);

   return(finishTLV(message,tlvPosition));
}


/* ###### Create error parameter ######################################### */
static bool createErrorParameter(struct ASAPMessage* message)
{
   struct asap_errorcause* aec;
   size_t                  tlvPosition = 0;
   uint16_t                cause;
   char*                   data;
   size_t                  dataLength;

   if(beginTLV(message,&tlvPosition,ATT_OPERATION_ERROR) == false) {
      return(false);
   }

   cause = message->Error;
   switch(cause) {
      case AEC_UNRECOGNIZED_PARAMETER:
         data       = message->OffendingParameterTLV;
         dataLength = message->OffendingParameterTLVLength;
       break;
      case AEC_UNRECOGNIZED_MESSAGE:
      case AEC_INVALID_VALUES:
         data       = message->OffendingMessageTLV;
         dataLength = message->OffendingMessageTLVLength;
       break;
      case AEC_POLICY_INCONSISTENT:
         data       = NULL;
         dataLength = 0;
       break;
      default:
         data       = NULL;
         dataLength = 0;
       break;
   }
   if(data == NULL) {
      dataLength = 0;
   }

   aec = (struct asap_errorcause*)getSpace(message,sizeof(struct asap_errorcause) + dataLength);
   if(aec == NULL) {
      return(false);
   }

   aec->aec_cause  = htons(cause);
   aec->aec_length = htons(sizeof(struct asap_errorcause) + dataLength);
   memcpy((char*)&aec->aec_data,data,dataLength);

   return(finishTLV(message,tlvPosition));
}


/* ###### Create cookie parameter ######################################### */
static bool createCookieParameter(struct ASAPMessage* message,
                                  void*               cookie,
                                  const size_t        CookieSize)
{
   void*  buffer;
   size_t tlvPosition = 0;
   if(beginTLV(message,&tlvPosition,ATT_COOKIE) == false) {
      return(false);
   }

   buffer = (struct asap_errorcause*)getSpace(message,CookieSize);
   if(buffer == NULL) {
      return(false);
   }

   memcpy(buffer,cookie,CookieSize);

   return(finishTLV(message,tlvPosition));
}


/* ###### Create endpoint keepalive message ############################## */
static bool createEndpointKeepAliveMessage(struct ASAPMessage* message)
{
   if(message->PoolHandlePtr == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_ENDPOINT_KEEP_ALIVE,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create endpoint keepalive message ############################## */
static bool createEndpointUnreachableMessage(struct ASAPMessage* message)
{
   if(message->PoolHandlePtr == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_ENDPOINT_UNREACHABLE,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   if(createPoolElementIdentifierParameter(message,message->Identifier) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create endpoint keepalive acknowledgement message ############## */
static bool createEndpointKeepAliveAckMessage(struct ASAPMessage* message)
{
   if(message->PoolHandlePtr == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_ENDPOINT_KEEP_ALIVE_ACK,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   if(createPoolElementIdentifierParameter(message,message->Identifier) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create registration message #################################### */
static bool createRegistrationMessage(struct ASAPMessage* message)
{
   if((message->PoolHandlePtr == NULL) || (message->PoolElementPtr == NULL))  {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_REGISTRATION,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   if(createPoolElementParameter(message,message->PoolElementPtr) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create registration response message ########################### */
static bool createRegistrationResponseMessage(struct ASAPMessage* message)
{
   if((message->PoolHandlePtr == NULL) || (message->PoolElementPtr == NULL))  {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_REGISTRATION_RESPONSE,
                   (message->Error != 0) ? AHF_REGISTRATION_REJECT : 0) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   if(createPoolElementIdentifierParameter(message,message->Identifier) == false) {
      return(false);
   }

   if(message->Error != 0x00) {
      if(createErrorParameter(message) == false) {
         return(false);
      }
   }

   return(finishMessage(message));
}


/* ###### Create deregistration message ################################## */
static bool createDeregistrationMessage(struct ASAPMessage* message)
{
   if(message->PoolHandlePtr == NULL)  {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_DEREGISTRATION,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   if(createPoolElementIdentifierParameter(message,message->Identifier) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create deregistration response message ######################### */
static bool createDeregistrationResponseMessage(struct ASAPMessage* message)
{
   if((message->PoolHandlePtr == NULL) || (message->PoolElementPtr == NULL))  {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_DEREGISTRATION_RESPONSE,
                   (message->Error != 0) ? AHF_DEREGISTRATION_REJECT : 0) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   if(createPoolElementIdentifierParameter(message,message->Identifier) == false) {
      return(false);
   }

   if(message->Error != 0x00) {
      if(createErrorParameter(message) == false) {
         return(false);
      }
   }

   return(finishMessage(message));
}


/* ###### Create name resolution message ################################# */
static bool createNameResolutionMessage(struct ASAPMessage* message)
{
   if(beginMessage(message,AHT_NAME_RESOLUTION,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolHandlePtr) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create name resolution response message ######################## */
static bool createNameResolutionResponseMessage(struct ASAPMessage* message)
{
   struct PoolElement* poolElement;
   GList*              list;
   size_t              oldPosition;
   size_t              elementCount;

   if(beginMessage(message,AHT_NAME_RESOLUTION_RESPONSE,0x00) == false) {
      return(false);
   }

   if(message->PoolPtr) {
      if(createPoolHandleParameter(message,message->PoolPtr->Handle) == false) {
         return(false);
      }

      if(message->PoolPtr->Policy != NULL) {
         if(createPolicyParameter(message,message->PoolPtr->Policy) == false) {
            return(false);
         }
      }

      elementCount = 0;
      if(message->ResumeList) {
         list = message->ResumeList;
         LOG_VERBOSE3
         fputs("Resuming Pool Parameter\n", stdlog);
         LOG_END
      }
      else {
         list = g_list_first(message->PoolPtr->PoolElementList);
      }
      message->ResumeList = NULL;

      while(list != NULL) {
         poolElement = (struct PoolElement*)list->data;

         oldPosition = message->Position;
         if(createPoolElementParameter(message,poolElement) == false) {
            if(elementCount > 0) {
               LOG_VERBOSE3
               fputs("Splitting Pool Parameter\n", stdlog);
               LOG_END
               message->Position   = oldPosition;
               message->ResumeList = list;
               break;
            }
            else {
               LOG_ERROR
               fputs("Pool Parameter creation failed - Splitting problem?\n", stdlog);
               LOG_END
            }
            return(false);
         }
         elementCount++;
         list = g_list_next(list);
      }
   }

   if(message->Error != 0x00) {
      if(createErrorParameter(message) == false) {
         return(false);
      }
   }

   return(finishMessage(message));
}


/* ###### Create business card message #################################### */
static bool createBusinessCardMessage(struct ASAPMessage* message)
{
   struct PoolElement* poolElement;
   GList*              list;

   if(message->PoolPtr == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_BUSINESS_CARD,0x00) == false) {
      return(false);
   }

   if(createPoolHandleParameter(message,message->PoolPtr->Handle) == false) {
      return(false);
   }

   if(createPolicyParameter(message,message->PoolPtr->Policy) == false) {
      return(false);
   }

   list = g_list_first(message->PoolPtr->PoolElementList);
   while(list != NULL) {
      poolElement = (struct PoolElement*)list->data;
      if(createPoolElementParameter(message,poolElement) == false) {
         return(false);
      }
      list = g_list_next(list);
   }

   return(finishMessage(message));
}


/* ###### Create server announce message ################################## */
static bool createServerAnnounceMessage(struct ASAPMessage* message)
{
   struct TransportAddress* transportAddress;
   GList*                   list;

   if(message->TransportAddressListPtr == NULL) {
      LOG_ERROR
      fputs("Invalid parameters\n", stdlog);
      LOG_END
      return(false);
   }

   if(beginMessage(message,AHT_SERVER_ANNOUNCE,0x00) == false) {
      return(false);
   }

   list = g_list_first(message->TransportAddressListPtr);
   while(list != NULL) {
      transportAddress = (struct TransportAddress*)list->data;
      if(createTransportParameter(message, transportAddress, false) == false) {
         return(false);
      }
      list = g_list_next(list);
   }

   return(finishMessage(message));
}


/* ###### Create cookie message ########################################### */
static bool createCookieMessage(struct ASAPMessage* message)
{
   if(beginMessage(message,AHT_COOKIE,0x00) == false) {
      return(false);
   }

   if(createCookieParameter(message,message->CookiePtr,message->CookieSize) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Create cookie echo message ###################################### */
static bool createCookieEchoMessage(struct ASAPMessage* message)
{
   if(beginMessage(message,AHT_COOKIE_ECHO,0x00) == false) {
      return(false);
   }

   if(createCookieParameter(message,message->CookiePtr,message->CookieSize) == false) {
      return(false);
   }

   return(finishMessage(message));
}


/* ###### Convert ASAPMessage to packet ################################## */
size_t asapMessage2Packet(struct ASAPMessage* message)
{
   asapMessageClearBuffer(message);

   switch(message->Type) {
      case AHT_NAME_RESOLUTION:
         LOG_ACTION
         fputs("Creating NameResolution message...\n", stdlog);
         LOG_END
         if(createNameResolutionMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_NAME_RESOLUTION_RESPONSE:
         LOG_ACTION
         fputs("Creating NameResolutionResponse message...\n", stdlog);
         LOG_END
         if(createNameResolutionResponseMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_ENDPOINT_KEEP_ALIVE:
         LOG_ACTION
         fputs("Creating EndpointKeepAlive message...\n", stdlog);
         LOG_END
         if(createEndpointKeepAliveMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_ENDPOINT_KEEP_ALIVE_ACK:
         LOG_ACTION
         fputs("Creating EndpointKeepAliveAck message...\n", stdlog);
         LOG_END
         if(createEndpointKeepAliveAckMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_SERVER_ANNOUNCE:
         LOG_ACTION
         fputs("Creating ServerAnnounce message...\n", stdlog);
         LOG_END
         if(createServerAnnounceMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_REGISTRATION:
         LOG_ACTION
         fputs("Creating Registration message...\n", stdlog);
         LOG_END
         if(createRegistrationMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_REGISTRATION_RESPONSE:
         LOG_ACTION
         fputs("Creating RegistrationResponse message...\n", stdlog);
         LOG_END
         if(createRegistrationResponseMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_DEREGISTRATION:
         LOG_ACTION
         fputs("Creating Deregistration message...\n", stdlog);
         LOG_END
         if(createDeregistrationMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_DEREGISTRATION_RESPONSE:
         LOG_ACTION
         fputs("Creating DeregistrationResponse message...\n", stdlog);
         LOG_END
         if(createDeregistrationResponseMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_COOKIE:
         LOG_ACTION
         fputs("Creating Cookie message...\n", stdlog);
         LOG_END
         if(createCookieMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_COOKIE_ECHO:
         LOG_ACTION
         fputs("Creating CookieEcho message...\n", stdlog);
         LOG_END
         if(createCookieEchoMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_BUSINESS_CARD:
         LOG_ACTION
         fputs("Creating BusinessCard message...\n", stdlog);
         LOG_END
         if(createBusinessCardMessage(message) == true) {
            return(message->Position);
         }
       break;
      case AHT_ENDPOINT_UNREACHABLE:
         LOG_ACTION
         fputs("Creating EndpointUnreachable message...\n", stdlog);
         LOG_END
         if(createEndpointUnreachableMessage(message) == true) {
            return(message->Position);
         }
       break;
   }
   LOG_ERROR
   fputs("Message creation failed\n", stdlog);
   LOG_END_FATAL
   return(0);
}
