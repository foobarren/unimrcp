/*
 * Copyright 2008 Arsen Chaloyan
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __RECOG_SESSION_H__
#define __RECOG_SESSION_H__

/**
 * @file recogsession.h
 * @brief Recognizer Session
 */ 

#include "umcsession.h"

class RecogScenario;
struct RecogChannel;

class RecogSession : public UmcSession
{
public:
/* ============================ CREATORS =================================== */
	RecogSession(const RecogScenario* pScenario);
	virtual ~RecogSession();

/* ============================ MANIPULATORS =============================== */
	virtual bool Run(const char* pProfileName);

/* ============================ HANDLERS =================================== */
	virtual bool OnSessionTerminate(mrcp_sig_status_code_e status);
	virtual bool OnChannelAdd(mrcp_channel_t* channel, mrcp_sig_status_code_e status);
	virtual bool OnChannelRemove(mrcp_channel_t* channel, mrcp_sig_status_code_e status);
	virtual bool OnMessageReceive(mrcp_channel_t* channel, mrcp_message_t* message);

/* ============================ ACCESSORS ================================== */
	const RecogScenario* GetScenario() const;

protected:
/* ============================ MANIPULATORS =============================== */
	RecogChannel* CreateRecogChannel();

	bool OnDefineGrammar(mrcp_channel_t* pMrcpChannel);
	
private:
/* ============================ DATA ======================================= */
	RecogChannel* m_pRecogChannel;
};


/* ============================ INLINE METHODS ============================= */
inline const RecogScenario* RecogSession::GetScenario() const
{
	return (RecogScenario*)m_pScenario;
}

#endif /*__RECOG_SESSION_H__*/