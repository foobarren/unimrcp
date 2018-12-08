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
#include <stdlib.h>
#include "mpf_buffer.h"
#include "apt_log.h"
#include "apt_consumer_task.h"
#include "mrcp_recog_engine.h"
#include "mpf_activity_detector.h"
#include "apr_file_info.h"
#include "nls2_asr.h"

#define RECOG_ENGINE_TASK_NAME "Nls2 Recog Engine"
#define RECOG_ENGINE_CONF_FILE_NAME "nls2recog.xml"

typedef struct nls2_recog_engine_t nls2_recog_engine_t;
typedef struct nls2_recog_channel_t nls2_recog_channel_t;
typedef struct nls2_recog_msg_t nls2_recog_msg_t;

/** Declaration of recognizer engine methods */
static apt_bool_t nls2_recog_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t nls2_recog_engine_open(mrcp_engine_t *engine);
static apt_bool_t nls2_recog_engine_close(mrcp_engine_t *engine);
static mrcp_engine_channel_t* nls2_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
	nls2_recog_engine_destroy,
	nls2_recog_engine_open,
	nls2_recog_engine_close,
	nls2_recog_engine_channel_create
};


/** Declaration of recognizer channel methods */
static apt_bool_t nls2_recog_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t nls2_recog_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t nls2_recog_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t nls2_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
	nls2_recog_channel_destroy,
	nls2_recog_channel_open,
	nls2_recog_channel_close,
	nls2_recog_channel_request_process
};

/** Declaration of recognizer audio stream methods */
static apt_bool_t nls2_recog_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t nls2_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t nls2_recog_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t nls2_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
	nls2_recog_stream_destroy,
	NULL,
	NULL,
	NULL,
	nls2_recog_stream_open,
	nls2_recog_stream_close,
	nls2_recog_stream_write,
	NULL
};

/** Declaration of nls recognizer engine */
struct nls2_recog_engine_t {
	apt_consumer_task_t    *task;
};

/** Declaration of nls recognizer channel */
struct nls2_recog_channel_t {
	/** Back pointer to engine */
	nls2_recog_engine_t     *nls2_engine;
	/** Engine channel base */
	mrcp_engine_channel_t   *channel;

	/** Active (in-progress) recognition request */
	mrcp_message_t          *recog_request;
	/** Pending stop response */
	mrcp_message_t          *stop_response;
	/** Indicates whether input timers are started */
	apt_bool_t               timers_started;

	/** Indicates whether asrserver are disconnected */
	//apt_bool_t               asrserver_disconnected;
	/** Voice activity detector */
	//mpf_activity_detector_t *detector;
	/** File to write utterance to */
	FILE                    *audio_out;

	Nls2ASR::ASRSession	*asr_session; //Nls2::ASRSession
	Nls2ASR::ParamCallBack		cbParam;
};

enum nls2_recog_event_et
{
	nls2_recog_event_e_Ignore			=	0,
	nls2_recog_event_e_BeginSpeaking		=	1,
	nls2_recog_event_e_SpeechDetected	=	2,
	nls2_recog_event_e_TimeoutNoInput		=	3,
	nls2_recog_event_e_TimeoutDetecting	=	4,
	nls2_recog_event_e_Error				=	5,
};


typedef enum {
	NLS2_RECOG_MSG_OPEN_CHANNEL,
	NLS2_RECOG_MSG_CLOSE_CHANNEL,
	NLS2_RECOG_MSG_REQUEST_PROCESS
} nls2_recog_msg_type_e;

/** Declaration of nls recognizer task message */
struct nls2_recog_msg_t {
	nls2_recog_msg_type_e  type;
	mrcp_engine_channel_t *channel; 
	mrcp_message_t        *request;
};

static apt_bool_t nls2_recog_msg_signal(nls2_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t nls2_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg);

static int32_t nls2_recog_on_speechrecognizer_notify(NlsEvent* cbEvent, void* pvContext);
static int32_t nls2_recog_on_speechtranscriber_notify(NlsEvent* cbEvent, void* pvContext);
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
	nls2_recog_engine_t *nls2_engine = (nls2_recog_engine_t*)apr_palloc(pool,sizeof(nls2_recog_engine_t));
	apt_task_t *task;
	apt_task_vtable_t *vtable;
	apt_task_msg_pool_t *msg_pool;

	msg_pool = apt_task_msg_pool_create_dynamic(sizeof(nls2_recog_msg_t),pool);
	nls2_engine->task = apt_consumer_task_create(nls2_engine,msg_pool,pool);
	if(!nls2_engine->task) {
		return NULL;
	}
	task = apt_consumer_task_base_get(nls2_engine->task);
	apt_task_name_set(task,RECOG_ENGINE_TASK_NAME);
	vtable = apt_task_vtable_get(task);
	if(vtable) {
		vtable->process_msg = nls2_recog_msg_process;
	}

	/* create engine base */
	return mrcp_engine_create(
				MRCP_RECOGNIZER_RESOURCE,  /* MRCP resource identifier */
				nls2_engine,               /* object to associate */
				&engine_vtable,            /* virtual methods table of engine */
				pool);                     /* pool to allocate memory from */
}

