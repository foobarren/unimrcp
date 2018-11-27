/*
 * Copyright 2008-2015 Arsen Chaloyan
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

/* 
 * Mandatory rules concerning plugin implementation.
 * 1. Each plugin MUST implement a plugin/engine creator function
 *    with the exact signature and name (the main entry point)
 *        MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
 * 2. Each plugin MUST declare its version number
 *        MRCP_PLUGIN_VERSION_DECLARE
 * 3. One and only one response MUST be sent back to the received request.
 * 4. Methods (callbacks) of the MRCP engine channel MUST not block.
 *   (asynchronous response can be sent from the context of other thread)
 * 5. Methods (callbacks) of the MPF engine stream MUST not block.
 */

#include "mrcp_recog_engine.h"
#include "mpf_activity_detector.h"
#include "apt_consumer_task.h"
#include "apt_log.h"
#include "apr_file_info.h"
#include "nls_asr.h"
#include <stdlib.h>

#define RECOG_ENGINE_TASK_NAME "Nls Recog Engine"
#define RECOG_ENGINE_CONF_FILE_NAME "nlsrecog.xml"

typedef struct nls_recog_engine_t nls_recog_engine_t;
typedef struct nls_recog_channel_t nls_recog_channel_t;
typedef struct nls_recog_msg_t nls_recog_msg_t;

/** Declaration of recognizer engine methods */
static apt_bool_t nls_recog_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t nls_recog_engine_open(mrcp_engine_t *engine);
static apt_bool_t nls_recog_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* nls_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
	nls_recog_engine_destroy,
	nls_recog_engine_open,
	nls_recog_engine_close,
	nls_recog_engine_channel_create
};


/** Declaration of recognizer channel methods */
static apt_bool_t nls_recog_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t nls_recog_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t nls_recog_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t nls_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
	nls_recog_channel_destroy,
	nls_recog_channel_open,
	nls_recog_channel_close,
	nls_recog_channel_request_process
};

/** Declaration of recognizer audio stream methods */
static apt_bool_t nls_recog_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t nls_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t nls_recog_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t nls_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
	nls_recog_stream_destroy,
	NULL,
	NULL,
	NULL,
	nls_recog_stream_open,
	nls_recog_stream_close,
	nls_recog_stream_write,
	NULL
};

struct nls_recog_state_machine_t {
	apr_pool_t*	pool;

	uint64_t	lluTimeBeginSession;
	uint64_t	lluTimeBeginSpeaking;
	uint64_t	lluTimeSpeechDetected;

	NlsASR::ET_AsrStatus	eAsrStatusPrev;

	apr_thread_mutex_t*		pmutexAsrStatus;
	NlsASR::ET_AsrStatus	eAsrStatus;
	char*		pstrAsrResult;

	uint64_t	lluTimeoutNoInput;
	uint64_t	lluTimeoutDetecting;
};

enum nls_recog_event_et
{
	nls_recog_event_e_Ignore			=	0,
	nls_recog_event_e_BeginSpeaking		=	1,
	nls_recog_event_e_SpeechDetected	=	2,
	nls_recog_event_e_TimeoutNoInput		=	3,
	nls_recog_event_e_TimeoutDetecting	=	4,
	nls_recog_event_e_Error				=	5,
};
static nls_recog_state_machine_t*	nls_recog_sm_new(apr_pool_t *pool, uint64_t lluTimeoutNoInput, uint64_t lluTimeoutDetecting);
static void							nls_recog_sm_delete(nls_recog_state_machine_t* recog_sm);
static int32_t						nls_recog_sm_on_notify(void* pvContext, NlsASR::ET_AsrStatus eAsrStatus, const std::string& strAsrResult);
static nls_recog_event_et			nls_recog_sm_get_status(nls_recog_state_machine_t* recog_sm);

/** Declaration of nls recognizer engine */
struct nls_recog_engine_t {
	apt_consumer_task_t    *task;
};

