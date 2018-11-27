
#ifndef NLS_TTS_H
#define NLS_TTS_H

#include "NlsClient.h"
#include <stdint.h>
#include <string>
#include <map>
#include "mpf_buffer.h"
#include "apt_log.h"

class NlsASRSession;
namespace NlsASR
{
	enum ET_AsrStatus
	{
		E_AsrStatus_Invalid		=	0,
		E_AsrStatus_Feeding		=	1,	// feeding audio data
		E_AsrStatus_Detecting	=	2,	// first sample of speech detected
									//		with result partially updated
		E_AsrStatus_Detected	=	3,	// entire speech detected
									//		with result updated
	};
	typedef int32_t	(*tpfnOnNotify)(void* pvContext, ET_AsrStatus eStatus, const std::string& strResult);

	int32_t	GlobalInit(const std::string& strFilePathConf);
	int32_t	GlobalFini();

	std::string	GetRuntimeCfg(const std::string& strCfgName);

	NlsASRSession*	OpenSession(tpfnOnNotify pfnOnNotify, void* pvContext);
	int32_t	CloseSession(NlsASRSession* pSession);

	// tool functions
	bool	IsZSTR(const char* pstr);
	std::string&	StrReplace(std::string& strFull, const std::string& substrOld, const std::string& substrNew);
	std::string&	StrXMLEscape(std::string& strXML);
	std::string&	StrXMLUnescape(std::string& strXML);
	template< typename T >
	std::string	Itoa(T value);
	int32_t	SetParam(std::map< std::string, std::string >& mapParams, const std::string& strParamName, const std::string& strParamValue);

	const uint64_t	lluDftTimeoutNoInput	=	10;
	const uint64_t	lluDftTimeoutDetecting	=	30;
};

class NlsASRSession
{
public:
	NlsASRSession(NlsASR::tpfnOnNotify pfnOnNotify, void* pvContext);
	~NlsASRSession();

	int32_t	SetAuthInfo(const std::string& strAccessKeyId, const std::string& strAccessKeySecret);
	int32_t	SetParams(const std::map< std::string, std::string >& mapParams);
	int32_t	SetParam(const std::string& strParamName, const std::string& strParamValue);

	int32_t	Start();
	int32_t	Stop(bool bNeedStop = true);

	int32_t	FeedAudioData(const void* pvAudioData, uint32_t lenAudioData);

	static int32_t	OnNotify(void* pvContext, NlsASR::ET_AsrStatus eStatus, const std::string& strResult);

private:
	std::string	m_strAccessKeyId;
	std::string	m_strAccessKeySecret;

	std::map< std::string, std::string >	m_mapParams;

	NlsRequest*			m_pNlsReq;
	NlsSpeechCallback*	m_pNlsCB;

	NlsASR::ET_AsrStatus	m_eAsrStatus;
	std::string		m_strAsrResult;

	NlsASR::tpfnOnNotify	m_pfnOnNotify;
	void*			m_pvContext;
};

#include <sstream>
template< typename T >
inline std::string	NlsASR::Itoa(T value)
{
	std::ostringstream	streamValue;
	streamValue << value;

	return streamValue.str();
}

#endif //end NLS_TTS_H

