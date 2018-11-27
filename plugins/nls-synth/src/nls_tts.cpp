
#include "nls_tts.h"

#include "tinyxml2.h"
using namespace tinyxml2;

extern apt_log_source_t *SYNTH_PLUGIN;

/** Use custom log source mark */
#define SYNTH_LOG_MARK   APT_LOG_MARK_DECLARE(SYNTH_PLUGIN)

namespace NlsTTS
{
	NlsClient*	g_pNlsClient	=	NULL;

	std::string	g_strDftAccessKeyId;
	std::string	g_strDftAccessKeySecret;

	std::map< std::string, std::string >	g_mapDftParams;

	void	OnMessageReceiced(NlsEvent* evt, void* pvContext);
	void	OnOperationFailed(NlsEvent* evt, void* pvContext);
	void	OnChannelClosed(NlsEvent* evt, void* pvContext);
	void	OnBinaryDataReceived(NlsEvent* evt, void* pvContext);
};

int32_t	NlsTTS::GlobalInit(const std::string& strFilePathConf)
{
	NlsTTS::GlobalFini();

	NlsTTS::g_mapDftParams.insert(std::make_pair("Url", "wss://nls.dataapi.aliyun.com:443"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("AppKey", "nls-service"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("TtsEnable", "true"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("TtsEncodeType", "wav"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("TtsVolume", "100"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("TtsNus", "0"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("TtsVoice", "xiaoyun"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("TtsSpeechRate", "0"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("BackgroundMusicId", "1"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("BackgroundMusicOffset", "0"));
	NlsTTS::g_mapDftParams.insert(std::make_pair("BstreamAttached", "false"));

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

		XMLElement*	pNodeAuthInfo	=	pRootNode->FirstChildElement("AuthInfo");
		XMLElement*	pNodeParams		=	pRootNode->FirstChildElement("Params");
		if ((pNodeAuthInfo == NULL) || (pNodeParams == NULL))
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
		NlsTTS::g_strDftAccessKeyId.assign((pstrAccessKeyId != NULL) ? pstrAccessKeyId : "");
		NlsTTS::g_strDftAccessKeySecret.assign((pstrAccessKeySecret != NULL) ? pstrAccessKeySecret : "");
		if (NlsTTS::g_strDftAccessKeyId.empty() || NlsTTS::g_strDftAccessKeySecret.empty())
		{
			nRet	=	-1;
			break;
		}

		XMLElement*	pNodeParamIter	=	pNodeParams->FirstChildElement();
		while (pNodeParamIter != NULL)
		{
			const char*	pstrParamName	=	pNodeParamIter->Value();
			const char*	pstrParamValue	=	pNodeParamIter->GetText();
			if (! NlsTTS::IsZSTR(pstrParamName))
			{
				NlsTTS::SetParam(NlsTTS::g_mapDftParams, pstrParamName, (pstrParamValue != NULL) ? pstrParamValue : "");
			}

			pNodeParamIter	=	pNodeParamIter->NextSiblingElement();
		}

		NlsTTS::g_pNlsClient	=	NlsClient::getInstance();
		if (NlsTTS::g_pNlsClient == NULL)
		{
			nRet	=	-1;
			break;
		}

		nRet	=	0;
	}
	if (nRet != 0)
	{
		NlsTTS::GlobalFini();
	}

	return nRet;
}

int32_t	NlsTTS::GlobalFini()
{
	if (NlsTTS::g_pNlsClient != NULL)
	{
		//TOOD
		// no invoke, for unsafe in multi invoke of getInstance()
		//NlsClient::releaseInstance();
		NlsTTS::g_pNlsClient	=	NULL;
	}

	NlsTTS::g_strDftAccessKeyId.clear();
	NlsTTS::g_strDftAccessKeySecret.clear();

	NlsTTS::g_mapDftParams.clear();

	return 0;
}

int32_t	NlsTTS::Text2Audio(mpf_buffer_t* pAudioBuffer, const char* pstrText)
{
	int32_t	nRet	=	-1;

	NlsTTSSession*	pSession	=	NULL;

	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		pSession	=	NlsTTS::OpenSession();
		if (pSession == NULL)
		{
			nRet	=	-1;
			break;
		}

		nRet	=	pSession->Text2Audio(pAudioBuffer, pstrText);
		if (nRet != 0)
		{
			break;
		}

		nRet	=	0;
	}
	if (nRet != 0)
	{
		//do-nothing
	}

	// release resources
	if (pSession != NULL)
	{
		NlsTTS::CloseSession(pSession);
		pSession	=	NULL;
	}

	return nRet;
}

NlsTTSSession*	NlsTTS::OpenSession()
{
	NlsTTSSession*	pSession	=	NULL;

	int32_t	nRet	=	-1;
	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		pSession	=	new NlsTTSSession;
		if (pSession == NULL)
		{
			nRet	=	-1;
			break;
		}

		if (pSession->SetAuthInfo(NlsTTS::g_strDftAccessKeyId, NlsTTS::g_strDftAccessKeySecret) != 0)
		{
			nRet	=	-1;
			break;
		}

		if (pSession->SetParams(NlsTTS::g_mapDftParams) != 0)
		{
			nRet	=	-1;
			break;
		}

		nRet	=	0;
	}
	if (nRet != 0)
	{
		NlsTTS::CloseSession(pSession);
		pSession	=	NULL;
	}

	return pSession;
}

int32_t	NlsTTS::CloseSession(NlsTTSSession* pSession)
{
	if (pSession != NULL)
	{
		delete pSession;
		pSession	=	NULL;
	}

	return 0;
}

	// tool functions
bool	NlsTTS::IsZSTR(const char* pstr)
{
	if (pstr == NULL)
	{
		return true;
	}

	if (strlen(pstr) == 0)
	{
		return true;
	}

	return false;
}

int32_t	NlsTTS::SetParam(std::map< std::string, std::string >& mapParams, const std::string& strParamName, const std::string& strParamValue)
{
	// check parameters
	if (strParamName.empty())
	{
		return -1;
	}

	std::map< std::string, std::string >::iterator	iter	=
		mapParams.find(strParamName);
	if (iter != mapParams.end())
	{
		iter->second	=	strParamValue;
	}
	else
	{
		mapParams.insert(std::make_pair(strParamName, strParamValue));
	}

	return 0;
}

NlsTTSSession::NlsTTSSession()
:m_nResultStatus(-1), m_pAudioBuffer(NULL)
{
}

NlsTTSSession::~NlsTTSSession()
{
}

int32_t	NlsTTSSession::SetAuthInfo(const std::string& strAccessKeyId, const std::string& strAccessKeySecret)
{
	this->m_strAccessKeyId.assign(strAccessKeyId);
	this->m_strAccessKeySecret.assign(strAccessKeySecret);

	return 0;
}

int32_t	NlsTTSSession::SetParams(const std::map< std::string, std::string >& mapParams)
{
	this->m_mapParams	=	mapParams;

	return 0;
}

int32_t	NlsTTSSession::SetParam(const std::string& strParamName, const std::string& strParamValue)
{
	return NlsTTS::SetParam(this->m_mapParams, strParamName, strParamValue);
}

int32_t	NlsTTSSession::Text2Audio(mpf_buffer_t* pAudioBuffer, const char* pstrText)
{
	// check parameters
	if ((pAudioBuffer == NULL) || (pstrText == NULL))
	{
		return -1;
	}
	this->m_pAudioBuffer	=	pAudioBuffer;

	int32_t	nRet	=	-1;

	NlsRequest*			pNlsReq	=	NULL;
	NlsSpeechCallback*	pNlsCB	=	NULL;

	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		pNlsCB	=	new NlsSpeechCallback();
		if (pNlsCB == NULL)
		{
			apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
				"NlsTTSSession::Text2Audio() failed: new NlsSpeechCallback!!! "
				);

			nRet	=	-1;
			break;
		}
		pNlsCB->setOnMessageReceiced(NlsTTS::OnMessageReceiced, this);
		pNlsCB->setOnOperationFailed(NlsTTS::OnOperationFailed, this);
		pNlsCB->setOnChannelClosed(NlsTTS::OnChannelClosed, this);
		pNlsCB->setOnBinaryDataReceived(NlsTTS::OnBinaryDataReceived, this);

		pNlsReq	=	NlsTTS::g_pNlsClient->createTtsRequest(pNlsCB, NULL);
		if (pNlsReq == NULL)
		{
			apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
				"NlsTTSSession::Text2Audio() failed: createTtsRequest!!! "
				);

			nRet	=	-1;
			break;
		}

		std::map< std::string, std::string >::const_iterator	iterParams;
		for (iterParams = this->m_mapParams.begin(); iterParams != this->m_mapParams.end(); ++ iterParams)
		{
			apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
				"NlsTTSSession::Text2Audio() setParam(\"%s\", \"%s\")!!!",
				iterParams->first.c_str(), iterParams->second.c_str()
				);

			pNlsReq->SetParam(iterParams->first.c_str(), iterParams->second.c_str());
		}
		apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
			"NlsTTSSession::Text2Audio() setParam(\"%s\", \"%s\")!!!",
			"TtsReq", pstrText
			);
		pNlsReq->SetParam("TtsReq", pstrText);

		apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
			"NlsTTSSession::Text2Audio() Authorize(\"%s\", \"%s\")!!!",
			this->m_strAccessKeyId.c_str(), this->m_strAccessKeySecret.c_str()
			);
		pNlsReq->Authorize(this->m_strAccessKeyId.c_str(), this->m_strAccessKeySecret.c_str());

		this->m_nResultStatus	=	0;

		if (pNlsReq->Start() < 0)
		{
			apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
				"NlsTTSSession::Text2Audio() failed: pNlsReq->Start()!!! "
				);

			nRet	=	-1;
			break;
		}
		pNlsReq->Stop();

		if (this->m_nResultStatus != 0)
		{
			apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
				"NlsTTSSession::Text2Audio() failed: DoNlsTransaction!!! "
				);

			nRet	=	-1;
			break;
		}

		apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
			"NlsTTSSession::Text2Audio() ended!!! "
			);

		nRet	=	0;
	}
	if (nRet != 0)
	{
		// do-nothing
	}

	if (pNlsReq != NULL)
	{
		NlsTTS::g_pNlsClient->releaseNlsRequest(pNlsReq);
		pNlsReq	=	NULL;
	}
	if (pNlsCB != NULL)
	{
		delete pNlsCB;
		pNlsCB	=	NULL;
	}

	return nRet;
}