/** Declaration of nls recognizer channel */
struct nls_recog_channel_t {
	/** Back pointer to engine */
	nls_recog_engine_t     *nls_engine;
	/** Engine channel base */
	mrcp_engine_channel_t   *channel;

	/** Active (in-progress) recognition request */
	mrcp_message_t          *recog_request;
	/** Pending stop response */
	mrcp_message_t          *stop_response;
	/** Indicates whether input timers are started */
	apt_bool_t               timers_started;
	/** Voice activity detector */
	//mpf_activity_detector_t *detector;
	/** File to write utterance to */
	FILE                    *audio_out;

	NlsASRSession			*asr_session;
	nls_recog_state_machine_t	*recog_sm;
};

typedef enum {
	NLS_RECOG_MSG_OPEN_CHANNEL,
	NLS_RECOG_MSG_CLOSE_CHANNEL,
	NLS_RECOG_MSG_REQUEST_PROCESS
} nls_recog_msg_type_e;

/** Declaration of nls recognizer task message */
struct nls_recog_msg_t {
	nls_recog_msg_type_e  type;
	mrcp_engine_channel_t *channel; 
	mrcp_message_t        *request;
};

static apt_bool_t nls_recog_msg_signal(nls_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t nls_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg);

/** Declare this macro to set plugin version */
MRCP_PLUGIN_VERSION_DECLARE

/**
 * Declare this macro to use log routine of the server, plugin is loaded from.
 * Enable/add the corresponding entry in logger.xml to set a cutsom log source priority.
 *    <source name="RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(RECOG_PLUGIN,"RECOG-PLUGIN")

/** Use custom log source mark */
#define RECOG_LOG_MARK   APT_LOG_MARK_DECLARE(RECOG_PLUGIN)

/** Create nls recognizer engine */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
	nls_recog_engine_t *nls_engine = (nls_recog_engine_t*)apr_palloc(pool,sizeof(nls_recog_engine_t));
	apt_task_t *task;
	apt_task_vtable_t *vtable;
	apt_task_msg_pool_t *msg_pool;

	msg_pool = apt_task_msg_pool_create_dynamic(sizeof(nls_recog_msg_t),pool);
	nls_engine->task = apt_consumer_task_create(nls_engine,msg_pool,pool);
	if(!nls_engine->task) {
		return NULL;
	}
	task = apt_consumer_task_base_get(nls_engine->task);
	apt_task_name_set(task,RECOG_ENGINE_TASK_NAME);
	vtable = apt_task_vtable_get(task);
	if(vtable) {
		vtable->process_msg = nls_recog_msg_process;
	}

	/* create engine base */
	return mrcp_engine_create(
				MRCP_RECOGNIZER_RESOURCE,  /* MRCP resource identifier */
				nls_engine,               /* object to associate */
				&engine_vtable,            /* virtual methods table of engine */
				pool);                     /* pool to allocate memory from */
}

/** Destroy recognizer engine */
static apt_bool_t nls_recog_engine_destroy(mrcp_engine_t *engine)
{
	nls_recog_engine_t *nls_engine = (nls_recog_engine_t*)engine->obj;
	if(nls_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(nls_engine->task);
		apt_task_destroy(task);
		nls_engine->task = NULL;
	}
	return TRUE;
}

