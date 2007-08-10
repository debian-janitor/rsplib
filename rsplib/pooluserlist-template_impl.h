/* $Id$
 * --------------------------------------------------------------------------
 *
 *              //===//   //=====   //===//   //=====  //   //      //
 *             //    //  //        //    //  //       //   //=/  /=//
 *            //===//   //=====   //===//   //====   //   //  //  //
 *           //   \\         //  //             //  //   //  //  //
 *          //     \\  =====//  //        =====//  //   //      //  Version V
 *
 * ------------- An Open Source RSerPool Simulation for OMNeT++ -------------
 *
 * Copyright (C) 2003-2007 by Thomas Dreibholz
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact: dreibh@iem.uni-due.de
 */

/* ###### Initialize ##################################################### */
void ST_CLASS(poolUserListNew)(struct ST_CLASS(PoolUserList)* poolUserList)
{
   ST_METHOD(New)(&poolUserList->PoolUserListStorage,
                  ST_CLASS(poolUserStorageNodePrint),
                  ST_CLASS(poolUserStorageNodeComparison));
}


/* ###### Invalidate ##################################################### */
void ST_CLASS(poolUserListDelete)(struct ST_CLASS(PoolUserList)* poolUserList)
{
   ST_METHOD(Delete)(&poolUserList->PoolUserListStorage);
}


/* ###### Get number of pool elements #################################### */
size_t ST_CLASS(poolUserListGetPoolUserNodes)(
          const struct ST_CLASS(PoolUserList)* poolUserList)
{
   return(ST_METHOD(GetElements)(&poolUserList->PoolUserListStorage));
}


/* ###### Get first PoolUserNode ######################################### */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListGetFirstPoolUserNode)(
                                      struct ST_CLASS(PoolUserList)* poolUserList)
{
   struct STN_CLASSNAME* node = ST_METHOD(GetFirst)(&poolUserList->PoolUserListStorage);
   if(node) {
      return(ST_CLASS(getPoolUserNodeFromPoolUserListStorageNode)(node));
   }
   return(NULL);
}


/* ###### Get last PoolUserNode ########################################## */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListGetLastPoolUserNode)(
                                      struct ST_CLASS(PoolUserList)* poolUserList)
{
   struct STN_CLASSNAME* node = ST_METHOD(GetLast)(&poolUserList->PoolUserListStorage);
   if(node) {
      return(ST_CLASS(getPoolUserNodeFromPoolUserListStorageNode)(node));
   }
   return(NULL);
}


/* ###### Get next PoolUserNode ########################################## */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListGetNextPoolUserNode)(
                                  struct ST_CLASS(PoolUserList)* poolUserList,
                                  struct ST_CLASS(PoolUserNode)* poolUserNode)
{
   struct STN_CLASSNAME* node = ST_METHOD(GetNext)(&poolUserList->PoolUserListStorage,
                                                   &poolUserNode->PoolUserListStorageNode);
   if(node) {
      return(ST_CLASS(getPoolUserNodeFromPoolUserListStorageNode)(node));
   }
   return(NULL);
}


/* ###### Get previous PoolUserNode ###################################### */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListGetPrevPoolUserNode)(
                                  struct ST_CLASS(PoolUserList)* poolUserList,
                                  struct ST_CLASS(PoolUserNode)* poolUserNode)
{
   struct STN_CLASSNAME* node = ST_METHOD(GetPrev)(&poolUserList->PoolUserListStorage,
                                                   &poolUserNode->PoolUserListStorageNode);
   if(node) {
      return(ST_CLASS(getPoolUserNodeFromPoolUserListStorageNode)(node));
   }
   return(NULL);
}


/* ###### Get textual description ######################################## */
void ST_CLASS(poolUserListGetDescription)(struct ST_CLASS(PoolUserList)* poolUserList,
                                          char*                          buffer,
                                          const size_t                   bufferSize)
{
   snprintf(buffer, bufferSize, "PoolUserList (%u poolUsers)",
            (unsigned int)ST_CLASS(poolUserListGetPoolUserNodes)(poolUserList));
}


/* ###### Verify ######################################################### */
void ST_CLASS(poolUserListVerify)(struct ST_CLASS(PoolUserList)* poolUserList)
{
   ST_METHOD(Verify)(&poolUserList->PoolUserListStorage);
}


