#include <ctime>
#include <iostream>
#include "nls2_tts.h"

#include "tinyxml2.h"
using namespace tinyxml2;
using AlibabaNls::NlsClient;
using std::string;
using std::cout;
using std::endl;
extern apt_log_source_t *SYNTH_PLUGIN;

/** Use custom log source mark */
#define SYNTH_LOG_MARK   APT_LOG_MARK_DECLARE(SYNTH_PLUGIN)

namespace Nls2TTS
{
	NlsClient*	g_pNlsClient	=	NULL;

	std::string	g_strAppKey;
	std::string	g_strDftAccessKeyId;
	std::string	g_strDftAccessKeySecret;
	int	g_iSampleRate = 8000;
	std::string	g_strFormat = "pcm";
	std::string g_strToken = "";
	long g_lExireTime = -1;
	int g_iSpeechRate = 0;
	int g_iVolume = 50;
	int g_iMethod = 0;
	int g_iPitchRate = 0;
	int g_iTimeout = 0;
	std::string g_strVoice = "xiaoyun";

	int32_t	GlobalInit(const std::string& strFilePathConf)
	{
		GlobalFini();

		int32_t	nRet	=	-1;
		for (int32_t iOnce=0; iOnce<1; ++iOnce)
		{
			XMLDocument	xmlDoc;
			if (xmlDoc.LoadFile(strFilePathConf.c_str()) != XML_SUCCESS)
			{
				nRet	=	-1;
				break;
			}

			XMLElement*	pRootNode	=	xmlDoc.RootElement();
			if (pRootNode == NULL)
			{
				nRet	=	-1;
				break;
			}

			XMLElement*	pNodeAuthInfo		=	pRootNode->FirstChildElement("AuthInfo");
			XMLElement*	pNodeParams			=	pRootNode->FirstChildElement("Params");

			if ( (pNodeAuthInfo == NULL) || (pNodeParams == NULL))
			{
				nRet	=	-1;
				break;
			}

			XMLElement*	pNodeAccessKeyId	=	pNodeAuthInfo->FirstChildElement("AccessKeyId");
			XMLElement*	pNodeAccessKeySecret=	pNodeAuthInfo->FirstChildElement("AccessKeySecret");
			if ((pNodeAccessKeyId == NULL) || (pNodeAccessKeySecret == NULL))
			{
				nRet	=	-1;
				break;
			}
			const char*	pstrAccessKeyId		=	pNodeAccessKeyId->GetText();
			const char*	pstrAccessKeySecret	=	pNodeAccessKeySecret->GetText();
			g_strDftAccessKeyId.assign((pstrAccessKeyId != NULL) ? pstrAccessKeyId : "");
			g_strDftAccessKeySecret.assign((pstrAccessKeySecret != NULL) ? pstrAccessKeySecret : "");
			if (g_strDftAccessKeyId.empty() || g_strDftAccessKeySecret.empty())
			{
				nRet	=	-1;
				break;
			}

			XMLElement*	pNodeAppKey	=	pNodeParams->FirstChildElement("AppKey");
			XMLElement*	pNodeSampleRate	=	pNodeParams->FirstChildElement("TtsSampleRate");
			XMLElement*	pNodeFormat	=	pNodeParams->FirstChildElement("TtsFormat");
			XMLElement*	pNodeTtsSpeechRate	=	pNodeParams->FirstChildElement("TtsSpeechRate");
			XMLElement*	pNodeTtsVolume	=	pNodeParams->FirstChildElement("TtsVolume");
			XMLElement*	pNodeTtsMethod	=	pNodeParams->FirstChildElement("TtsMethod");
			XMLElement*	pNodeTtsVoice	=	pNodeParams->FirstChildElement("TtsVoice");
			XMLElement*	pNodeTtsPitchRate	=	pNodeParams->FirstChildElement("TtsPitchRate");
			XMLElement*	pNodeTtsTimeout	=	pNodeParams->FirstChildElement("TtsTimeout");

			if ((pNodeAppKey == NULL) || (pNodeSampleRate == NULL) || (pNodeFormat == NULL) || (pNodeTtsSpeechRate == NULL))
			{
				nRet	=	-1;
				break;
			}
			if ((pNodeTtsVolume == NULL) || (pNodeTtsMethod == NULL) || (pNodeTtsVoice == NULL) || (pNodeTtsPitchRate == NULL))
			{
				nRet	=	-1;
				break;
			}
			g_iSampleRate	=	pNodeSampleRate->IntText(8000);		
			g_iSpeechRate	=	pNodeTtsSpeechRate->IntText(0);
			g_iVolume	=	pNodeTtsVolume->IntText(50);
			g_iMethod	=	pNodeTtsMethod->IntText(0);
			g_iPitchRate	=	pNodeTtsPitchRate->IntText(0);
			g_iTimeout	=	pNodeTtsTimeout->IntText(0);

			const char*	pstrAppKey	=	pNodeAppKey->GetText();
			const char*	pstrFormat	=	pNodeFormat->GetText();
			const char*	pstrVoice	=	pNodeTtsVoice->GetText();
			

			g_strAppKey.assign((pstrAppKey != NULL) ? pstrAppKey : "");
			g_strFormat.assign((pstrFormat != NULL) ? pstrFormat : "pcm");
			g_strVoice.assign((pstrVoice != NULL) ? pstrVoice : "pcm");
			if (g_strAppKey.empty() || g_strFormat.empty())
			{
				nRet	=	-1;
				break;
			}

			g_pNlsClient	=	NlsClient::getInstance();
			if (g_pNlsClient == NULL)
			{
				nRet	=	-1;
				break;
			}

			nRet	=	0;
		}
		if (nRet != 0)
		{
			GlobalFini();
		}

		return nRet;
	}

