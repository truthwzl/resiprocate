#include "resiprocate/dum/AppDialog.hxx"
#include "resiprocate/dum/AppDialogSet.hxx"
#include "resiprocate/dum/BaseCreator.hxx"
#include "resiprocate/dum/ClientAuthManager.hxx"
#include "resiprocate/dum/Dialog.hxx"
#include "resiprocate/dum/DialogSet.hxx"
#include "resiprocate/dum/DialogUsageManager.hxx"
#include "resiprocate/os/Logger.hxx"
#include "resiprocate/dum/ClientOutOfDialogReq.hxx"
#include "resiprocate/dum/ClientRegistration.hxx"
#include "resiprocate/dum/ServerOutOfDialogReq.hxx"
#include "resiprocate/dum/ServerRegistration.hxx"
#include "resiprocate/dum/ClientPublication.hxx"
#include "resiprocate/dum/ServerPublication.hxx"

#define RESIPROCATE_SUBSYSTEM Subsystem::DUM

using namespace resip;
using namespace std;

DialogSet::DialogSet(BaseCreator* creator, DialogUsageManager& dum) :
   mMergeKey(),
   mDialogs(),
   mCreator(creator),
   mId(creator->getLastRequest()),
   mDum(dum),
   mAppDialogSet(0),
   mCancelled(false),
   mDestroying(false),
   mClientRegistration(0),
   mServerRegistration(0),
   mClientPublication(0),
   mServerPublication(0),
   mClientOutOfDialogRequests(),
   mServerOutOfDialogRequest(0)
{
   assert(!creator->getLastRequest().isExternal());
   InfoLog ( << " ************* Created DialogSet(UAC)  -- " << mId << "*************" );
}

DialogSet::DialogSet(const SipMessage& request, DialogUsageManager& dum) : 
   mMergeKey(request),
   mDialogs(),
   mCreator(NULL),
   mId(request),
   mDum(dum),
   mAppDialogSet(0),
   mCancelled(false),
   mDestroying(false),
   mClientRegistration(0),
   mServerRegistration(0),
   mClientPublication(0),
   mServerPublication(0),
   mClientOutOfDialogRequests(),
   mServerOutOfDialogRequest(0)
{
   assert(request.isRequest());
   assert(request.isExternal());
   mDum.mMergedRequests.insert(mMergeKey);
   InfoLog ( << " ************* Created DialogSet(UAS)  -- " << mId << "*************" );
}

DialogSet::~DialogSet()
{
   mDestroying = true;
   if (mMergeKey != MergedRequestKey::Empty)
   {
      mDum.mMergedRequests.erase(mMergeKey);
   }

   delete mCreator;
   while(!mDialogs.empty())
   {
      delete mDialogs.begin()->second;
   } 

   delete mClientRegistration;
   delete mServerRegistration;
   delete mClientPublication;
   delete mServerPublication;
   delete mServerOutOfDialogRequest;

   while (!mClientOutOfDialogRequests.empty())
   {
      delete *mClientOutOfDialogRequests.begin();
   }

   InfoLog ( << " ********** DialogSet::~DialogSet: " << mId << "*************" );
   //!dcm! -- very delicate code, change the order things go horribly wrong

   delete mAppDialogSet;
   mDum.removeDialogSet(this->getId());
}

void DialogSet::possiblyDie()
{
   if (!mDestroying)
   {
      if(mDialogs.empty() && 
         mClientOutOfDialogRequests.empty() &&
         !(mClientPublication ||
           mServerPublication ||
           mServerOutOfDialogRequest ||
           mClientRegistration ||
           mServerRegistration))
      {
         delete this;
      }   
   }
}

DialogSetId
DialogSet::getId()
{
   return mId;
}

void
DialogSet::addDialog(Dialog *dialog)
{
   mDialogs[dialog->getId()] = dialog;
}

BaseCreator* 
DialogSet::getCreator() 
{
   return mCreator;
}

Dialog* 
DialogSet::findDialog(const SipMessage& msg)
{
   DialogId id(msg);
   return findDialog(id);
}

bool
DialogSet::empty() const
{
   return mDialogs.empty();
}

