#include "Prd.hxx"

using namespace resip;

Prd::Prd(SipMessage &initialMessage)
  : mInitialMessage(initialMessage),
    mIsDead(false)

{
}

void 
Prd::makeInitialRequest(const NameAddr& target, MethodTypes method)
{
   assert(mUserProfile.get());
   makeInitialRequest(target, mUserProfile->getDefaultFrom(), method);
}

void 
Prd::makeInitialRequest(const NameAddr& target, const NameAddr& from, MethodTypes method)
{
   RequestLine rLine(method);
   rLine.uri() = target.uri();   
   mInitialRequest.header(h_RequestLine) = rLine;

   mInitialRequest.header(h_To) = target;
   mInitialRequest.header(h_MaxForwards).value() = 70;
   mInitialRequest.header(h_CSeq).method() = method;
   mInitialRequest.header(h_CSeq).sequence() = 1;
   mInitialRequest.header(h_From) = from;
   mInitialRequest.header(h_From).param(p_tag) = Helper::computeTag(Helper::tagSize);
   mInitialRequest.header(h_CallId).value() = Helper::computeCallId();

   NameAddr contact; // if no GRUU, let the stack fill in the contact 

   assert(mUserProfile.get());
   if (!mUserProfile->getImsAuthUri().host().empty())
   {
      Auth auth;
      Uri source = mUserProfile->getImsAuthUri();      
      auth.scheme() = "Digest";
      auth.param(p_username) = source.getAorNoPort();
      auth.param(p_realm) = source.host();
      source.user() = Data::Empty;
      auth.param(p_uri) = "sip:" + source.host();
      auth.param(p_nonce) = Data::Empty;
      auth.param(p_response) = Data::Empty;
      mInitialRequest.header(h_Authorizations).push_back(auth);
      DebugLog ( << "Adding auth header to inital reg for IMS: " << auth);   
   }

   if (mUserProfile->hasGruu(target.uri().getAor()))
   {
      contact = mUserProfile->getGruu(target.uri().getAor());
      mInitialRequest.header(h_Contacts).push_front(contact);
   }
   else
   {
      if (mUserProfile->hasOverrideHostAndPort())
      {
         contact.uri() = mUserProfile->getOverrideHostAndPort();
      }
      contact.uri().user() = from.uri().user();
      const Data& instanceId = mUserProfile->getInstanceId();
      if (!instanceId.empty())
      {
         contact.param(p_Instance) = instanceId;
      }
      mInitialRequest.header(h_Contacts).push_front(contact);

      if (method != REGISTER)
      {
         const NameAddrs& sRoute = mUserProfile->getServiceRoute();
         if (!sRoute.empty())
         {
            mInitialRequest.header(h_Routes) = sRoute;
         }
      }
   }
      
   Via via;
   mInitialRequest.header(h_Vias).push_front(via);

   if(mUserProfile->isAdvertisedCapability(Headers::Allow)) mInitialRequest.header(h_Allows) = mDum.getMasterProfile()->getAllowedMethods();
   if(mUserProfile->isAdvertisedCapability(Headers::AcceptEncoding)) mInitialRequest.header(h_AcceptEncodings) = mDum.getMasterProfile()->getSupportedEncodings();
   if(mUserProfile->isAdvertisedCapability(Headers::AcceptLanguage)) mInitialRequest.header(h_AcceptLanguages) = mDum.getMasterProfile()->getSupportedLanguages();
   if(mUserProfile->isAdvertisedCapability(Headers::AllowEvents)) mInitialRequest.header(h_AllowEvents) = mDum.getMasterProfile()->getAllowedEvents();
   if(mUserProfile->isAdvertisedCapability(Headers::Supported)) mInitialRequest.header(h_Supporteds) = mDum.getMasterProfile()->getSupportedOptionTags();

   // Merge Embedded parameters
   mInitialRequest.mergeUri(target.uri());

   //DumHelper::setOutgoingEncryptionLevel(mLastRequest, mEncryptionLevel);

   DebugLog ( << "Prd::makeInitialRequest: " << mLastRequest);
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