/** Open recognizer engine */
static apt_bool_t nls_recog_engine_open(mrcp_engine_t *engine)
{
	nls_recog_engine_t *nls_engine = (nls_recog_engine_t*)engine->obj;

	const apt_dir_layout_t *dir_layout = engine->dir_layout;
	const char* dir_path_conf = apt_dir_layout_path_get(dir_layout, APT_LAYOUT_CONF_DIR);
	char* file_path_conf = NULL;
	if (apr_filepath_merge(&file_path_conf, dir_path_conf, RECOG_ENGINE_CONF_FILE_NAME, APR_FILEPATH_NATIVE, engine->pool) != APR_SUCCESS)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_ERROR,
			"Get file path for %s failed!!!",
			RECOG_ENGINE_CONF_FILE_NAME
			);
		return FALSE;
	}

	if (NlsASR::GlobalInit(file_path_conf) != 0)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_ERROR,
			"NlsASR::GlobalInit(%s) failed!!!",
			file_path_conf
			);
		return FALSE;
	}
	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"NlsASR::GlobalInit(%s) successfully.",
		file_path_conf
		);

	if(nls_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(nls_engine->task);
		apt_task_start(task);
	}
	return mrcp_engine_open_respond(engine,TRUE);
}

/** Close recognizer engine */
static apt_bool_t nls_recog_engine_close(mrcp_engine_t *engine)
{
	nls_recog_engine_t *nls_engine = (nls_recog_engine_t*)engine->obj;

	NlsASR::GlobalFini();
	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"NlsASR::GlobalFini() successfully."
		);

	if(nls_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(nls_engine->task);
		apt_task_terminate(task,TRUE);
	}
	return mrcp_engine_close_respond(engine);
}

static mrcp_engine_channel_t* nls_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
	mpf_stream_capabilities_t *capabilities;
	mpf_termination_t *termination; 

	/* create nls recog channel */
	nls_recog_channel_t *recog_channel = (nls_recog_channel_t*)apr_palloc(pool,sizeof(nls_recog_channel_t));
	recog_channel->nls_engine = (nls_recog_engine_t*)engine->obj;
	recog_channel->recog_request = NULL;
	recog_channel->stop_response = NULL;
	//recog_channel->detector = mpf_activity_detector_create(pool);
	recog_channel->audio_out = NULL;
	recog_channel->asr_session = NULL;
	recog_channel->recog_sm = NULL;

	capabilities = mpf_sink_stream_capabilities_create(pool);
	mpf_codec_capabilities_add(
			&capabilities->codecs,
			MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
			"LPCM");

	/* create media termination */
	termination = mrcp_engine_audio_termination_create(
			recog_channel,        /* object to associate */
			&audio_stream_vtable, /* virtual methods table of audio stream */
			capabilities,         /* stream capabilities */
			pool);                /* pool to allocate memory from */

	/* create engine channel base */
	recog_channel->channel = mrcp_engine_channel_create(
			engine,               /* engine */
			&channel_vtable,      /* virtual methods table of engine channel */
			recog_channel,        /* object to associate */
			termination,          /* associated media termination */
			pool);                /* pool to allocate memory from */

	return recog_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t nls_recog_channel_destroy(mrcp_engine_channel_t *channel)
{
	/* nothing to destrtoy */
	return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent)*/
static apt_bool_t nls_recog_channel_open(mrcp_engine_channel_t *channel)
{
	return nls_recog_msg_signal(NLS_RECOG_MSG_OPEN_CHANNEL,channel,NULL);
}

/** Close engine channel (asynchronous response MUST be sent)*/
static apt_bool_t nls_recog_channel_close(mrcp_engine_channel_t *channel)
{
	return nls_recog_msg_signal(NLS_RECOG_MSG_CLOSE_CHANNEL,channel,NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent)*/
static apt_bool_t nls_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	return nls_recog_msg_signal(NLS_RECOG_MSG_REQUEST_PROCESS,channel,request);
}

