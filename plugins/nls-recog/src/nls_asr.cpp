
#include "nls_asr.h"

#include "tinyxml2.h"
using namespace tinyxml2;

extern apt_log_source_t *RECOG_PLUGIN;

/** Use custom log source mark */
#define RECOG_LOG_MARK   APT_LOG_MARK_DECLARE(RECOG_PLUGIN)

namespace NlsASR
{
	NlsClient*	g_pNlsClient	=	NULL;

	std::map< std::string, std::string >	g_mapRuntimeCfgs;

	std::string	g_strDftAccessKeyId;
	std::string	g_strDftAccessKeySecret;

	std::map< std::string, std::string >	g_mapDftParams;

	std::string	g_strResultFormat;

	void	OnResultReceiced(NlsEvent* evt, void* pvContext);
	void	OnOperationFailed(NlsEvent* evt, void* pvContext);
	void	OnChannelClosed(NlsEvent* evt, void* pvContext);
};

int32_t	NlsASR::GlobalInit(const std::string& strFilePathConf)
{
	NlsASR::GlobalFini();

	NlsASR::g_mapRuntimeCfgs.insert(std::make_pair("DftTimeoutNoInput", NlsASR::Itoa(NlsASR::lluDftTimeoutNoInput)));
	NlsASR::g_mapRuntimeCfgs.insert(std::make_pair("DftTimeoutDetecting", NlsASR::Itoa(NlsASR::lluDftTimeoutDetecting)));

	NlsASR::g_mapDftParams.insert(std::make_pair("Url", "wss://nls-trans.dataapi.aliyun.com:443/realtime"));
	NlsASR::g_mapDftParams.insert(std::make_pair("AppKey", "nls-service-telephone8khz"));
	NlsASR::g_mapDftParams.insert(std::make_pair("SampleRate", "8000"));
	NlsASR::g_mapDftParams.insert(std::make_pair("VocabId", ""));
	NlsASR::g_mapDftParams.insert(std::make_pair("ResponseMode", "streaming"));
	NlsASR::g_mapDftParams.insert(std::make_pair("Format", "pcm"));
	NlsASR::g_mapDftParams.insert(std::make_pair("MaxEndSilence", "500"));

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

		XMLElement*	pNodeRuntimeCfgs	=	pRootNode->FirstChildElement("RuntimeCfgs");
		XMLElement*	pNodeAuthInfo		=	pRootNode->FirstChildElement("AuthInfo");
		XMLElement*	pNodeParams			=	pRootNode->FirstChildElement("Params");
		XMLElement*	pNodeResultFormat	=	pRootNode->FirstChildElement("ResultFormat");
		if ((pNodeRuntimeCfgs == NULL) || (pNodeAuthInfo == NULL) || (pNodeParams == NULL) || (pNodeResultFormat == NULL))
		{
			nRet	=	-1;
			break;
		}

		XMLElement*	pNodeRuntimeCfgIter	=	pNodeRuntimeCfgs->FirstChildElement();
		while (pNodeRuntimeCfgIter != NULL)
		{
			const char*	pstrCfgName	=	pNodeRuntimeCfgIter->Value();
			const char*	pstrCfgValue=	pNodeRuntimeCfgIter->GetText();
			if (! NlsASR::IsZSTR(pstrCfgName))
			{
				std::map< std::string, std::string >::iterator	iter	=
					NlsASR::g_mapRuntimeCfgs.find(pstrCfgName);
				if (iter != NlsASR::g_mapRuntimeCfgs.end())
				{
					iter->second	=	(pstrCfgValue != NULL) ? pstrCfgValue : "";
				}
				else
				{
					NlsASR::g_mapRuntimeCfgs.insert(std::make_pair(pstrCfgName, (pstrCfgValue != NULL) ? pstrCfgValue : ""));
				}
			}

			pNodeRuntimeCfgIter	=	pNodeRuntimeCfgIter->NextSiblingElement();
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
		NlsASR::g_strDftAccessKeyId.assign((pstrAccessKeyId != NULL) ? pstrAccessKeyId : "");
		NlsASR::g_strDftAccessKeySecret.assign((pstrAccessKeySecret != NULL) ? pstrAccessKeySecret : "");
		if (NlsASR::g_strDftAccessKeyId.empty() || NlsASR::g_strDftAccessKeySecret.empty())
		{
			nRet	=	-1;
			break;
		}

		XMLElement*	pNodeParamIter	=	pNodeParams->FirstChildElement();
		while (pNodeParamIter != NULL)
		{
			const char*	pstrParamName	=	pNodeParamIter->Value();
			const char*	pstrParamValue	=	pNodeParamIter->GetText();
			if (! NlsASR::IsZSTR(pstrParamName))
			{
				NlsASR::SetParam(NlsASR::g_mapDftParams, pstrParamName, (pstrParamValue != NULL) ? pstrParamValue : "");
			}

			pNodeParamIter	=	pNodeParamIter->NextSiblingElement();
		}

		const char*	pstrResultFormat	=	pNodeResultFormat->GetText();
		NlsASR::g_strResultFormat.assign((pstrResultFormat != NULL) ? pstrResultFormat : "%s");
		if (NlsASR::g_strResultFormat.empty())
		{
			nRet	=	-1;
			break;
		}

		NlsASR::g_pNlsClient	=	NlsClient::getInstance();
		if (NlsASR::g_pNlsClient == NULL)
		{
			nRet	=	-1;
			break;
		}

		nRet	=	0;
	}
	if (nRet != 0)
	{
		NlsASR::GlobalFini();
	}

	return nRet;
}