	int32_t	GlobalFini()
	{
		if (g_pNlsClient != NULL)
		{
			g_pNlsClient	=	NULL;
		}
		g_strDftAccessKeyId.clear();
		g_strDftAccessKeySecret.clear();

		return 0;
	}

	TTSSession*	OpenSession()
	{
		TTSSession*	pSession	=	NULL;

		int32_t	nRet	=	-1;
		for (int32_t iOnce=0; iOnce<1; ++iOnce)
		{
			pSession	=	new TTSSession();
			if (pSession == NULL)
			{
				nRet	=	-1;
				break;
			}

			nRet	=	0;
		}
		if (nRet != 0)
		{
			CloseSession(pSession,false);
			pSession	=	NULL;
		}

		return pSession;
	}

	int32_t	CloseSession(TTSSession* pSession,bool bNeedStop)
	{
		if (pSession != NULL)
		{
			pSession->Stop(bNeedStop);
			delete pSession;
			pSession	=	NULL;
		}

		return 0;
	}

	/**
	 * 根据AccessKey ID和AccessKey Secret重新生成一个token，并获取其有效期时间戳
	 */
	int generateToken(string akId, string akSecret, string* token, long* expireTime) {
		NlsToken nlsTokenRequest;
		nlsTokenRequest.setAccessKeyId(akId);
		nlsTokenRequest.setKeySecret(akSecret);

		if (-1 == nlsTokenRequest.applyNlsToken()) {
			// cout << "Failed: " << nlsTokenRequest.getErrorMsg() << endl; /*获取失败原因*/
			return -1;
		}

		*token = nlsTokenRequest.getToken();
		*expireTime = nlsTokenRequest.getExpireTime();

		return 0;
	}


	/**
		* @brief 调用start(), 发送text至云端, sdk内部线程上报started事件
		* @note 不允许在回调函数内部调用stop(), releaseRecognizerRequest()对象操作, 否则会异常
		* @param cbEvent 回调事件结构, 详见nlsEvent.h
		* @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
		* @return
	*/
	void OnSynthesisStarted(NlsEvent* cbEvent, void* cbParam) {

		cout << "OnSynthesisStarted: "
			<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
			<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
			<< endl;
		//cout << "OnSynthesisStarted: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
		ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
		pSession->pfnOnNotify(cbEvent,pSession->pContext);

	}

