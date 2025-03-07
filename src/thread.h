/* --------------------------------------------------------------------------
 *
 *              //===//   //=====   //===//   //       //   //===//
 *             //    //  //        //    //  //       //   //    //
 *            //===//   //=====   //===//   //       //   //===<<
 *           //   \\         //  //        //       //   //    //
 *          //     \\  =====//  //        //=====  //   //===//   Version III
 *
 * ------------- An Efficient RSerPool Prototype Implementation -------------
 *
 * Copyright (C) 2002-2022 by Thomas Dreibholz
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

#ifndef THREAD_H
#define THREAD_H

#include <pthread.h>

#include "mutex.h"


class TDThread : public TDMutex
{
   public:
   TDThread();
   virtual ~TDThread();

   inline bool isRunning() const {
      return(MyThread != 0);
   }
   inline bool isStopping() {
      lock();
      const bool stopping = Stopping;
      unlock();
      return(stopping);
   }

   virtual bool start();
   virtual void stop();
   void waitForFinish();

   static void delay(const unsigned int us);

   protected:
   virtual void run() = 0;

   protected:
   pthread_t MyThread;

   private:      
   static void* startRoutine(void* object);   

   bool         Stopping;
};

#endif