int32_t	NlsASR::GlobalFini()
{
	if (NlsASR::g_pNlsClient != NULL)
	{
		//TOOD
		// no invoke, for unsafe in multi invoke of getInstance()
		//NlsClient::releaseInstance();
		NlsASR::g_pNlsClient	=	NULL;
	}

	NlsASR::g_mapRuntimeCfgs.clear();

	NlsASR::g_strDftAccessKeyId.clear();
	NlsASR::g_strDftAccessKeySecret.clear();

	NlsASR::g_mapDftParams.clear();

	NlsASR::g_strResultFormat.assign("%s");

	return 0;
}

std::string	NlsASR::GetRuntimeCfg(const std::string& strCfgName)
{
	std::map< std::string, std::string >::const_iterator	iter	=
		NlsASR::g_mapRuntimeCfgs.find(strCfgName);
	if (iter == NlsASR::g_mapRuntimeCfgs.end() )
	{
		return "";
	}

	return iter->second;
}

NlsASRSession*	NlsASR::OpenSession(NlsASR::tpfnOnNotify pfnOnNotify, void* pvContext)
{
	NlsASRSession*	pSession	=	NULL;

	int32_t	nRet	=	-1;
	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		pSession	=	new NlsASRSession(pfnOnNotify, pvContext);
		if (pSession == NULL)
		{
			nRet	=	-1;
			break;
		}

		if (pSession->SetAuthInfo(NlsASR::g_strDftAccessKeyId, NlsASR::g_strDftAccessKeySecret) != 0)
		{
			nRet	=	-1;
			break;
		}

		if (pSession->SetParams(NlsASR::g_mapDftParams) != 0)
		{
			nRet	=	-1;
			break;
		}

		nRet	=	0;
	}
	if (nRet != 0)
	{
		NlsASR::CloseSession(pSession);
		pSession	=	NULL;
	}

	return pSession;
}

int32_t	NlsASR::CloseSession(NlsASRSession* pSession)
{
	if (pSession != NULL)
	{
		delete pSession;
		pSession	=	NULL;
	}

	return 0;
}

	// tool functions
bool	NlsASR::IsZSTR(const char* pstr)
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

std::string&	NlsASR::StrReplace(std::string& strFull, const std::string& substrOld, const std::string& substrNew)
{
	if (strFull.empty() || substrOld.empty())
	{
		return strFull;
	}

	std::string::size_type	lenSubstrOld	=	substrOld.length();

	std::string::size_type	posToReplace	=	strFull.find(substrOld);
	while (posToReplace != std::string::npos)
	{
		strFull.replace(posToReplace, lenSubstrOld, substrNew);

		posToReplace	=	strFull.find(substrOld);
	}

	return strFull;
}

std::string&	NlsASR::StrXMLEscape(std::string& strXML)
{
	NlsASR::StrReplace(strXML, "<", "&lt;");
	NlsASR::StrReplace(strXML, ">", "&gt;");
	NlsASR::StrReplace(strXML, "\"", "&quot;");
	NlsASR::StrReplace(strXML, "&", "&amp;");
	NlsASR::StrReplace(strXML, " ", "&nbsp;");

	return strXML;
}

