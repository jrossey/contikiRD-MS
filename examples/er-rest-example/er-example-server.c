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
 *      Erbium (Er) REST Engine example.
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "rest-engine.h"

#if PLATFORM_HAS_BUTTON
#include "dev/button-sensor.h"
#endif

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


#define RESOURCE_DIRECTORY 1
#if RESOURCE_DIRECTORY
#define REMOTE_PORT UIP_HTONS(COAP_DEFAULT_PORT) 
#define MAX_DATA_LEN 256
#define START_RD_REGISTRATION		3 /* number of seconds it takes after booting the sensor before sensor registers with mirror server */
#endif /* RESOURCE_DIRECTORY */

#include "er-coap-block1.h"
#include "er-coap-separate.h"
#include "er-coap-transactions.h"
#include "er-coap-engine.h"


/*
 * Resources to be activated need to be imported through the extern keyword.
 * The build system automatically compiles the resources in the corresponding sub-directory.
 */
extern resource_t
  res_hello,
  res_mirror,
  res_chunks,
  res_separate,
  res_push,
  res_event,
  res_sub,
  res_b1_sep_b2;

#if PLATFORM_HAS_LEDS
extern resource_t res_leds, res_toggle;
#endif
#if PLATFORM_HAS_LIGHT
#include "dev/light-sensor.h"
extern resource_t res_light;
#endif

#if RESOURCE_DIRECTORY
static int s_ResponseReceived;
static void *s_pResponse;  // Payload of the received acknowledge
static char LocationPath[20];
/* This function is will be passed to COAP_BLOCKING_REQUEST() to handle responses. */
static void client_chunk_handler(void *response)
{
  unsigned code=((coap_packet_t *)response)->code;

  s_ResponseReceived=(code>>5)*100+(code&0x1f);
  s_pResponse=response;
}
#endif /* RESOURCE_DIRECTORY */

PROCESS(er_example_server, "Resource Directory Example Server");
AUTOSTART_PROCESSES(&er_example_server);

