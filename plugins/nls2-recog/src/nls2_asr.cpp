#include <ctime>
#include <iostream>
#include "nls2_asr.h"

#include "tinyxml2.h"

using std::cout;
using std::endl;
using namespace tinyxml2;
using AlibabaNls::NlsClient;

extern apt_log_source_t *RECOG_PLUGIN;

/** Use custom log source mark */
#define RECOG_LOG_MARK   APT_LOG_MARK_DECLARE(RECOG_PLUGIN)

namespace Nls2ASR
{
	NlsClient*	g_pNlsClient	=	NULL;

	std::string	g_strAppKey;
	std::string	g_strDftAccessKeyId;
	std::string	g_strDftAccessKeySecret;
	int	g_iSampleRate = 8000;
	std::string	g_strFormat = "pcm";
	int g_iMaxSentenceSilence = 800;
	std::string g_strToken = "";
	long g_lExireTime = -1;

	std::string	g_strResultFormat;



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
		XMLElement*	pNodeResultFormat	=	pRootNode->FirstChildElement("ResultFormat");
		if ( (pNodeAuthInfo == NULL) || (pNodeParams == NULL) || (pNodeResultFormat == NULL))
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
		XMLElement*	pNodeSampleRate	=	pNodeParams->FirstChildElement("SampleRate");
		XMLElement*	pNodeFormat	=	pNodeParams->FirstChildElement("Format");
		XMLElement*	pNodeMaxEndSilence	=	pNodeParams->FirstChildElement("MaxEndSilence");
		if ((pNodeAppKey == NULL) || (pNodeSampleRate == NULL) || (pNodeFormat == NULL) || (pNodeMaxEndSilence == NULL))
		{
			nRet	=	-1;
			break;
		}
		const char*	pstrAppKey	=	pNodeAppKey->GetText();
		const char*	pstrFormat	=	pNodeFormat->GetText();
		g_iSampleRate	=	pNodeSampleRate->IntText(8000);		
		g_iMaxSentenceSilence	=	pNodeMaxEndSilence->IntText(800);

		g_strAppKey.assign((pstrAppKey != NULL) ? pstrAppKey : "");
		g_strFormat.assign((pstrFormat != NULL) ? pstrFormat : "pcm");
		if (g_strAppKey.empty() || g_strFormat.empty())
		{
			nRet	=	-1;
			break;
		}

		const char*	pstrResultFormat	=	pNodeResultFormat->GetText();
		g_strResultFormat.assign((pstrResultFormat != NULL) ? pstrResultFormat : "%s");
		if (g_strResultFormat.empty())
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
		//TOOD
		// no invoke, for unsafe in multi invoke of getInstance()
		//NlsClient::releaseInstance();
		g_pNlsClient	=	NULL;
	}

	g_strDftAccessKeyId.clear();
	g_strDftAccessKeySecret.clear();

	g_strResultFormat.assign("%s");

	return 0;
}

ASRSession*	OpenASRSession()
{
	ASRSession*	pSession	=	NULL;

	int32_t	nRet	=	-1;
	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		pSession	=	new ASRSession();
		if (pSession == NULL)
		{
			nRet	=	-1;
			break;
		}

		nRet	=	0;
	}
	if (nRet != 0)
	{
		CloseASRSession(pSession,false);
		pSession	=	NULL;
	}

	return pSession;
}

int32_t	CloseASRSession(ASRSession* pSession,bool bNeedStop)
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
        cout << "Failed: " << nlsTokenRequest.getErrorMsg() << endl; /*获取失败原因*/

        return -1;
    }

    *token = nlsTokenRequest.getToken();
    *expireTime = nlsTokenRequest.getExpireTime();

    return 0;
}