/** Destroy recognizer engine */
static apt_bool_t nls2_recog_engine_destroy(mrcp_engine_t *engine)
{
	nls2_recog_engine_t *nls2_engine = (nls2_recog_engine_t*)engine->obj;
	if(nls2_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(nls2_engine->task);
		apt_task_destroy(task);
		nls2_engine->task = NULL;
	}
	return TRUE;
}

/** Open recognizer engine */
static apt_bool_t nls2_recog_engine_open(mrcp_engine_t *engine)
{
	nls2_recog_engine_t *nls2_engine = (nls2_recog_engine_t*)engine->obj;

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

	if (Nls2ASR::GlobalInit(file_path_conf) != 0)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_ERROR,
			"Nls2ASR::GlobalInit(%s) failed!!!",
			file_path_conf
			);
		return FALSE;
	}
	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"Nls2ASR::GlobalInit(%s) successfully.",
		file_path_conf
		);

	if(nls2_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(nls2_engine->task);
		apt_task_start(task);
	}
	return mrcp_engine_open_respond(engine,TRUE);
}

/** Close recognizer engine */
static apt_bool_t nls2_recog_engine_close(mrcp_engine_t *engine)
{
	nls2_recog_engine_t *nls2_engine = (nls2_recog_engine_t*)engine->obj;

	Nls2ASR::GlobalFini();
	apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,
		"Nls2ASR::GlobalFini() successfully."
		);

	if(nls2_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(nls2_engine->task);
		apt_task_terminate(task,TRUE);
	}
	return mrcp_engine_close_respond(engine);
}

static mrcp_engine_channel_t* nls2_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
	mpf_stream_capabilities_t *capabilities;
	mpf_termination_t *termination; 

	/* create nls recog channel */
	nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)apr_palloc(pool,sizeof(nls2_recog_channel_t));
	recog_channel->nls2_engine = (nls2_recog_engine_t*)engine->obj;
	recog_channel->recog_request = NULL;
	recog_channel->stop_response = NULL;
	//recog_channel->detector = mpf_activity_detector_create(pool);
	recog_channel->audio_out = NULL;
	recog_channel->asr_session = NULL;
	recog_channel->cbParam.pfnOnNotify = nls2_recog_on_speechrecognizer_notify;
	recog_channel->cbParam.pContext = recog_channel;
	recog_channel->timers_started = FALSE;
	// recog_channel->asrserver_disconnected = FALSE;

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
static apt_bool_t nls2_recog_channel_destroy(mrcp_engine_channel_t *channel)
{
	/* nothing to destrtoy */
	return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent)*/
static apt_bool_t nls2_recog_channel_open(mrcp_engine_channel_t *channel)
{
	// nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)channel->method_obj;
	// recog_channel->asr_session	=	Nls2ASR::OpenASRSession();
	// if (recog_channel->asr_session == NULL)
	// {
	// 	apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to open Nls2ASR session " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
	// 	response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
	// 	return FALSE;
	// }
	// if (recog_channel->asr_session->Start(&recog_channel->cbParam) != 0)
	// {
	// 	apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to start Nls2ASR session " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
	// 	response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
	// 	return FALSE;
	// }
	return nls2_recog_msg_signal(NLS2_RECOG_MSG_OPEN_CHANNEL,channel,NULL);
}

/** Close engine channel (asynchronous response MUST be sent)*/
static apt_bool_t nls2_recog_channel_close(mrcp_engine_channel_t *channel)
{
	nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)channel->method_obj;
	if (recog_channel != NULL)
	{
		if (recog_channel->asr_session != NULL)
		{
			Nls2ASR::CloseASRSession(recog_channel->asr_session,recog_channel->timers_started);
			recog_channel->asr_session = NULL;
		}

		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"nls2_recog_channel_close() invoked!!!");
	}
	return nls2_recog_msg_signal(NLS2_RECOG_MSG_CLOSE_CHANNEL,channel,NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent)*/
static apt_bool_t nls2_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	return nls2_recog_msg_signal(NLS2_RECOG_MSG_REQUEST_PROCESS,channel,request);
}