PROCESS_THREAD(er_example_server, ev, data)
{
  PROCESS_BEGIN();
  //PROCESS_PAUSE();

#ifdef RF_CHANNEL
  PRINTF("RF channel: %u\n", RF_CHANNEL);
#endif
#ifdef IEEE802154_PANID
  PRINTF("PAN ID: 0x%04X\n", IEEE802154_PANID);
#endif

  PRINTF("uIP buffer: %u\n", UIP_BUFSIZE);
  PRINTF("LL header: %u\n", UIP_LLH_LEN);
  PRINTF("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  PRINTF("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  /* Initialize the REST engine. */
  rest_init_engine();

  /*
   * Bind the resources to their Uri-Path.
   * WARNING: Activating twice only means alternate path, not two instances!
   * All static variables are the same for each URI path.
   */
  rest_activate_resource(&res_hello, "test/hello");
/*  rest_activate_resource(&res_mirror, "debug/mirror"); */
/*  rest_activate_resource(&res_chunks, "test/chunks"); */
/*  rest_activate_resource(&res_separate, "test/separate"); */
  rest_activate_resource(&res_push, "test/push");
/*  rest_activate_resource(&res_event, "sensors/button"); */
/*  rest_activate_resource(&res_sub, "test/sub"); */
/*  rest_activate_resource(&res_b1_sep_b2, "test/b1sepb2"); */
#if PLATFORM_HAS_LEDS
/*  rest_activate_resource(&res_leds, "actuators/leds"); */
  rest_activate_resource(&res_toggle, "actuators/toggle");
#endif
#if PLATFORM_HAS_LIGHT
  rest_activate_resource(&res_light, "sensors/light"); 
  SENSORS_ACTIVATE(light_sensor);  
#endif


#if RESOURCE_DIRECTORY
	static struct etimer rd_registration_timer;
	etimer_set(&rd_registration_timer, START_RD_REGISTRATION * CLOCK_SECOND);
	static uip_ipaddr_t resource_directory_ipaddr;
   

	uip_ip6addr(&resource_directory_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0x0001);
	static char* rd_registration_url = "/rd";
	static char* rd_registration_ep[20];
  
	/* create endpoint uri_query based on rime address */
	uint8_t i;
	uint8_t ep_pos = 0;
	ep_pos += snprintf((char *)rd_registration_ep + ep_pos, 5, "ep=");
	for(i = 7; i < sizeof(linkaddr_node_addr.u8)-1; i++) {
		//rd_registration_ep[i] = rimeaddr_node_addr.u8[i];
		//PRINTF("ep_pos: %u\n",ep_pos);
		ep_pos += snprintf((char *)rd_registration_ep + ep_pos, 5, "%d.",linkaddr_node_addr.u8[i]);
	}
	ep_pos += snprintf((char *)rd_registration_ep + ep_pos, 5, "%d",linkaddr_node_addr.u8[i]);
#endif /* RESOURCE_DIRECTORY */

  /* Define application-specific events here. */
  	while(1) {
    		PROCESS_WAIT_EVENT();
#if PLATFORM_HAS_BUTTON
    		if(ev == sensors_event && data == &button_sensor) {
      			PRINTF("*******BUTTON*******\n");

      			/* Call the event_handler for this application-specific event. */
      			res_event.trigger();

      			/* Also call the separate response example handler. */
      			res_separate.resume();
    		}
#endif /* PLATFORM_HAS_BUTTON */
#if RESOURCE_DIRECTORY
    		if(etimer_expired(&rd_registration_timer)){ //register all resources with MS
	
			//first create string with well-known/core payload
			static char well_known_core_msg[MAX_DATA_LEN];
			uint16_t pos = 0;
			resource_t *resource = NULL;	

			for(resource = (resource_t *)list_head(rest_get_resources()); resource;resource = resource->next){
				if(pos>0){
					strcat(well_known_core_msg,",");
				}
				strcat(well_known_core_msg,"</");
				strcat(well_known_core_msg,resource->url);
				strcat(well_known_core_msg,">");
				if(resource->attributes[0]) {
      					strcat(well_known_core_msg,";");
      					strcat(well_known_core_msg,resource->attributes);
   				}
				pos++;
		
			}
			uint8_t well_known_core_len = strlen(well_known_core_msg);
			//PRINTF("strcat well-knowncore (%u): \n%s\n",well_known_core_len,well_known_core_msg);
			PRINTF("REST_MAX_CHUNK_SIZE: %u\n",REST_MAX_CHUNK_SIZE);
	
			static coap_packet_t coap_temp_packet[1];
			coap_init_message(coap_temp_packet, COAP_TYPE_CON, COAP_POST,0);
			coap_set_header_uri_path(coap_temp_packet, rd_registration_url);
			REST.set_header_content_type(coap_temp_packet, REST.type.APPLICATION_LINK_FORMAT);

			coap_set_header_uri_query(coap_temp_packet, rd_registration_ep);
			coap_set_payload(coap_temp_packet, well_known_core_msg, strlen(well_known_core_msg)); 
			
			COAP_BLOCKING_REQUEST(&resource_directory_ipaddr, REMOTE_PORT, coap_temp_packet,client_chunk_handler); 

			if (s_ResponseReceived==201){
    				const char *path;
    				int Length=coap_get_header_location_path(s_pResponse, &path);
    				memcpy(LocationPath, path, Length);
    				LocationPath[Length]=0;
    				printf("Location Path is %s\n", LocationPath);
  			}else{
    				printf("Registering on Resource Directory. Wrong or no response received %d\n",s_ResponseReceived);
    				PT_EXIT(process_pt);  /* We consider all other response codes an error, since we need the location path to send data to the mirror proxy */
  			}
		}
#endif /* RESOURCE_DIRECTORY */
  	}/* while (1) */
  	PROCESS_END();
}