std::string&	NlsASR::StrXMLUnescape(std::string& strXML)
{
	NlsASR::StrReplace(strXML, "&lt;", "<");
	NlsASR::StrReplace(strXML, "&gt;", ">");
	NlsASR::StrReplace(strXML, "&quot;", "\"");
	NlsASR::StrReplace(strXML, "&amp;", "&");
	NlsASR::StrReplace(strXML, "&nbsp;", " ");

	return strXML;
}

int32_t	NlsASR::SetParam(std::map< std::string, std::string >& mapParams, const std::string& strParamName, const std::string& strParamValue)
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

NlsASRSession::NlsASRSession(NlsASR::tpfnOnNotify pfnOnNotify, void* pvContext)
:m_pNlsReq(NULL), m_pNlsCB(NULL)
,m_eAsrStatus(NlsASR::E_AsrStatus_Invalid)
,m_pfnOnNotify(pfnOnNotify), m_pvContext(pvContext)
{
}

NlsASRSession::~NlsASRSession()
{
	this->Stop((this->m_eAsrStatus != NlsASR::E_AsrStatus_Invalid));
}

int32_t	NlsASRSession::SetAuthInfo(const std::string& strAccessKeyId, const std::string& strAccessKeySecret)
{
	this->m_strAccessKeyId.assign(strAccessKeyId);
	this->m_strAccessKeySecret.assign(strAccessKeySecret);

	return 0;
}

int32_t	NlsASRSession::SetParams(const std::map< std::string, std::string >& mapParams)
{
	this->m_mapParams	=	mapParams;

	return 0;
}

int32_t	NlsASRSession::SetParam(const std::string& strParamName, const std::string& strParamValue)
{
	return NlsASR::SetParam(this->m_mapParams, strParamName, strParamValue);
}

int32_t	NlsASRSession::Start()
{
	int32_t	nRet	=	-1;

	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		this->m_pNlsCB	=	new NlsSpeechCallback();
		if (this->m_pNlsCB == NULL)
		{
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
				"NlsASRSession::Start() failed on new NlsSpeechCallback()!!!"
				);

			nRet	=	-1;
			break;
		}
		this->m_pNlsCB->setOnMessageReceiced(NlsASR::OnResultReceiced, this);
		this->m_pNlsCB->setOnOperationFailed(NlsASR::OnOperationFailed, this);
		this->m_pNlsCB->setOnChannelClosed(NlsASR::OnChannelClosed, this);

		this->m_pNlsReq	=	NlsASR::g_pNlsClient->createRealTimeRequest(this->m_pNlsCB, NULL);
		if (this->m_pNlsReq == NULL)
		{
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
				"NlsASRSession::Start() failed on NlsASR::g_pNlsClient->createAsrRequest!!!"
				);

			nRet	=	-1;
			break;
		}

		std::map< std::string, std::string >::const_iterator	iterParams;
		for (iterParams = this->m_mapParams.begin(); iterParams != this->m_mapParams.end(); ++ iterParams)
		{
			apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
				"NlsASRSession::Start() setParam(\"%s\", \"%s\")!!!",
				iterParams->first.c_str(), iterParams->second.c_str()
				);

			this->m_pNlsReq->SetParam(iterParams->first.c_str(), iterParams->second.c_str());
		}

		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
			"NlsASRSession::Start() Authorize(\"%s\", \"%s\")!!!",
			this->m_strAccessKeyId.c_str(), this->m_strAccessKeySecret.c_str()
			);
		this->m_pNlsReq->Authorize(this->m_strAccessKeyId.c_str(), this->m_strAccessKeySecret.c_str());

		this->m_eAsrStatus	=	NlsASR::E_AsrStatus_Feeding;
		this->m_strAsrResult.clear();

		if (this->m_pNlsReq->Start() < 0)
		{
			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
				"NlsASRSession::Start() failed!!!"
				);

			nRet	=	-1;
			break;
		}
		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
			"NlsASRSession::Start() successfully."
			);

		nRet	=	0;
	}
	if (nRet != 0)
	{
		this->Stop(false);
	}

	return nRet;
}

int32_t	NlsASRSession::Stop(bool bNeedStop)
{
	if (this->m_pNlsReq != NULL)
	{
		if (bNeedStop)
		{
			this->m_pNlsReq->Stop();
		}

		NlsASR::g_pNlsClient->releaseNlsRequest(this->m_pNlsReq);
		this->m_pNlsReq	=	NULL;
	}

	if (this->m_pNlsCB != NULL)
	{
		delete this->m_pNlsCB;
		this->m_pNlsCB	=	NULL;
	}

	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"NlsASRSession::Stop() invoked."
		);

	return 0;
}

