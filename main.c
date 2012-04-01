/*
 * main.c
 *
 *  Created on: Mar 28, 2012
 *      Author: carlos
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>

#include "util.h"
#include "dwg/dwg.h"
#include "networking/ip_socket.h"

#define READ_INTERVAL			5 // seconds
#define KEEP_ALIVE_INTERVAL		6 // seconds
#define SMSSRV_PORT				7008

#define SMS_ENQUEUE(_queue_, _sms_) pthread_mutex_lock(&_queue_.lock); \
									_queue_.sms	= _sms_; \
									pthread_mutex_unlock(&_queue_.lock);

typedef struct sms_wait_queue
{
	int socket_fd;
	sms_t *sms;
	pthread_mutex_t lock;
} sms_wait_queue_t;

static str_t *process_message(str_t *input);
static void *gw_interactor(void *param);

sms_wait_queue_t _sms_queue;

static str_t *process_message(str_t *input)
{
	str_t *output		= malloc(sizeof(str_t)),
		  body;
    dwg_msg_des_header_t des_header;

    LOG(L_DEBUG, "%s: received %d bytes\n", __FUNCTION__, input->len);

	des_header	= dwg_deserialize_message(input, &body);

	switch(des_header.type)
	{
		case DWG_TYPE_KEEP_ALIVE:
			LOG(L_DEBUG, "%s: received DWG_TYPE_KEEP_ALIVE\n", __FUNCTION__);
			break;
		case DWG_TYPE_STATUS:
			LOG(L_DEBUG, "%s: received DWG_TYPE_STATUS\n", __FUNCTION__);

			LOG(L_DEBUG, "\tNumber of ports: %d\n", (int) body.s[0]);
			LOG(L_DEBUG, "\t\tPORT0: %d\n", (int) body.s[1]);
			LOG(L_DEBUG, "\t\tPORT1: %d\n", (int) body.s[2]);
			LOG(L_DEBUG, "\t\tPORT2: %d\n", (int) body.s[3]);
			LOG(L_DEBUG, "\t\tPORT3: %d\n", (int) body.s[4]);
			LOG(L_DEBUG, "\t\tPORT4: %d\n", (int) body.s[5]);
			LOG(L_DEBUG, "\t\tPORT5: %d\n", (int) body.s[6]);
			LOG(L_DEBUG, "\t\tPORT6: %d\n", (int) body.s[7]);
			LOG(L_DEBUG, "\t\tPORT7: %d\n", (int) body.s[8]);

			dwg_build_status_response(output);
			break;
		case DWG_TYPE_SEND_SMS:
			LOG(L_DEBUG, "%s: received DWG_TYPE_SEND_SMS\n", __FUNCTION__);
			break;
		case DWG_TYPE_SEND_SMS_RESP:
			LOG(L_DEBUG, "%s: received DWG_TYPE_SEND_SMS_RESP\n", __FUNCTION__);

			if ((int) body.s[0] == 0)
			{
				LOG(L_DEBUG, "%s: SMS was received by the gw\n", __FUNCTION__);
			}
			else
			{
				LOG(L_ERROR, "%s: Error sending sms\n", __FUNCTION__);
				hexdump(output->s, output->len);
			}

			break;
		case DWG_TYPE_SEND_SMS_RESULT:
			LOG(L_DEBUG, "%s: received DWG_TYPE_SEND_SMS_RESULT\n", __FUNCTION__);
			break;
		case DWG_TYPE_SEND_SMS_RESULT_RESP:
			LOG(L_DEBUG, "%s: received DWG_TYPE_SEND_SMS_RESULT_RESP\n", __FUNCTION__);
			break;
		default:
			LOG(L_DEBUG, "%s: Received unknown code %d\n", __FUNCTION__, des_header.type);

			hexdump(input->s, input->len);
			break;
	}

//	hexdump(output->s, output->len);

	return output;
}

static void *gw_interactor(void *param)
{
	char buffer[BUFFER_SIZE];
	int client_fd		= *((int *)param),
		bytes_read 		= 0,
		time_elapsed	= 0;

	_sms_queue.socket_fd	= client_fd;

	fcntl(client_fd, F_SETFL, O_NONBLOCK);	// set the socket to non-blocking mode

	for(;;)
	{
		int written	= 0;
		str_t from_gw	= { NULL, 0 },
			  to_gw		= { NULL, 0 };

		bzero(buffer, sizeof(buffer));

		bytes_read	= read(client_fd, buffer, sizeof(buffer));

		if (bytes_read > BUFFER_SIZE)
		{
			LOG(L_WARNING, "%s: Buffer overflow receiving %d bytes\n", __FUNCTION__, bytes_read);
			continue;
		}

		if (bytes_read < 0)
		{
			//LOG(L_DEBUG, "%s: Nothing to read\n", __FUNCTION__);
			sleep(READ_INTERVAL);
			time_elapsed	+= READ_INTERVAL;
		}
		else if (bytes_read == 0)
		{
			LOG(L_WARNING, "%s: empty message\n", __FUNCTION__);
			break;
		}
		else
		{
			from_gw.s	= buffer;
			from_gw.len	= bytes_read;

			to_gw	= *((str_t *) process_message(&from_gw));
		}

		pthread_mutex_lock(&_sms_queue.lock);
		if (_sms_queue.sms != NULL)
		{
			/*
			 * select a port randomically
			 */
			int gw_port	= rand() % 8;

			dwg_build_sms(_sms_queue.sms, gw_port, &to_gw);
			LOG(L_DEBUG, "%s: Sending sms to %s, using port %d\n", __FUNCTION__, _sms_queue.sms->destination.s, gw_port);
			_sms_queue.sms	= NULL;
		}
		pthread_mutex_unlock(&_sms_queue.lock);

		if (to_gw.len == 0 && time_elapsed >= KEEP_ALIVE_INTERVAL)
		{
			LOG(L_DEBUG, "%s: sending keep alive request\n", __FUNCTION__);

			dwg_build_keep_alive(&to_gw);
			time_elapsed	= 0;
		}

//		hexdump(to_gw.s, to_gw.len);

		if (to_gw.len > 0 && (written = write(client_fd, to_gw.s, to_gw.len)) != to_gw.len)
		{
			LOG(L_ERROR, "%s: write(): wrote %d/%d bytes only\n", __FUNCTION__, written, to_gw.len);
			close(client_fd);
			return NULL;
		}
	}

	return NULL;
}

int main(int argc, char** argv)
{
	ip_start_listener(SMSSRV_PORT, gw_interactor, DIR_DUAL);

	pthread_mutex_init(&_sms_queue.lock, NULL);

	str_t m	= { "hola", 4 };
	str_t nbr = { "0981146623", 10 };
	sms_t sms = { { "0981146623", 10 }, { "hola", 4 } };

	_sms_queue.sms	= NULL;
	for(;;)
	{
		getchar();
		SMS_ENQUEUE(_sms_queue, &sms);
	}

}