/** Process RECOGNIZE request */
static apt_bool_t nls_recog_channel_recognize(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process RECOGNIZE request */
	mrcp_recog_header_t *recog_header;
	nls_recog_channel_t *recog_channel = (nls_recog_channel_t*)channel->method_obj;
	const mpf_codec_descriptor_t *descriptor = mrcp_engine_sink_stream_codec_get(channel);

	if(!descriptor) {
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
		return FALSE;
	}

	recog_channel->timers_started = TRUE;

	uint64_t	lluTimeoutNoInput	=	::atoll(NlsASR::GetRuntimeCfg("DftTimeoutNoInput").c_str());
	uint64_t	lluTimeoutDetecting	=	::atoll(NlsASR::GetRuntimeCfg("DftTimeoutDetecting").c_str());

	/* get recognizer header */
	recog_header = (mrcp_recog_header_t*)mrcp_resource_header_get(request);
	if(recog_header) {
		/*
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_START_INPUT_TIMERS) == TRUE) {
			recog_channel->timers_started = recog_header->start_input_timers;
		}
		*/
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_NO_INPUT_TIMEOUT) == TRUE) {
			//mpf_activity_detector_noinput_timeout_set(recog_channel->detector,recog_header->no_input_timeout);
			lluTimeoutNoInput	=	recog_header->no_input_timeout;
		}
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT) == TRUE) {
			//mpf_activity_detector_silence_timeout_set(recog_channel->detector,recog_header->speech_complete_timeout);
			lluTimeoutDetecting	=	recog_header->speech_complete_timeout;
		}
	}
	if (lluTimeoutNoInput == 0)		lluTimeoutNoInput	=	NlsASR::lluDftTimeoutNoInput;
	if (lluTimeoutDetecting == 0)	lluTimeoutDetecting	=	NlsASR::lluDftTimeoutDetecting;
	recog_channel->recog_sm	=	nls_recog_sm_new(channel->pool, lluTimeoutNoInput, lluTimeoutDetecting);
	if (recog_channel->recog_sm == NULL)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to new recog state machine " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
		return FALSE;
	}

	if(!recog_channel->audio_out) {
		const apt_dir_layout_t *dir_layout = channel->engine->dir_layout;
		char *file_name = apr_psprintf(channel->pool,"utter-%dkHz-%s.pcm",
							descriptor->sampling_rate/1000,
							request->channel_id.session_id.buf);
		char *file_path = apt_vardir_filepath_get(dir_layout,file_name,channel->pool);
		if(file_path) {
			apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Open Utterance Output File [%s] for Writing",file_path);
			recog_channel->audio_out = fopen(file_path,"wb");
			if(!recog_channel->audio_out) {
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to Open Utterance Output File [%s] for Writing",file_path);
			}
		}
	}

	response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynchronous response */
	mrcp_engine_channel_message_send(channel,response);

	recog_channel->asr_session	=	NlsASR::OpenSession(nls_recog_sm_on_notify, recog_channel->recog_sm);
	if (recog_channel->asr_session == NULL)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to open NlsASR session " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
		return FALSE;
	}
	if (recog_channel->asr_session->Start() != 0)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to start NlsASR session " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
		return FALSE;
	}

	recog_channel->recog_request = request;
	return TRUE;
}

/** Process STOP request */
static apt_bool_t nls_recog_channel_stop(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process STOP request */
	nls_recog_channel_t *recog_channel = (nls_recog_channel_t*)channel->method_obj;
	if (recog_channel != NULL)
	{
		if (recog_channel->asr_session != NULL)
		{
			NlsASR::CloseSession(recog_channel->asr_session);
			recog_channel->asr_session = NULL;
		}

		if (recog_channel->recog_sm != NULL )
		{
			nls_recog_sm_delete(recog_channel->recog_sm);
			recog_channel->recog_sm	=	NULL;
		}

		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"nls_recog_channel_stop() invoked!!!");
	}

	/* store STOP request, make sure there is no more activity and only then send the response */
	recog_channel->stop_response = response;
	return TRUE;
}

/** Process START-INPUT-TIMERS request */
static apt_bool_t nls_recog_channel_timers_start(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	nls_recog_channel_t *recog_channel = (nls_recog_channel_t*)channel->method_obj;
	recog_channel->timers_started = TRUE;
	return mrcp_engine_channel_message_send(channel,response);
}

