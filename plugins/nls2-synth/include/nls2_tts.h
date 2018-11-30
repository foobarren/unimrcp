
#ifndef NLS2_TTS_H
#define NLS2_TTS_H

#include <stdint.h>
#include <string>
#include <map>
#include "mpf_buffer.h"
#include "apt_log.h"

#include "nlsClient.h"
#include "nlsEvent.h"
#include "speechSynthesizerRequest.h"
#include "nlsCommonSdk/Token.h"
using namespace AlibabaNlsCommon;

using AlibabaNls::NlsEvent;
using AlibabaNls::SpeechSynthesizerCallback;
using AlibabaNls::SpeechSynthesizerRequest;
namespace Nls2TTS
{
	class TTSSession;

	typedef int32_t	(*tpfnOnNotify)(NlsEvent* cbEvent, void* pvContext);//pvContext is nls2_synth_channel_t
	// 自定义事件回调参数
	struct ParamCallBack {
		tpfnOnNotify pfnOnNotify;
		void* pContext;//nls2_synth_channel_t*
	};

	int32_t	GlobalInit(const std::string& strFilePathConf);
	int32_t	GlobalFini();

	TTSSession*	OpenSession();
	int32_t	CloseSession(TTSSession* pSession,bool bNeedStop = true);


	class TTSSession
	{
	public:
		TTSSession();
		~TTSSession();
		int32_t	Start(const char* value,void* pContext); //pContext is ParamCallBack*
		int32_t	Stop(bool bNeedStop = true);

	private:
		SpeechSynthesizerRequest*	m_pNlsReq;
		SpeechSynthesizerCallback*	m_pNlsCB;
	};
};
#endif //end NLS2_TTS_H

