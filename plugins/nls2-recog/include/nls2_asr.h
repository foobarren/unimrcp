
#ifndef NLS2_ASR_H
#define NLS2_ASR_H

#include <stdint.h>
#include <string>
#include <map>
#include "mpf_buffer.h"
#include "apt_log.h"

#include "nlsClient.h"
#include "nlsEvent.h"
#include "speechRecognizerRequest.h"
#include "speechTranscriberRequest.h"
#include "nlsCommonSdk/Token.h"

using std::string;
using std::map;

using namespace AlibabaNlsCommon;

using AlibabaNls::NlsEvent;

using AlibabaNls::SpeechRecognizerCallback;
using AlibabaNls::SpeechRecognizerRequest;

using AlibabaNls::SpeechTranscriberCallback;
using AlibabaNls::SpeechTranscriberRequest;


namespace Nls2ASR
{
	class ASRSession
	{
	public:
		virtual ~ASRSession(){};
		virtual int32_t	Start(void* pContext) =0; //pContext is ParamCallBack*
		virtual int32_t	Stop(bool bNeedStop = true) =0;
 
		virtual int32_t	FeedAudioData(const void* pvAudioData, uint32_t lenAudioData)=0 ;
	};
	//ASR事件类型
	// enum ET_AsrEventType
	// {
	// 	E_TranscriptionStarted		=	0,//调用start(), 成功与云端建立连接, sdk内部线程上报started事件
	// 	E_SentenceBegin		=	1,	// 服务端检测到了一句话的开始, sdk内部线程上报SentenceBegin事件
	// 	E_SentenceEnd	=	2,	// 服务端检测到了一句话结束, sdk内部线程上报SentenceEnd事件
	// 	E_TranscriptionResultChanged	=	3,	// 识别结果发生了变化, sdk在接收到云端返回到最新结果时, sdk内部线程上报ResultChanged事件
	// 	E_TranscriptionCompleted	=	4,	// 服务端停止实时音频流识别时, sdk内部线程上报Completed事件
	// 	E_TaskFailed	=	5,	// 识别过程(包含start(), send(), stop())发生异常时, sdk内部线程上报TaskFailed事件
	// 	E_ChannelClosed	=	6,	// 识别结束或发生异常时，会关闭连接通道, sdk内部线程上报ChannelCloseed事件
	// };

	typedef int32_t	(*tpfnOnNotify)(NlsEvent* cbEvent, void* pvContext);//pvContext is nls2_recog_channel_t
	// 自定义事件回调参数
	struct ParamCallBack {
		tpfnOnNotify pfnOnNotify;
		void* pContext;//nls2_recog_channel_t*
	};

	int32_t	GlobalInit(const std::string& strFilePathConf);
	int32_t	GlobalFini();

	ASRSession*	OpenASRSession(int type = 0);
	int32_t	CloseASRSession(ASRSession* pSession,bool bNeedStop = true);

	class SpeechRecognizerSession: public ASRSession
	{
	public:
		SpeechRecognizerSession();
		~SpeechRecognizerSession();
		int32_t	Start(void* pContext); //pContext is ParamCallBack*
		int32_t	Stop(bool bNeedStop = true);

		int32_t	FeedAudioData(const void* pvAudioData, uint32_t lenAudioData);

	private:
		SpeechRecognizerRequest*	m_pNlsReq;
		SpeechRecognizerCallback*	m_pNlsCB;
	};

	class SpeechTranscriberSession: public ASRSession
	{
	public:
		SpeechTranscriberSession();
		~SpeechTranscriberSession();
		int32_t	Start(void* pContext); //pContext is ParamCallBack*
		int32_t	Stop(bool bNeedStop = true);

		int32_t	FeedAudioData(const void* pvAudioData, uint32_t lenAudioData);

	private:
		SpeechTranscriberRequest*	m_pNlsReq;
		SpeechTranscriberCallback*	m_pNlsCB;
	};

};




#endif //end NLS2_ASR_H