/** Dispatch MRCP request */
static apt_bool_t nls_recog_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t processed = FALSE;
	mrcp_message_t *response = mrcp_response_create(request,request->pool);
	switch(request->start_line.method_id) {
		case RECOGNIZER_SET_PARAMS:
			break;
		case RECOGNIZER_GET_PARAMS:
			break;
		case RECOGNIZER_DEFINE_GRAMMAR:
			break;
		case RECOGNIZER_RECOGNIZE:
			processed = nls_recog_channel_recognize(channel,request,response);
			break;
		case RECOGNIZER_GET_RESULT:
			break;
		case RECOGNIZER_START_INPUT_TIMERS:
			processed = nls_recog_channel_timers_start(channel,request,response);
			break;
		case RECOGNIZER_STOP:
			processed = nls_recog_channel_stop(channel,request,response);
			break;
		default:
			break;
	}
	if(processed == FALSE) {
		/* send asynchronous response for not handled request */
		mrcp_engine_channel_message_send(channel,response);
	}
	return TRUE;
}

/** Callback is called from MPF engine context to destroy any additional data associated with audio stream */
static apt_bool_t nls_recog_stream_destroy(mpf_audio_stream_t *stream)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t nls_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
static apt_bool_t nls_recog_stream_close(mpf_audio_stream_t *stream)
{
	return TRUE;
}

