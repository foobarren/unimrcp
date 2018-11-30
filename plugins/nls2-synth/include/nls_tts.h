
#ifndef NLS_TTS_H
#define NLS_TTS_H

#include "NlsClient.h"
#include <stdint.h>
#include <string>
#include <map>
#include "mpf_buffer.h"
#include "apt_log.h"

class NlsTTSSession;
namespace NlsTTS
{
	int32_t	GlobalInit(const std::string& strFilePathConf);
	int32_t	GlobalFini();

	int32_t	Text2Audio(mpf_buffer_t* pAudioBuffer, const char* pstrText);

	NlsTTSSession*	OpenSession();
	int32_t	CloseSession(NlsTTSSession* pSession);

	// tool functions
	bool	IsZSTR(const char* pstr);
	int32_t	SetParam(std::map< std::string, std::string >& mapParams, const std::string& strParamName, const std::string& strParamValue);
};

class NlsTTSSession
{
public:
	NlsTTSSession();
	~NlsTTSSession();

	int32_t	SetAuthInfo(const std::string& strAccessKeyId, const std::string& strAccessKeySecret);
	int32_t	SetParams(const std::map< std::string, std::string >& mapParams);
	int32_t	SetParam(const std::string& strParamName, const std::string& strParamValue);

	int32_t	Text2Audio(mpf_buffer_t* pAudioBuffer, const char* pstrText);

	int32_t	OnAudioDataReceived(const char* pcAudioData, uint32_t lenAudioData);
	int32_t	OnText2AudioFinished(int32_t nResultCode);

private:
	std::string	m_strAccessKeyId;
	std::string	m_strAccessKeySecret;

	std::map< std::string, std::string >	m_mapParams;

	int32_t	m_nResultStatus;
	mpf_buffer_t*	m_pAudioBuffer;
};

#endif //end NLS_TTS_H