int32_t	NlsASRSession::FeedAudioData(const void* pvAudioData, uint32_t lenAudioData)
{
	if (this->m_pNlsReq == NULL)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
			"NlsASRSession::FeedAudioData() failed, this->m_pNlsReq is NULL!!!"
			);

		return -1;
	}

	return (this->m_pNlsReq->SendAudio((char*)pvAudioData, lenAudioData) > 0) ? 0 : -1;
}

int32_t	NlsASRSession::OnNotify(void* pvContext, NlsASR::ET_AsrStatus eStatus, const std::string& strResult)
{
	NlsASRSession*	pSession	=	(NlsASRSession*)pvContext;
	if (pSession == NULL)
	{
		return -1;
	}
	if ((pSession->m_pfnOnNotify == NULL) || (pSession->m_pvContext == NULL))
	{
		return -1;
	}

	if (eStatus != NlsASR::E_AsrStatus_Detecting)
	{
		return pSession->m_pfnOnNotify(pSession->m_pvContext, NlsASR::E_AsrStatus_Invalid, "");
	}

	// ignore while none-result returned
	if (strResult.find("\"text\":") == std::string::npos)
	{
		return 0;
	}

	if (strResult.find("\"status_code\":0") == std::string::npos)
	{ // partial result returned
		if (pSession->m_eAsrStatus == NlsASR::E_AsrStatus_Feeding)
		{
			pSession->m_eAsrStatus	=	NlsASR::E_AsrStatus_Detecting;
			return pSession->m_pfnOnNotify(pSession->m_pvContext, NlsASR::E_AsrStatus_Detecting, "");
		}
	}
	else
	{ // entire result returned
		std::string::size_type	posBegin	=	strResult.find("\"text\":\"");
		if (posBegin == std::string::npos)
		{
			return pSession->m_pfnOnNotify(pSession->m_pvContext, NlsASR::E_AsrStatus_Invalid, "");
		}
		posBegin	=	posBegin + strlen("\"text\":\"");
		std::string::size_type	posEnd		=	strResult.find("\"", posBegin+1);
		if (posEnd == std::string::npos)
		{
			return pSession->m_pfnOnNotify(pSession->m_pvContext, NlsASR::E_AsrStatus_Invalid, "");
		}
		std::string	strResultRaw	=	strResult.substr(posBegin, posEnd-posBegin);

		pSession->m_eAsrStatus	=	NlsASR::E_AsrStatus_Detected;
		/*
		std::string	strResultFormatted	=	std::string(
		"<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>"
		"<result>"
			"<interpretation confidence=\"1.0\">"
				"<instance>") + strResultRaw + "</instance>"
				"<input mode=\"speech\">" + strResultRaw + "</input>"
			"</interpretation>"
		"</result>";
		return pSession->m_pfnOnNotify(pSession->m_pvContext, NlsASR::E_AsrStatus_Detected, strResultFormatted);
		*/
		std::string	strResultFormatted	=	NlsASR::g_strResultFormat;
		NlsASR::StrReplace(strResultFormatted, "%s", strResultRaw);

		return pSession->m_pfnOnNotify(pSession->m_pvContext, NlsASR::E_AsrStatus_Detected, strResultFormatted);
	}

	return 0;
}

void	NlsASR::OnResultReceiced(NlsEvent* evt, void* pvContext)
{
	NlsASRSession*	pSession	=	(NlsASRSession*)pvContext;

	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"NlsASR::OnResultReceiced: %s!!!",
		evt->getResponse()
		);

	const char*	pstrResp	=	evt->getResponse();
	if (strstr(pstrResp, "\"finish\":1") == NULL)
	{
		pSession->OnNotify(pvContext, NlsASR::E_AsrStatus_Detecting, pstrResp);
	}
}

void	NlsASR::OnOperationFailed(NlsEvent* evt, void* pvContext)
{
	NlsASRSession*	pSession	=	(NlsASRSession*)pvContext;

	apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,
		"NlsASR::OnOperationFailed: %s!!!",
		evt->getErrorMessage()
		);

	pSession->OnNotify(pvContext, NlsASR::E_AsrStatus_Invalid, "");
}

void	NlsASR::OnChannelClosed(NlsEvent* evt, void* pvContext)
{
	NlsASRSession*	pSession	=	(NlsASRSession*)pvContext;

	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"NlsASR::OnChannelClosed: %s!!!",
		evt->getResponse()
		);
}