/* Raise nls START-OF-INPUT event */
static apt_bool_t nls_recog_start_of_input(nls_recog_channel_t *recog_channel)
{
	/* create START-OF-INPUT event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_START_OF_INPUT,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/* Load nls recognition result */
static apt_bool_t nls_recog_result_load(nls_recog_channel_t *recog_channel, mrcp_message_t *message)
{
	apt_str_t *body = &message->body;
	body->buf		=	NULL;
	body->length	=	0;

	if (recog_channel->recog_sm != NULL)
	{
		if (recog_channel->recog_sm->pstrAsrResult != NULL)
		{
			body->buf	=	apr_psprintf(message->pool, "%s", recog_channel->recog_sm->pstrAsrResult);
		}
	}

	if(body->buf != NULL) {
		body->length = strlen(body->buf);

		mrcp_generic_header_t *generic_header;

		/* get/allocate generic header */
		generic_header = mrcp_generic_header_prepare(message);
		if(generic_header) {
			/* set content types */
			apt_string_assign(&generic_header->content_type,"application/x-nlsml",message->pool);
			mrcp_generic_header_property_add(message,GENERIC_HEADER_CONTENT_TYPE);
		}
	}
	return TRUE;
}

/* Raise nls RECOGNITION-COMPLETE event */
static apt_bool_t nls_recog_recognition_complete(nls_recog_channel_t *recog_channel, mrcp_recog_completion_cause_e cause)
{
	if (recog_channel != NULL)
	{
		if (recog_channel->asr_session != NULL)
		{
			NlsASR::CloseSession(recog_channel->asr_session);
			recog_channel->asr_session = NULL;

			apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"nls_recog_recognition_complete() invoked!!!");
		}
	}

	mrcp_recog_header_t *recog_header;
	/* create RECOGNITION-COMPLETE event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_RECOGNITION_COMPLETE,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* get/allocate recognizer header */
	recog_header = (mrcp_recog_header_t*)mrcp_resource_header_prepare(message);
	if(recog_header) {
		/* set completion cause */
		recog_header->completion_cause = cause;
		mrcp_resource_header_property_add(message,RECOGNIZER_HEADER_COMPLETION_CAUSE);
	}
	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

	if(cause == RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
		nls_recog_result_load(recog_channel,message);
	}

	recog_channel->recog_request = NULL;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/** Callback is called from MPF engine context to write/send new frame */
static apt_bool_t nls_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
	nls_recog_channel_t *recog_channel = (nls_recog_channel_t*)stream->obj;
	if(recog_channel->stop_response) {
		/* send asynchronous response to STOP request */
		mrcp_engine_channel_message_send(recog_channel->channel,recog_channel->stop_response);
		recog_channel->stop_response = NULL;
		recog_channel->recog_request = NULL;
		return TRUE;
	}

	if(recog_channel->recog_request) {
		/*
		mpf_detector_event_e det_event = mpf_activity_detector_process(recog_channel->detector,frame);
		switch(det_event) {
			case MPF_DETECTOR_EVENT_ACTIVITY:
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Activity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls_recog_start_of_input(recog_channel);
				break;
			case MPF_DETECTOR_EVENT_INACTIVITY:
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Inactivity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
				break;
			case MPF_DETECTOR_EVENT_NOINPUT:
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Noinput " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					nls_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT);
				}
				break;
			default:
				break;
		}
		*/
		if(recog_channel->asr_session)
		{
			recog_channel->asr_session->FeedAudioData(frame->codec_frame.buffer, frame->codec_frame.size);
		}

		if(recog_channel->recog_sm)
		{
			nls_recog_event_et	recog_event	=	nls_recog_sm_get_status(recog_channel->recog_sm);
			switch(recog_event)
			{
			case nls_recog_event_e_BeginSpeaking:
				{
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"First Activity Sample detected, event(\"begin-speaking\") should be emitted " APT_SIDRES_FMT,
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
					nls_recog_start_of_input(recog_channel);
					break;
				}
			case nls_recog_event_e_SpeechDetected:
				{
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"EOS detected, event(\"speech-detected\") should be emitted " APT_SIDRES_FMT,
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
					nls_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
					break;
				}
			case nls_recog_event_e_TimeoutNoInput:
				{
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Noinput " APT_SIDRES_FMT,
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
					if(recog_channel->timers_started == TRUE) {
						nls_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT);
					}
					break;
				}
			case nls_recog_event_e_TimeoutDetecting:
				{
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Too Much " APT_SIDRES_FMT,
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
					if(recog_channel->timers_started == TRUE) {
						nls_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_TOO_MUCH_SPEECH_TIMEOUT);
					}
					break;
				}
			case nls_recog_event_e_Error:
				{
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Too Much " APT_SIDRES_FMT,
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
					if(recog_channel->timers_started == TRUE) {
						nls_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_ERROR);
					}
					break;
				}
			default:
				break;
			}
		}

		if(recog_channel->recog_request) {
			if((frame->type & MEDIA_FRAME_TYPE_EVENT) == MEDIA_FRAME_TYPE_EVENT) {
				if(frame->marker == MPF_MARKER_START_OF_EVENT) {
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Start of Event " APT_SIDRES_FMT " id:%d",
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request),
						frame->event_frame.event_id);
				}
				else if(frame->marker == MPF_MARKER_END_OF_EVENT) {
					apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected End of Event " APT_SIDRES_FMT " id:%d duration:%d ts",
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request),
						frame->event_frame.event_id,
						frame->event_frame.duration);
				}
			}
		}

		if(recog_channel->audio_out) {
			fwrite(frame->codec_frame.buffer,1,frame->codec_frame.size,recog_channel->audio_out);
		}
	}
	return TRUE;
}

static apt_bool_t nls_recog_msg_signal(nls_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t status = FALSE;
	nls_recog_channel_t *nls_channel = (nls_recog_channel_t*)channel->method_obj;
	nls_recog_engine_t *nls_engine = nls_channel->nls_engine;
	apt_task_t *task = apt_consumer_task_base_get(nls_engine->task);
	apt_task_msg_t *msg = apt_task_msg_get(task);
	if(msg) {
		nls_recog_msg_t *nls_msg;
		msg->type = TASK_MSG_USER;
		nls_msg = (nls_recog_msg_t*) msg->data;

		nls_msg->type = type;
		nls_msg->channel = channel;
		nls_msg->request = request;
		status = apt_task_msg_signal(task,msg);
	}
	return status;
}