/** Process RECOGNIZE request */
static apt_bool_t nls2_recog_channel_recognize(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process RECOGNIZE request */
	mrcp_recog_header_t *recog_header;
	nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)channel->method_obj;
	const mpf_codec_descriptor_t *descriptor = mrcp_engine_sink_stream_codec_get(channel);

	if(!descriptor) {
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
		return FALSE;
	}	
	/* get recognizer header */
	// recog_header = (mrcp_recog_header_t*)mrcp_resource_header_get(request);

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

	recog_channel->recog_request = request;
	recog_channel->cbParam.pfnOnNotify = nls2_recog_on_speechrecognizer_notify;
	recog_channel->asr_session	=	Nls2ASR::OpenASRSession();
	if (recog_channel->asr_session == NULL)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to open Nls2ASR session " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
		return FALSE;
	}
	if (recog_channel->asr_session->Start(&recog_channel->cbParam) != 0)
	{
		apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to start Nls2ASR session " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_RESOURCE_SPECIFIC_FAILURE;
		return FALSE;
	}

	recog_channel->timers_started = TRUE;
	
	response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynchronous response */
	mrcp_engine_channel_message_send(channel,response);

	return TRUE;
}

/** Process STOP request */
static apt_bool_t nls2_recog_channel_stop(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process STOP request */
	nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)channel->method_obj;
	if (recog_channel != NULL)
	{
		if (recog_channel->asr_session != NULL)
		{
			Nls2ASR::CloseASRSession(recog_channel->asr_session,recog_channel->timers_started);
			recog_channel->asr_session = NULL;
		}

		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"nls2_recog_channel_stop() invoked!!!");
	}

	/* store STOP request, make sure there is no more activity and only then send the response */
	recog_channel->stop_response = response;
	return TRUE;
}

/** Process START-INPUT-TIMERS request */
static apt_bool_t nls2_recog_channel_timers_start(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)channel->method_obj;
	recog_channel->timers_started = TRUE;
	return mrcp_engine_channel_message_send(channel,response);
}

/** Dispatch MRCP request */
static apt_bool_t nls2_recog_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request)
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
			processed = nls2_recog_channel_recognize(channel,request,response);
			break;
		case RECOGNIZER_GET_RESULT:
			break;
		case RECOGNIZER_START_INPUT_TIMERS:
			processed = nls2_recog_channel_timers_start(channel,request,response);
			break;
		case RECOGNIZER_STOP:
			processed = nls2_recog_channel_stop(channel,request,response);
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
static apt_bool_t nls2_recog_stream_destroy(mpf_audio_stream_t *stream)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t nls2_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
static apt_bool_t nls2_recog_stream_close(mpf_audio_stream_t *stream)
{
	return TRUE;
}


/** Callback is called from MPF engine context to write/send new frame */
static apt_bool_t nls2_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
	nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)stream->obj;
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
				nls2_recog_start_of_input(recog_channel);
				break;
			case MPF_DETECTOR_EVENT_INACTIVITY:
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Inactivity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls2_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
				break;
			case MPF_DETECTOR_EVENT_NOINPUT:
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Noinput " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					nls2_recog_recognition_complete(recog_channel,RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT);
				}
				break;
			default:
				break;
		}
		*/

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
			}else{
				if(recog_channel->timers_started){
					if(recog_channel->asr_session->FeedAudioData(frame->codec_frame.buffer, frame->codec_frame.size)==-1)
					{
						Nls2ASR::CloseASRSession(recog_channel->asr_session,recog_channel->timers_started);
						recog_channel->asr_session = NULL;
						apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"nls2_recog_stream_write() FeedAudioData Failed,asrserver disconnected!");
						return TRUE;
					}
				}
			}
		}

		if(recog_channel->audio_out) {
			fwrite(frame->codec_frame.buffer,1,frame->codec_frame.size,recog_channel->audio_out);
		}
	}
	return TRUE;
}

static apt_bool_t nls2_recog_msg_signal(nls2_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t status = FALSE;
	nls2_recog_channel_t *nls2_channel = (nls2_recog_channel_t*)channel->method_obj;
	nls2_recog_engine_t *nls2_engine = nls2_channel->nls2_engine;
	apt_task_t *task = apt_consumer_task_base_get(nls2_engine->task);
	apt_task_msg_t *msg = apt_task_msg_get(task);
	if(msg) {
		nls2_recog_msg_t *nls2_msg;
		msg->type = TASK_MSG_USER;
		nls2_msg = (nls2_recog_msg_t*) msg->data;

		nls2_msg->type = type;
		nls2_msg->channel = channel;
		nls2_msg->request = request;
		status = apt_task_msg_signal(task,msg);
	}
	return status;
}