/**
    * @brief 获取sendAudio发送延时时间
    * @param dataSize 待发送数据大小
    * @param sampleRate 采样率 16k/8K
    * @param compressRate 数据压缩率，例如压缩比为10:1的16k opus编码，此时为10；非压缩数据则为1
    * @return 返回sendAudio之后需要sleep的时间
    * @note 对于8k pcm 编码数据, 16位采样，建议每发送1600字节 sleep 100 ms.
            对于16k pcm 编码数据, 16位采样，建议每发送3200字节 sleep 100 ms.
            对于其它编码格式的数据, 用户根据压缩比, 自行估算, 比如压缩比为10:1的 16k opus,
            需要每发送3200/10=320 sleep 100ms.
*/
unsigned int getSendAudioSleepTime(const int dataSize,
                                   const int sampleRate,
                                   const int compressRate) {
    // 仅支持16位采样
    const int sampleBytes = 16;
    // 仅支持单通道
    const int soundChannel = 1;

    // 当前采样率，采样位数下每秒采样数据的大小
    int bytes = (sampleRate * sampleBytes * soundChannel) / 8;

    // 当前采样率，采样位数下每毫秒采样数据的大小
    int bytesMs = bytes / 1000;

    // 待发送数据大小除以每毫秒采样数据大小，以获取sleep时间
    int sleepMs = (dataSize * compressRate) / bytesMs;

    return sleepMs;
}

/**
    * @brief 调用start(), 成功与云端建立连接, sdk内部线程上报started事件
    * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onTranscriptionStarted(NlsEvent* cbEvent, void* cbParam) {	
	cout << "onTranscriptionStarted: "
		<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
		<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
		<< endl;
	// cout << "onTranscriptionStarted: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}

/**
    * @brief 服务端检测到了一句话的开始, sdk内部线程上报SentenceBegin事件
    * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onSentenceBegin(NlsEvent* cbEvent, void* cbParam) {
	cout << "onSentenceBegin: "
		<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
		<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
		<< ", index: " << cbEvent->getSentenceIndex() //句子编号，从1开始递增
		<< ", time: " << cbEvent->getSentenceTime() //当前已处理的音频时长，单位是毫秒
		<< endl;
	// cout << "onSentenceBegin: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}

/**
    * @brief 服务端检测到了一句话结束, sdk内部线程上报SentenceEnd事件
    * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onSentenceEnd(NlsEvent* cbEvent, void* cbParam) {
	cout << "onSentenceEnd: "
		<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
		<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
		<< ", result: " << cbEvent->getResult()    // 当前句子的完成识别结果
		<< ", index: " << cbEvent->getSentenceIndex()  // 当前句子的索引编号
		<< ", time: " << cbEvent->getSentenceTime()    // 当前句子的音频时长
        << ", begin_time: " << cbEvent->getSentenceBeginTime() // 对应的SentenceBegin事件的时间
        << ", confidence: " << cbEvent->getSentenceConfidence()    // 结果置信度,取值范围[0.0,1.0]，值越大表示置信度越高
		<< endl;
	// cout << "onSentenceEnd: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}

/**
    * @brief 识别结果发生了变化, sdk在接收到云端返回到最新结果时, sdk内部线程上报ResultChanged事件
    * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onTranscriptionResultChanged(NlsEvent* cbEvent, void* cbParam) {
	cout << "onTranscriptionResultChanged: "
		<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
		<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
		<< ", result: " << cbEvent->getResult()    // 当前句子的中间识别结果
		<< ", index: " << cbEvent->getSentenceIndex()  // 当前句子的索引编号
		<< ", time: " << cbEvent->getSentenceTime()    // 当前句子的音频时长
		<< endl;
	// cout << "onTranscriptionResultChanged: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}

/**
    * @brief 服务端停止实时音频流识别时, sdk内部线程上报Completed事件
    * @note 上报Completed事件之后，SDK内部会关闭识别连接通道. 此时调用sendAudio会返回-1, 请停止发送.
    *       不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onTranscriptionCompleted(NlsEvent* cbEvent, void* cbParam) {
	cout << "onTranscriptionCompleted: "
		<< "status code: " << cbEvent->getStausCode()  // 获取消息的状态码，成功为0或者20000000，失败时对应失败的错误码
		<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
		<< endl;
	// cout << "onTranscriptionCompleted: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}

/**
    * @brief 识别过程(包含start(), send(), stop())发生异常时, sdk内部线程上报TaskFailed事件
    * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @note 上报TaskFailed事件之后, SDK内部会关闭识别连接通道. 此时调用sendAudio会返回-1, 请停止发送
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onTaskFailed(NlsEvent* cbEvent, void* cbParam) {
	cout << "onTaskFailed: "
		<< "status code: " << cbEvent->getStausCode()
		<< ", task id: " << cbEvent->getTaskId()   // 当前任务的task id，方便定位问题，建议输出
		<< ", error message: " << cbEvent->getErrorMessage()
		<< endl;
	// cout << "onTaskFailed: All response:" << cbEvent->getAllResponse() << endl; // 获取服务端返回的全部信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}

/**
    * @brief 识别结束或发生异常时，会关闭连接通道, sdk内部线程上报ChannelCloseed事件
    * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
    * @param cbEvent 回调事件结构, 详见nlsEvent.h
    * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
    * @return
*/
void onChannelClosed(NlsEvent* cbEvent, void* cbParam) {
	cout << "OnChannelCloseed: All response: " << cbEvent->getAllResponse() << endl; // getResponse() 可以通道关闭信息
	ParamCallBack*	pSession	=	(ParamCallBack*)cbParam;
	pSession->pfnOnNotify(cbEvent,pSession->pContext);
}