static apt_bool_t nls_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
	nls_recog_msg_t *nls_msg = (nls_recog_msg_t*)msg->data;
	switch(nls_msg->type) {
		case NLS_RECOG_MSG_OPEN_CHANNEL:
			/* open channel and send asynch response */
			mrcp_engine_channel_open_respond(nls_msg->channel,TRUE);
			break;
		case NLS_RECOG_MSG_CLOSE_CHANNEL:
		{
			/* close channel, make sure there is no activity and send asynch response */
			nls_recog_channel_t *recog_channel = (nls_recog_channel_t*)nls_msg->channel->method_obj;
			if(recog_channel->audio_out) {
				fclose(recog_channel->audio_out);
				recog_channel->audio_out = NULL;
			}

			mrcp_engine_channel_close_respond(nls_msg->channel);
			break;
		}
		case NLS_RECOG_MSG_REQUEST_PROCESS:
			nls_recog_channel_request_dispatch(nls_msg->channel,nls_msg->request);
			break;
		default:
			break;
	}
	return TRUE;
}

static nls_recog_state_machine_t*	nls_recog_sm_new(apr_pool_t *pool, uint64_t lluTimeoutNoInput, uint64_t lluTimeoutDetecting)
{
	nls_recog_state_machine_t*	recog_sm	=	NULL;

	int32_t	nRet	=	-1;
	for (int32_t iOnce=0; iOnce<1; ++iOnce)
	{
		recog_sm	=	(nls_recog_state_machine_t*)apr_palloc(pool, sizeof(nls_recog_state_machine_t));
		if (recog_sm == NULL)
		{
			nRet	=	-1;
			break;
		}

		memset(recog_sm, 0, sizeof(nls_recog_state_machine_t));

		recog_sm->pool					=	pool;

		recog_sm->lluTimeBeginSession	=	time(NULL);
		recog_sm->lluTimeBeginSpeaking	=	0;
		recog_sm->lluTimeSpeechDetected	=	0;

		recog_sm->eAsrStatusPrev		=	NlsASR::E_AsrStatus_Feeding;

		if (apr_thread_mutex_create(&recog_sm->pmutexAsrStatus, APR_THREAD_MUTEX_DEFAULT, pool) != APR_SUCCESS)
		{
			recog_sm->pmutexAsrStatus	=	NULL;

			nRet	=	-1;
			break;
		}
		recog_sm->eAsrStatus			=	NlsASR::E_AsrStatus_Feeding;
		recog_sm->pstrAsrResult			=	NULL;

		recog_sm->lluTimeoutNoInput		=	lluTimeoutNoInput;
		recog_sm->lluTimeoutDetecting	=	lluTimeoutDetecting;

		nRet	=	0;
	}
	if (nRet != 0)
	{
		if (recog_sm != NULL)
		{
			nls_recog_sm_delete(recog_sm);
			recog_sm	=	NULL;
		}
	}

	return recog_sm;
}

static void							nls_recog_sm_delete(nls_recog_state_machine_t* recog_sm)
{
	// memory pool used, no need free memory manually
	if (recog_sm != NULL)
	{
		if (recog_sm->pstrAsrResult != NULL)
		{
			//free(recog_sm->pstrAsrResult;
			//recog_sm->pstrAsrResult	=	NULL;
		}
		if (recog_sm->pmutexAsrStatus != NULL)
		{
			apr_thread_mutex_destroy(recog_sm->pmutexAsrStatus);
			recog_sm->pmutexAsrStatus	=	NULL;
		}

		//free(recog_sm);
		//recog_sm	=	NULL;
	}
}