	/**
		* @brief sdk在接收到云端返回合成结束消息时, sdk内部线程上报Completed事件
		* @note 上报Completed事件之后，SDK内部会关闭识别连接通道.
		* 		不允许在回调函数内部调用stop(), releaseRecognizerRequest()对象操作, 否则会异常
		* @param cbEvent 回调事件结构, 详见nlsEvent.h
		* @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
		* @return
	*/
	void OnSynthesisCompleted(NlsEvent* cbEvent, void* cbParam) {

		cout << "OnSynthesisCompleted: "
			<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
			<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
			<< endl;
		// cout << "OnSynthesisCompleted: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
		ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
		pSession->pfnOnNotify(cbEvent,pSession->pContext);

	}

	/**
		* @brief 合成过程发生异常时, sdk内部线程上报TaskFailed事件
		* @note 上报TaskFailed事件之后，SDK内部会关闭识别连接通道.
		* 		不允许在回调函数内部调用stop(), releaseRecognizerRequest()对象操作, 否则会异常
		* @param cbEvent 回调事件结构, 详见nlsEvent.h
		* @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
		* @return
	*/
	void OnSynthesisTaskFailed(NlsEvent* cbEvent, void* cbParam) {

		cout << "OnSynthesisTaskFailed: "
			<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
			<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
			<< ", error message: " << cbEvent->getErrorMessage()
			<< endl;
		// cout << "OnSynthesisTaskFailed: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息

		ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
		pSession->pfnOnNotify(cbEvent,pSession->pContext);

	}

	/**
		* @brief 识别结束或发生异常时，会关闭连接通道, sdk内部线程上报ChannelCloseed事件
		* @note 不允许在回调函数内部调用stop(), releaseRecognizerRequest()对象操作, 否则会异常
		* @param cbEvent 回调事件结构, 详见nlsEvent.h
		* @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
		* @return
	*/
	void OnSynthesisChannelClosed(NlsEvent* cbEvent, void* cbParam) {
		cout << "OnRecognitionChannelCloseed: All response: " << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息

		ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
		pSession->pfnOnNotify(cbEvent,pSession->pContext);
	}

	/**
		* @brief 文本上报服务端之后, 收到服务端返回的二进制音频数据, SDK内部线程通过BinaryDataRecved事件上报给用户
		* @note 不允许在回调函数内部调用stop(), releaseRecognizerRequest()对象操作, 否则会异常
		* @param cbEvent 回调事件结构, 详见nlsEvent.h
		* @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
		* @return
	*/
	void OnBinaryDataRecved(NlsEvent* cbEvent, void* cbParam) {
		vector<unsigned char> data = cbEvent->getBinaryData(); // getBinaryData() 获取文本合成的二进制音频数据

		cout << "OnBinaryDataRecved: "
			<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
			<< ", taskId: " << cbEvent->getTaskId()        // 当前任务的task id，方便定位问题，建议输出
			<< ", data size: " << data.size()              // 数据的大小
			<< endl;

		// // 以追加形式将二进制音频数据写入文件
		// if (data.size() > 0) {
		// 	tmpParam->audioFile.write((char*)&data[0], data.size());
		// }
		ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
		pSession->pfnOnNotify(cbEvent,pSession->pContext);
	}


	TTSSession::TTSSession()
	:m_pNlsReq(NULL), m_pNlsCB(NULL)
	{
	}

	TTSSession::~TTSSession()
	{
	}