static apt_bool_t nls2_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
	nls2_recog_msg_t *nls2_msg = (nls2_recog_msg_t*)msg->data;
	switch(nls2_msg->type) {
		case NLS2_RECOG_MSG_OPEN_CHANNEL:
			/* open channel and send asynch response */
			mrcp_engine_channel_open_respond(nls2_msg->channel,TRUE);
			break;
		case NLS2_RECOG_MSG_CLOSE_CHANNEL:
		{
			/* close channel, make sure there is no activity and send asynch response */
			nls2_recog_channel_t *recog_channel = (nls2_recog_channel_t*)nls2_msg->channel->method_obj;
			if(recog_channel->audio_out) {
				fclose(recog_channel->audio_out);
				recog_channel->audio_out = NULL;
			}

			mrcp_engine_channel_close_respond(nls2_msg->channel);
			break;
		}
		case NLS2_RECOG_MSG_REQUEST_PROCESS:
			nls2_recog_channel_request_dispatch(nls2_msg->channel,nls2_msg->request);
			break;
		default:
			break;
	}
	return TRUE;
}



/* Raise nls START-OF-INPUT event */
static apt_bool_t nls2_recog_start_of_input(nls2_recog_channel_t *recog_channel)
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
static apt_bool_t nls2_recog_result_load(nls2_recog_channel_t *recog_channel, mrcp_message_t *message,NlsEvent* cbEvent)
{
	apt_str_t *body = &message->body;
	body->buf		=	NULL;
	body->length	=	0;

	if (cbEvent != NULL)
	{
		body->buf	=	apr_psprintf(message->pool, "%s", cbEvent->getResult());
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
static apt_bool_t nls2_recog_recognition_complete(nls2_recog_channel_t *recog_channel,NlsEvent* cbEvent, mrcp_recog_completion_cause_e cause)
{
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
		nls2_recog_result_load(recog_channel,message,cbEvent);
	}

	recog_channel->recog_request = NULL;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}


static int32_t	nls2_recog_on_speechrecognizer_notify(NlsEvent* cbEvent, void* pvContext)
{
	nls2_recog_channel_t*	recog_channel =	(nls2_recog_channel_t*)pvContext;
	NlsEvent::EventType evType = cbEvent->getMsgType();
	switch (evType)
	{
	case NlsEvent::RecognitionStarted:
		{ // 调用start(), 成功与云端建立连接, sdk内部线程上报started事件
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"onRecognitionStarted, event(\"begin-speaking\") should be emitted " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls2_recog_start_of_input(recog_channel);
			}
			break;
		}
	case NlsEvent::RecognitionResultChanged:
		{ // 识别结果发生了变化, 当前句子的中间识别结果
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"onRecognitionResultChanged, event(\"speech-detected\") should be emitted " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
			}
			// nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
			break;
		}
	case NlsEvent::RecognitionCompleted:
		{//服务端停止实时音频流识别时
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"onRecognitionCompleted " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
			}
			break;
		}
	case NlsEvent::TaskFailed:
		{//识别过程(包含start(), send(), stop())发生异常时
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"onTaskFailed " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_ERROR);
				}
			}
			break;
		}
	case NlsEvent::Close:
		{///*语音功能通道连接关闭*/
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"onClose " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_ERROR);
				}
			}
			break;
		}
	default:
		{
			break;
		}
	}
}

static int32_t	nls2_recog_on_speechtranscriber_notify(NlsEvent* cbEvent, void* pvContext)
{
	nls2_recog_channel_t*	recog_channel =	(nls2_recog_channel_t*)pvContext;
	NlsEvent::EventType evType = cbEvent->getMsgType();
	switch (evType)
	{
	case NlsEvent::TranscriptionStarted:
		{ // 调用start(), 成功与云端建立连接, sdk内部线程上报started事件
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"onTranscriptionStarted, event(\"begin-speaking\") should be emitted " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
			}
			break;
		}
	case NlsEvent::TranscriptionResultChanged:
		{ // 识别结果发生了变化, 当前句子的中间识别结果
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"onTranscriptionResultChanged, event(\"speech-detected\") should be emitted " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
			}
			// nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
			break;
		}
	case NlsEvent::TranscriptionCompleted:
		{//服务端停止实时音频流识别时
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"onTranscriptionCompleted " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_ERROR);
			}
			break;
		}
	case NlsEvent::SentenceBegin:
		{//服务端检测到了一句话的开始
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"onSentenceBegin " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				nls2_recog_start_of_input(recog_channel);
			}
			// if(recog_channel->timers_started == TRUE) {
			// 	nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_ERROR);
			// }
			break;
		}
	case NlsEvent::SentenceEnd:
		{//服务端检测到了一句话结束
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"onSentenceEnd " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));

				nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
			}
			break;
		}
	case NlsEvent::TaskFailed:
		{//识别过程(包含start(), send(), stop())发生异常时
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"onTaskFailed " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_ERROR);
				}
			}
			break;
		}
	case NlsEvent::Close:
		{///*语音功能通道连接关闭*/
			if(recog_channel->recog_request){
				apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"onClose " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					nls2_recog_recognition_complete(recog_channel,cbEvent,RECOGNIZER_COMPLETION_CAUSE_ERROR);
				}
			}
			break;
		}
	default:
		{
			break;
		}
	}
}