static int32_t						nls_recog_sm_on_notify(void* pvContext, NlsASR::ET_AsrStatus eAsrStatus, const std::string& strAsrResult)
{
	nls_recog_state_machine_t*	recog_sm	=	(nls_recog_state_machine_t*)pvContext;

	switch (eAsrStatus)
	{
	case NlsASR::E_AsrStatus_Detecting:
		{ // 1st frame detected, should send begin-speaking event
			apr_thread_mutex_lock(recog_sm->pmutexAsrStatus);
			if (recog_sm->eAsrStatus != eAsrStatus)
			{
				recog_sm->eAsrStatus	=	eAsrStatus;
				recog_sm->lluTimeBeginSpeaking	=	time(NULL);
			}
			apr_thread_mutex_unlock(recog_sm->pmutexAsrStatus);
			break;
		}
	case NlsASR::E_AsrStatus_Detected:
		{ // detection finished, should send speech-detected event
			apr_thread_mutex_lock(recog_sm->pmutexAsrStatus);
			if (recog_sm->eAsrStatus != eAsrStatus)
			{
				recog_sm->eAsrStatus	=	eAsrStatus;
				recog_sm->pstrAsrResult	=	apr_pstrdup(recog_sm->pool, strAsrResult.c_str());
				recog_sm->lluTimeSpeechDetected	=	time(NULL);
			}
			apr_thread_mutex_unlock(recog_sm->pmutexAsrStatus);
			break;
		}
	case NlsASR::E_AsrStatus_Invalid:
		{ // error
			apr_thread_mutex_lock(recog_sm->pmutexAsrStatus);
			if (recog_sm->eAsrStatus != eAsrStatus)
			{
				recog_sm->eAsrStatus	=	eAsrStatus;
			}
			apr_thread_mutex_unlock(recog_sm->pmutexAsrStatus);
			break;
		}
	default:
		{
			break;
		}
	}
}

static nls_recog_event_et			nls_recog_sm_get_status(nls_recog_state_machine_t* recog_sm)
{
	nls_recog_event_et	recog_event	=	nls_recog_event_e_Ignore;

	uint64_t	lluTimeNow	=	time(NULL);

	apr_thread_mutex_lock(recog_sm->pmutexAsrStatus);
	switch (recog_sm->eAsrStatus)
	{
	case NlsASR::E_AsrStatus_Feeding:
		{
			// check if TimeoutNoInput
			if ((lluTimeNow - recog_sm->lluTimeBeginSession) >= recog_sm->lluTimeoutNoInput)
			{
				recog_event	=	nls_recog_event_e_TimeoutNoInput;
			}
			break;
		}
	case NlsASR::E_AsrStatus_Detecting:
		{ // 1st frame detected, should send begin-speaking event
			// check if TimeoutDetecing
			if ((lluTimeNow - recog_sm->lluTimeBeginSpeaking) >= recog_sm->lluTimeoutDetecting)
			{
				recog_event	=	nls_recog_event_e_TimeoutDetecting;
				break;
			}

			if (recog_sm->eAsrStatusPrev != recog_sm->eAsrStatus)
			{
				recog_sm->eAsrStatusPrev	=	recog_sm->eAsrStatus;
				recog_event	=	nls_recog_event_e_BeginSpeaking;
			}
			break;
		}
	case NlsASR::E_AsrStatus_Detected:
		{ // detection finished, should send speech-detected event
			if (recog_sm->eAsrStatusPrev != recog_sm->eAsrStatus)
			{
				recog_sm->eAsrStatusPrev	=	recog_sm->eAsrStatus;
				recog_event	=	nls_recog_event_e_SpeechDetected;
			}
			break;
		}
	case NlsASR::E_AsrStatus_Invalid:
	default:
		{ // error
			if (recog_sm->eAsrStatusPrev != recog_sm->eAsrStatus)
			{
				recog_sm->eAsrStatusPrev	=	recog_sm->eAsrStatus;
				recog_event	=	nls_recog_event_e_Error;
			}
			break;
		}
	}
	apr_thread_mutex_unlock(recog_sm->pmutexAsrStatus);

	return recog_event;
}