void
DialogSet::dispatch(const SipMessage& msg)
{
   assert(msg.isRequest() || msg.isResponse());

   if (msg.isResponse() && mDum.mClientAuthManager && !mCancelled)
   {
      //!dcm! -- multiple usage grief...only one of each method type allowed
      if (getCreator() &&
          msg.header(h_CSeq).method() == getCreator()->getLastRequest().header(h_RequestLine).method())
      {
         if (mDum.mClientAuthManager->handle( getCreator()->getLastRequest(), msg))
         {
            InfoLog( << "about to retransmit request with digest credentials" );
            InfoLog( << getCreator()->getLastRequest() );
            
            mDum.send(getCreator()->getLastRequest());
            return;                     
         }                  
      }
   }

   if (msg.isRequest())
   {
      const SipMessage& request = msg;
      switch (request.header(h_CSeq).method())
      {
         case INVITE:
         case BYE:
         case ACK:
         case CANCEL:
         case SUBSCRIBE:
         case REFER: //need to add out-of-dialog refer logic
            break; //dialog creating/handled by dialog
         case NOTIFY:
            if (request.header(h_To).exists(p_tag))
            {
               break; //dialog creating/handled by dialog
            }
            else // no to tag - unsolicited notify
            {
               assert(mServerOutOfDialogRequest == 0);
               mServerOutOfDialogRequest = makeServerOutOfDialog(request);
               mServerOutOfDialogRequest->dispatch(request);
            }
            break;                              
         case PUBLISH:
            if (mServerPublication == 0)
            {
               mServerPublication = makeServerPublication(request);
            }
            mServerPublication->dispatch(request);
            return;         
         case REGISTER:
            if (mServerRegistration == 0)
            {
               mServerRegistration = makeServerRegistration(request);
            }
            mServerRegistration->dispatch(request);
            return;
         default: 
            InfoLog ( << "In DialogSet::dispatch, default(ServerOutOfDialogRequest), msg: " << msg );            
            // only can be one ServerOutOfDialogReq at a time
            assert(mServerOutOfDialogRequest == 0);
            mServerOutOfDialogRequest = makeServerOutOfDialog(request);
            mServerOutOfDialogRequest->dispatch(request);
            break;
      }
   }
   else
   {
      const SipMessage& response = msg;
      switch (response.header(h_CSeq).method())
      {
         case INVITE:
         case BYE:
         case ACK:
         case CANCEL:
         case SUBSCRIBE:
         case REFER:  //need to add out-of-dialog refer logic
            break; //dialog creating/handled by dialog
         case PUBLISH:
            if (mClientPublication == 0)
            {
               mClientPublication = makeClientPublication(response);
            }
            mClientPublication->dispatch(response);
            return;
         case REGISTER:
            if (mClientRegistration == 0)
            {
               mClientRegistration = makeClientRegistration(response);
            }
            mClientRegistration->dispatch(response);
            return;
            // unsolicited - not allowed but commonly implemented
            // by large companies with a bridge as their logo
         case NOTIFY: 
         case INFO:   
         default:
         {
            ClientOutOfDialogReq* req = findMatchingClientOutOfDialogReq(response);
            if (req == 0)
            {
               req = makeClientOutOfDialogReq(response);
               mClientOutOfDialogRequests.push_back(req);
            }
            req->dispatch(response);
            return;
         }
      }
   }

   Dialog* dialog = findDialog(msg);
   if (dialog == 0)
   {
      if (msg.isRequest() && msg.header(h_RequestLine).method() == CANCEL)
      {
         for(DialogMap::iterator it = mDialogs.begin(); it != mDialogs.end(); it++)
         {
            it->second->dispatch(msg);
         }
         return;         
      }

      InfoLog ( << "Creating a new Dialog from msg: " << msg);
      // !jf! This could throw due to bad header in msg, should we catch and rethrow
      // !jf! if this threw, should we check to delete the DialogSet? 
      dialog = new Dialog(mDum, msg, *this);

      if (mCancelled)
      {
         dialog->cancel();
         dialog = findDialog(msg);
      }
      else
      {
         InfoLog ( << "### Calling CreateAppDialog ### " << msg);
         AppDialog* appDialog = mAppDialogSet->createAppDialog(msg);
         dialog->mAppDialog = appDialog;
         appDialog->mDialog = dialog;         
      }
   }     
   if (dialog)
   {     
      dialog->dispatch(msg);
   }
   else if (msg.isRequest())
   {
      SipMessage response;
      mDum.makeResponse(response, msg, 481);
      mDum.send(response);
   }
}