ASRSession::ASRSession()
:m_pNlsReq(NULL), m_pNlsCB(NULL)
{
}

ASRSession::~ASRSession()
{
	this->Stop();
}

int32_t	ASRSession::Start(void* pContext)
{
	int32_t	nRet	=	-1;

	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		/**
		 * 获取当前系统时间戳，判断token是否过期
		 */
		std::time_t curTime = std::time(0);
		if (g_lExireTime - curTime < 10) {
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
			 "the token will be expired, please generate new token by AccessKey-ID and AccessKey-Secret." 
			 );
			if (-1 == generateToken(g_strDftAccessKeyId, g_strDftAccessKeySecret, &g_strToken, &g_lExireTime)) {
				nRet	=	-1;
				break;
			}
		}
		
		this->m_pNlsCB	=	new SpeechTranscriberCallback();
		if (this->m_pNlsCB == NULL)
		{
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
				"ASRSession::Start() failed on new SpeechTranscriberCallback()!!!"
				);

			nRet	=	-1;
			break;
		}
		this->m_pNlsCB->setOnTranscriptionStarted(onTranscriptionStarted, pContext); // 设置识别启动回调函数
		this->m_pNlsCB->setOnTranscriptionResultChanged(onTranscriptionResultChanged, pContext); // 设置识别结果变化回调函数
		this->m_pNlsCB->setOnTranscriptionCompleted(onTranscriptionCompleted, pContext); // 设置语音转写结束回调函数
		this->m_pNlsCB->setOnSentenceBegin(onSentenceBegin, pContext); // 设置一句话开始回调函数
		this->m_pNlsCB->setOnSentenceEnd(onSentenceEnd, pContext); // 设置一句话结束回调函数
		this->m_pNlsCB->setOnTaskFailed(onTaskFailed, pContext); // 设置异常识别回调函数
		this->m_pNlsCB->setOnChannelClosed(onChannelClosed, pContext); // 设置识别通道关闭回调函数


		this->m_pNlsReq	=	g_pNlsClient->createTranscriberRequest(this->m_pNlsCB);
		if (this->m_pNlsReq == NULL)
		{
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
				"ASRSession::Start() failed on g_pNlsClient->createTranscriberRequest!!!"
				);

			nRet	=	-1;
			break;
		}
		this->m_pNlsReq->setAppKey(g_strAppKey.c_str()); // 设置AppKey, 必填参数, 请参照官网申请
		this->m_pNlsReq->setFormat(g_strFormat.c_str()); // 设置音频数据编码格式, 可选参数，目前支持pcm, opu, opus, speex. 默认是pcm
		this->m_pNlsReq->setSampleRate(g_iSampleRate); // 设置音频数据采样率, 可选参数，目前支持16000, 8000. 默认是16000
		this->m_pNlsReq->setIntermediateResult(false); // 设置是否返回中间识别结果, 可选参数. 默认false
		this->m_pNlsReq->setPunctuationPrediction(false); // 设置是否在后处理中添加标点, 可选参数. 默认false
		this->m_pNlsReq->setInverseTextNormalization(false); // 设置是否在后处理中执行数字转写, 可选参数. 默认false
		this->m_pNlsReq->setSemanticSentenceDetection(false); // 设置是否语义断句, 可选参数. 默认false
		this->m_pNlsReq->setMaxSentenceSilence(g_iMaxSentenceSilence);
		this->m_pNlsReq->setToken(g_strToken.c_str()); // 设置账号校验token, 必填参数
		/*
		* 3: start()为阻塞操作, 发送start指令之后, 会等待服务端响应, 或超时之后才返回
		*/
		if (this->m_pNlsReq->start() < 0) {
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
				"ASRSession::Start() failed!!!"
				);
			nRet	=	-1;
			break;
		}

		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
			"ASRSession::Start() successfully."
			);

		nRet	=	0;
	}
	if (nRet != 0)
	{
		this->Stop(false);
	}

	return nRet;
}

