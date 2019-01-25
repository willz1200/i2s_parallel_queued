#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include "queued_i2s_parallel.h"

//Queue for dummy data used to block main loop when all buffers are full
QueueHandle_t main_data_queue;

#define bufferFrameSize 1024 //Number of 16-bit samples per buffer
#define bufferMemorySize (bufferFrameSize*2) //Becuase 2 bytes to store each 16-bit sample

//#define showDebugPulse

uint16_t *bufferToFill; //Pointer to buffer that is next to be filled

//This gets called from the I2S interrupt. (This only removes dummy data from queue to unblock main loop)
void IRAM_ATTR buffer_filler_fn(void *buf, int len, void *arg) {
	QueueHandle_t queueHanle=(QueueHandle_t)arg;
	portBASE_TYPE high_priority_task_awoken = 0;
	
	uint8_t dummy[1];

	//Check if theres a dummy byte in the queue, indecating the main loop has updated the buffer, if not set to 0
	if (xQueueReceiveFromISR(queueHanle, (void*)&dummy, &high_priority_task_awoken)==pdFALSE) {
		memset(buf, 0, bufferFrameSize*2); //Nothing in queue. Zero out data
	}

	//Wake thread blocking the queue
	if (high_priority_task_awoken == pdTRUE) { //Check if a context switch needs to be requested
		portYIELD_FROM_ISR();
	}
}

void main_init() {
	//Create data queue
	main_data_queue=xQueueCreate(1, 1);//bufferFrameSize*2
	//Initialize I2S parallel device.
	i2s_parallel_config_t i2scfg={
		.gpio_bus={2, 4, 5, 9, 10, 12, 13, 14, 15, 18, 19, 21, 22, 23, 
		#ifdef showDebugPulse
		-1, -1
		#else
		25, 26
		#endif
		},
		.bits=I2S_PARALLEL_BITS_16,
		.clkspeed_hz=3333333, //3.33 MHz
		.bufsz=bufferFrameSize*2,
		.refill_cb=buffer_filler_fn, //Function Called by I2S interrupt
		.refill_cb_arg=main_data_queue //Queue pointer
	};
	i2s_parallel_setup(&I2S1, &i2scfg);
	i2s_parallel_start(&I2S1);
}

void writeSample(uint16_t *buf, uint16_t data, uint16_t pos){
	buf[pos^1]=data;
}

void mainloop(void *arg) {
	while(1) {
		uint8_t dummy[1];
		
		for (uint16_t i=0;i<=0xFFFF;i++){
			//Send the data to the queue becuase we've filled the buffer
			if (i%bufferFrameSize == 0){
				#ifdef showDebugPulse
				gpio_set_level(25, 1);
				gpio_set_level(25, 0);
				#endif
				xQueueSend(main_data_queue, dummy, portMAX_DELAY); //Blocked when queue is full
			}
			//Fill the buffer here
		 	if (bufferToFill!=NULL){
		 		writeSample(bufferToFill, i, i%bufferFrameSize);
		 	}
		}
	}
}

int app_main(void) {
	#ifdef showDebugPulse
	gpio_set_direction(25, GPIO_MODE_OUTPUT);
	gpio_set_direction(26, GPIO_MODE_OUTPUT);
	#endif

	main_init();

	xTaskCreatePinnedToCore(mainloop, "mainloop", 1024*16, NULL, 7, NULL, 1); //Use core 1 for main loop, I2S interrupt on core 0

	return 0;
}