	int32_t	TTSSession::Start(const char* value,void* pContext)
	{
		int32_t	nRet	=	-1;

		for (int32_t iOnce=0; iOnce<1; ++iOnce)
		{
			/**
			 * 获取当前系统时间戳，判断token是否过期
			 */
			std::time_t curTime = std::time(0);
			if (g_lExireTime - curTime < 10) {
				apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
				"the token will be expired, please generate new token by AccessKey-ID and AccessKey-Secret." 
				);
				if (-1 == generateToken(g_strDftAccessKeyId, g_strDftAccessKeySecret, &g_strToken, &g_lExireTime)) {
					nRet	=	-1;
					break;
				}
			}
			
			this->m_pNlsCB	=	new SpeechSynthesizerCallback();
			if (this->m_pNlsCB == NULL)
			{
				apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
					"TTSSession::Start() failed on new SpeechSynthesizerCallback()!!!"
					);

				nRet	=	-1;
				break;
			}
			this->m_pNlsCB->setOnSynthesisStarted(OnSynthesisStarted, pContext); // 设置音频合成启动成功回调函数
			this->m_pNlsCB->setOnSynthesisCompleted(OnSynthesisCompleted, pContext); // 设置音频合成结束回调函数
			this->m_pNlsCB->setOnChannelClosed(OnSynthesisChannelClosed, pContext); // 设置音频合成通道关闭回调函数
			this->m_pNlsCB->setOnTaskFailed(OnSynthesisTaskFailed, pContext); // 设置异常失败回调函数
			this->m_pNlsCB->setOnBinaryDataReceived(OnBinaryDataRecved, pContext); // 设置文本音频数据接收回调函数


			this->m_pNlsReq	=	NlsClient::getInstance()->createSynthesizerRequest(this->m_pNlsCB);
			if (this->m_pNlsReq == NULL)
			{
				apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
					"TTSSession::Start() failed on g_pNlsClient->createTranscriberRequest!!!"
					);

				nRet	=	-1;
				break;
			}
			this->m_pNlsReq->setAppKey(g_strAppKey.c_str()); // 设置AppKey, 必填参数, 请参照官网申请
			this->m_pNlsReq->setVoice(g_strVoice.c_str()); // 发音人, 包含"xiaoyun", "ruoxi" "xiaogang". 可选参数, 默认是xiaoyun
			this->m_pNlsReq->setVolume(50); // 音量, 范围是0~100, 可选参数, 默认50
			this->m_pNlsReq->setFormat(g_strFormat.c_str()); // 音频编码格式, 可选参数, wav. 支持的格式pcm, wav, mp3
			this->m_pNlsReq->setSampleRate(g_iSampleRate); // 音频采样率, 包含8000, 16000. 可选参数, 默认是16000  
			this->m_pNlsReq->setSpeechRate(g_iSpeechRate); // 语速, 范围是-500~500, 可选参数, 默认是0
			this->m_pNlsReq->setPitchRate(g_iPitchRate); // 语调, 范围是-500~500, 可选参数, 默认是0
			this->m_pNlsReq->setMethod(g_iMethod); // 合成方法, 可选参数, 默认是0. 参数含义0:不带录音的参数合成; 1:带录音的拼接合成; 2:不带录音的拼接合成; 3:带录音的参数合成

			this->m_pNlsReq->setToken(g_strToken.c_str()); // 设置账号校验token, 必填参数
			this->m_pNlsReq->setText(value); // 设置待合成文本, 必填参数. 文本内容必须为UTF-8编码。

			/*
			* 3: start()为阻塞操作, 发送start指令之后, 会等待服务端响应, 或超时之后才返回
			*/
			if (this->m_pNlsReq->start() < 0) {
				apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
					"TTSSession::Start() failed!!!"
					);
				nRet	=	-1;
				break;
			}

			apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
				"TTSSession::Start() successfully."
				);

			nRet	=	0;
		}
		if (nRet != 0)
		{
			this->Stop(false);
		}

		return nRet;
	}

	int32_t	TTSSession::Stop(bool bNeedStop)
	{
		if (this->m_pNlsReq != NULL)
		{
			if (bNeedStop)
			{
				this->m_pNlsReq->stop();
			}

			NlsClient::getInstance()->releaseSynthesizerRequest(this->m_pNlsReq);
			this->m_pNlsReq	=	NULL;
		}

		if (this->m_pNlsCB != NULL)
		{
			delete this->m_pNlsCB;
			this->m_pNlsCB	=	NULL;
		}

		apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
			"TTSSession::Stop() invoked."
			);

		return 0;
	}
}; //namespace Nls2