int32_t	ASRSession::Stop(bool bNeedStop)
{
	if (this->m_pNlsReq != NULL)
	{
		if (bNeedStop)
		{
			this->m_pNlsReq->stop();
		}

		g_pNlsClient->releaseTranscriberRequest(this->m_pNlsReq);
		this->m_pNlsReq	=	NULL;
	}

	if (this->m_pNlsCB != NULL)
	{
		delete this->m_pNlsCB;
		this->m_pNlsCB	=	NULL;
	}

	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"ASRSession::Stop() invoked."
		);

	return 0;
}

int32_t	ASRSession::FeedAudioData(const void* pvAudioData, uint32_t lenAudioData)
{
	if (this->m_pNlsReq == NULL)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
			"ASRSession::FeedAudioData() failed, this->m_pNlsReq is NULL!!!"
			);

		return -1;
	}

	return this->m_pNlsReq->sendAudio((char*)pvAudioData, lenAudioData) ;
}

// int32_t	ASRSession::OnNotify(void* pvContext, ET_AsrStatus eStatus, const std::string& strResult)
// {
// 	NlsASRSession*	pSession	=	(NlsASRSession*)pvContext;
// 	if (pSession == NULL)
// 	{
// 		return -1;
// 	}
// 	if ((pSession->m_pfnOnNotify == NULL) || (pSession->m_pvContext == NULL))
// 	{
// 		return -1;
// 	}

// 	if (eStatus != E_AsrStatus_Detecting)
// 	{
// 		return pSession->m_pfnOnNotify(pSession->m_pvContext, E_AsrStatus_Invalid, "");
// 	}

// 	// ignore while none-result returned
// 	if (strResult.find("\"text\":") == std::string::npos)
// 	{
// 		return 0;
// 	}

// 	if (strResult.find("\"status_code\":0") == std::string::npos)
// 	{ // partial result returned
// 		if (pSession->m_eAsrStatus == E_AsrStatus_Feeding)
// 		{
// 			pSession->m_eAsrStatus	=	E_AsrStatus_Detecting;
// 			return pSession->m_pfnOnNotify(pSession->m_pvContext, E_AsrStatus_Detecting, "");
// 		}
// 	}
// 	else
// 	{ // entire result returned
// 		std::string::size_type	posBegin	=	strResult.find("\"text\":\"");
// 		if (posBegin == std::string::npos)
// 		{
// 			return pSession->m_pfnOnNotify(pSession->m_pvContext, E_AsrStatus_Invalid, "");
// 		}
// 		posBegin	=	posBegin + strlen("\"text\":\"");
// 		std::string::size_type	posEnd		=	strResult.find("\"", posBegin+1);
// 		if (posEnd == std::string::npos)
// 		{
// 			return pSession->m_pfnOnNotify(pSession->m_pvContext, E_AsrStatus_Invalid, "");
// 		}
// 		std::string	strResultRaw	=	strResult.substr(posBegin, posEnd-posBegin);

// 		pSession->m_eAsrStatus	=	E_AsrStatus_Detected;
// 		/*
// 		std::string	strResultFormatted	=	std::string(
// 		"<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>"
// 		"<result>"
// 			"<interpretation confidence=\"1.0\">"
// 				"<instance>") + strResultRaw + "</instance>"
// 				"<input mode=\"speech\">" + strResultRaw + "</input>"
// 			"</interpretation>"
// 		"</result>";
// 		return pSession->m_pfnOnNotify(pSession->m_pvContext, E_AsrStatus_Detected, strResultFormatted);
// 		*/
// 		std::string	strResultFormatted	=	g_strResultFormat;
// 		StrReplace(strResultFormatted, "%s", strResultRaw);

// 		return pSession->m_pfnOnNotify(pSession->m_pvContext, E_AsrStatus_Detected, strResultFormatted);
// 	}

// 	return 0;
// }

}; //namespace Nls2