int32_t	NlsTTSSession::OnAudioDataReceived(const char* pcAudioData, uint32_t lenAudioData)
{
	//check parameters
	if ((pcAudioData == NULL) || (lenAudioData == 0))
	{
		return -1;
	}

	return mpf_buffer_audio_write(this->m_pAudioBuffer, (void*)pcAudioData, lenAudioData) ? 0 : -1;
}

int32_t	NlsTTSSession::OnText2AudioFinished(int32_t nResultCode)
{
	this->m_nResultStatus	=	nResultCode;

	return 0;
}

void	NlsTTS::OnMessageReceiced(NlsEvent* evt, void* pvContext)
{
	NlsTTSSession*	pSession	=	(NlsTTSSession*)pvContext;

	apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
		"NlsTTS::OnMessageReceived: %s!!!",
		evt->getResponse()
		);
}

void	NlsTTS::OnOperationFailed(NlsEvent* evt, void* pvContext)
{
	NlsTTSSession*	pSession	=	(NlsTTSSession*)pvContext;

	apt_log(SYNTH_LOG_MARK,APT_PRIO_WARNING,
		"NlsTTS::OnOperationFailed: %s!!!",
		evt->getErrorMessage()
		);

	pSession->OnText2AudioFinished(-1);
}

void	NlsTTS::OnChannelClosed(NlsEvent* evt, void* pvContext)
{
	NlsTTSSession*	pSession	=	(NlsTTSSession*)pvContext;

	apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
		"NlsTTS::OnChannelClosed: %s!!!",
		evt->getResponse()
		);
}

void	NlsTTS::OnBinaryDataReceived(NlsEvent* evt, void* pvContext)
{
	NlsTTSSession*	pSession	=	(NlsTTSSession*)pvContext;
	std::vector< unsigned char >	vecAudioData	=	evt->getBinaryData();

	apt_log(SYNTH_LOG_MARK,APT_PRIO_INFO,
		"NlsTTS::OnBinaryDataReceived: %d-bytes audio data received!!!",
		vecAudioData.size()
		);

	pSession->OnAudioDataReceived((const char*)(& vecAudioData[0]), vecAudioData.size());
}

