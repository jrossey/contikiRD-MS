/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      Erbium (Er) CoAP client example.
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "er-coap-engine.h"
#include "dev/button-sensor.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]", (lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3], (lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif


#define REMOTE_PORT UIP_HTONS(COAP_DEFAULT_PORT) 
#define MAX_DATA_LEN 256
#define MAX_URI_LEN 40
#define START_MS_REGISTRATION		3 /* number of seconds it takes after booting the sensor before sensor registers with mirror server */

#include "er-coap-block1.h"
#include "er-coap-separate.h"
#include "er-coap-transactions.h"
#include "er-coap-engine.h"

PROCESS(er_example_client, "Example Mirror Server Client");
AUTOSTART_PROCESSES(&er_example_client);



static int s_ResponseReceived;
static void *s_pResponse;  // Payload of the received acknowledge
static char LocationPath[20];

/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
void
client_chunk_handler(void *response)
{
  unsigned code=((coap_packet_t *)response)->code;

  s_ResponseReceived=(code>>5)*100+(code&0x1f);
  s_pResponse=response;
}

PROCESS_THREAD(er_example_client, ev, data)
{
  	PROCESS_BEGIN();

	static struct etimer mirror_timer;
	etimer_set(&mirror_timer, START_MS_REGISTRATION * CLOCK_SECOND);
	
	static uip_ipaddr_t mirror_server_ipaddr;
	uip_ip6addr(&mirror_server_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0x0001);

	static char* ms_registration_url = "/ms";
	static char* ms_registration_ep[20];
  
	/* create endpoint uri_query based on rime address */
	uint8_t i;
	uint8_t ep_pos = 0;
	ep_pos += snprintf((char *)ms_registration_ep + ep_pos, 5, "ep=");
	for(i = 7; i < sizeof(linkaddr_node_addr.u8)-1; i++) {
		//ms_registration_ep[i] = rimeaddr_node_addr.u8[i];
		//PRINTF("ep_pos: %u\n",ep_pos);
		ep_pos += snprintf((char *)ms_registration_ep + ep_pos, 5, "%d.",linkaddr_node_addr.u8[i]);
	}
	ep_pos += snprintf((char *)ms_registration_ep + ep_pos, 5, "%d",linkaddr_node_addr.u8[i]);

  	static coap_packet_t request[1];      /* This way the packet can be treated as pointer as usual. */

  	/* receives all CoAP messages */
  	coap_init_engine();


  	SENSORS_ACTIVATE(button_sensor);

  	static uint16_t button_presses=0;
  	while(1) {
    		PROCESS_YIELD();
    		if(ev == sensors_event && data == &button_sensor) {
			printf("Button pressed.\n");
			button_presses++;
  			char url[MAX_URI_LEN];


  			sprintf(url,"%s/%s",LocationPath, "actuators/button");
      			char payload[MAX_DATA_LEN];
      			uint8_t len=sprintf(payload,"Button pressed (%u)!",button_presses);
  
			coap_init_message(request, COAP_TYPE_CON, COAP_PUT, 0 );
      			coap_set_header_uri_path(request, url);
      			coap_set_payload(request, payload, len); 
			printf("--Sending \"%s\" to %s on MS--\n\n",payload,url);
      			COAP_BLOCKING_REQUEST(&mirror_server_ipaddr, REMOTE_PORT, request, client_chunk_handler);

    		}else if(etimer_expired(&mirror_timer)){ //register resource with MS
	
			//first create string with well-known/core payload
			static char well_known_core_msg[MAX_DATA_LEN];
			uint16_t pos = 0;
				
			sprintf(well_known_core_msg, "</actuators/button>;title=\"Button\"");
	
			coap_init_message(request, COAP_TYPE_CON, COAP_POST,0);
			coap_set_header_uri_path(request, ms_registration_url);
			REST.set_header_content_type(request, REST.type.APPLICATION_LINK_FORMAT);

			coap_set_header_uri_query(request, ms_registration_ep);
			coap_set_payload(request, well_known_core_msg, strlen(well_known_core_msg));
			
			COAP_BLOCKING_REQUEST(&mirror_server_ipaddr, REMOTE_PORT, request,client_chunk_handler); 

			if (s_ResponseReceived==201){
    				const char *path;
    				int Length=coap_get_header_location_path(s_pResponse, &path);
    				memcpy(LocationPath, path, Length);
    				LocationPath[Length]=0;
		    		printf("Location Path is %s\n", LocationPath);
  			}else{
    				printf("Registering on mirror proxy. Wrong or no response received %d\n",s_ResponseReceived);
    				PT_EXIT(process_pt);  /* We consider all other response codes an error, since we need the location path to send data to the mirror proxy */
  			}
		}
  	}

  	PROCESS_END();
}