/* ###### Print ########################################################## */
void ST_CLASS(poolUserListPrint)(struct ST_CLASS(PoolUserList)* poolUserList,
                                 FILE*                          fd,
                                 const unsigned int             fields)
{
   struct ST_CLASS(PoolUserNode)* poolUserNode;
   char                           poolUserListDescription[128];
   unsigned int                   i;

   ST_CLASS(poolUserListGetDescription)(poolUserList,
                                        (char*)&poolUserListDescription,
                                        sizeof(poolUserListDescription));
   fputs(poolUserListDescription, fd);
   fputs("\n", fd);

   fputs(" +-- Pool Users:\n", fd);
   i = 1;
   poolUserNode = ST_CLASS(poolUserListGetFirstPoolUserNode)(poolUserList);
   while(poolUserNode != NULL) {
      fprintf(fd, "   - idx:#%04u: ", i);
      ST_CLASS(poolUserNodePrint)(poolUserNode, fd, fields);
      fputs("\n", fd);
      i++;
      poolUserNode = ST_CLASS(poolUserListGetNextPoolUserNode)(poolUserList, poolUserNode);
   }
}


/* ###### Clear ########################################################## */
void ST_CLASS(poolUserListClear)(struct ST_CLASS(PoolUserList)* poolUserList,
                                 void                           (*poolUserNodeDisposer)(void* poolUserNode, void* userData),
                                 void*                          userData)
{
   struct ST_CLASS(PoolUserNode)* poolUserNode = ST_CLASS(poolUserListGetFirstPoolUserNode)(poolUserList);
   while(poolUserNode != NULL) {
      ST_CLASS(poolUserListRemovePoolUserNode)(poolUserList, poolUserNode);
      ST_CLASS(poolUserNodeDelete)(poolUserNode);
      poolUserNodeDisposer(poolUserNode, userData);
      poolUserNode = ST_CLASS(poolUserListGetFirstPoolUserNode)(poolUserList);
   }
}


/* ###### Add PoolUserNode ############################################### */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListAddPoolUserNode)(
                                  struct ST_CLASS(PoolUserList)* poolUserList,
                                  struct ST_CLASS(PoolUserNode)* poolUserNode,
                                  unsigned int*                  errorCode)
{
   struct STN_CLASSNAME* result;

   /* Allow random selection. It might be useful to add priorities ... */
   poolUserNode->PoolUserListStorageNode.Value = 1;

   result = ST_METHOD(Insert)(&poolUserList->PoolUserListStorage,
                              &poolUserNode->PoolUserListStorageNode);
   if(result == &poolUserNode->PoolUserListStorageNode) {
      *errorCode = RSPERR_OKAY;
      return(poolUserNode);
   }
   *errorCode = RSPERR_DUPLICATE_ID;
   return(ST_CLASS(getPoolUserNodeFromPoolUserListStorageNode)(result));
}


/* ###### Find PoolUserNode ############################################## */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListFindPoolUserNode)(
                                  struct ST_CLASS(PoolUserList)* poolUserList,
                                  const int                      connectionSocketDescriptor,
                                  const sctp_assoc_t             connectionAssocID)
{
   struct ST_CLASS(PoolUserNode)  cmpElement;
   struct STN_CLASSNAME*          result;

   cmpElement.ConnectionSocketDescriptor = connectionSocketDescriptor;
   cmpElement.ConnectionAssocID          = connectionAssocID;
   result = ST_METHOD(Find)(&poolUserList->PoolUserListStorage,
                              &cmpElement.PoolUserListStorageNode);
   if(result) {
      return(ST_CLASS(getPoolUserNodeFromPoolUserListStorageNode)(result));
   }
   return(NULL);
}


/* ###### Remove PoolUserNode ############################################ */
struct ST_CLASS(PoolUserNode)* ST_CLASS(poolUserListRemovePoolUserNode)(
                                  struct ST_CLASS(PoolUserList)* poolUserList,
                                  struct ST_CLASS(PoolUserNode)* poolUserNode)
{
   struct STN_CLASSNAME* result;

   result = ST_METHOD(Remove)(&poolUserList->PoolUserListStorage,
                              &poolUserNode->PoolUserListStorageNode);
   CHECK(result == &poolUserNode->PoolUserListStorageNode);

   return(poolUserNode);
}