ClientOutOfDialogReq*
DialogSet::findMatchingClientOutOfDialogReq(const SipMessage& msg)
{
   for (std::list<ClientOutOfDialogReq*>::iterator i=mClientOutOfDialogRequests.begin(); 
        i != mClientOutOfDialogRequests.end(); ++i)
   {
      if ((*i)->matches(msg))
      {
         return *i;
      }
   }
   return 0;
}

Dialog* 
DialogSet::findDialog(const DialogId id)
{
   DialogMap::iterator i = mDialogs.find(id);
   if (i == mDialogs.end())
   {
      return 0;
   }
   else
   {
      return i->second;
   }
}

void
DialogSet::cancel()
{
   mCancelled = true;
   for (DialogMap::iterator i = mDialogs.begin(); i != mDialogs.end(); ++i)
   {
      i->second->cancel();
   }
}


ClientRegistrationHandle 
DialogSet::getClientRegistration()
{
   if (mClientRegistration)
   {
      return mClientRegistration->getHandle();
   }
   else
   {
      return ClientRegistrationHandle::NotValid();
   }
}

ServerRegistrationHandle 
DialogSet::getServerRegistration()
{
   if (mServerRegistration)
   {
      return mServerRegistration->getHandle();
   }
   else
   {
      return ServerRegistrationHandle::NotValid();
   }
}

ClientPublicationHandle 
DialogSet::getClientPublication()
{
   if (mClientPublication)
   {
      return mClientPublication->getHandle();
   }
   else
   {
      return ClientPublicationHandle::NotValid();      
   }
}

ServerPublicationHandle 
DialogSet::getServerPublication()
{
   if (mServerPublication)
   {
      return mServerPublication->getHandle();
   }
   else
   {
      return ServerPublicationHandle::NotValid();      
   }
}


ClientRegistration*
DialogSet::makeClientRegistration(const SipMessage& response)
{
   BaseCreator* creator = getCreator();
   assert(creator);
   return new ClientRegistration(mDum, *this, creator->getLastRequest());
}

ClientPublication*
DialogSet::makeClientPublication(const SipMessage& response)
{
   BaseCreator* creator = getCreator();
   assert(creator);
   return new ClientPublication(mDum, *this, creator->getLastRequest());
}

ClientOutOfDialogReq*
DialogSet::makeClientOutOfDialogReq(const SipMessage& response)
{
   BaseCreator* creator = getCreator();
   assert(creator);
   return new ClientOutOfDialogReq(mDum, *this, creator->getLastRequest());
}

ServerRegistration* 
DialogSet::makeServerRegistration(const SipMessage& request)
{
   return new ServerRegistration(mDum, *this, request);
}

ServerPublication* 
DialogSet::makeServerPublication(const SipMessage& request)
{
   return new ServerPublication(mDum, *this, request);
}

ServerOutOfDialogReq* 
DialogSet::makeServerOutOfDialog(const SipMessage& request)
{
   return new ServerOutOfDialogReq(mDum, *this, request);
}


#if 0
ClientOutOfDialogReqHandle 
DialogSet::findClientOutOfDialog()
{
   if (mClientOutOfDialogRequests)
   {
      return mClientOutOfDialogReq->getHandle();
   }
   else
   {
      throw BaseUsage::Exception("no such client out of dialog",
                                 __FILE__, __LINE__);
   }
}
#endif

ServerOutOfDialogReqHandle
DialogSet::getServerOutOfDialog()
{
   if (mServerOutOfDialogRequest)
   {
      return mServerOutOfDialogRequest->getHandle();
   }
   else
   {
      return ServerOutOfDialogReqHandle::NotValid();
   }
}


/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */
