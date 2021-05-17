/**
 * @file       main.cpp
 * @author     Volodymyr Shymanskyy
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2015 Volodymyr Shymanskyy
 * @date       Mar 2015
 * @brief
 */
 
//#define BLYNK_DEBUG
#define BLYNK_PRINT stdout
#ifdef RASPBERRY
  #include <BlynkApiWiringPi.h>
#else
  #include <BlynkApiLinux.h>
#endif
#include <BlynkSocket.h>
#include <BlynkOptionsParser.h>
 
static BlynkTransportSocket _blynkTransport;
BlynkSocket Blynk(_blynkTransport);
 
static const char *auth, *serv;
static uint16_t port;
 
#include <BlynkWidgets.h>
#include "main.h"

using namespace std;

unsigned char buffer[3][3];
long lastInt= 0;

float tempVal, humidVal;
int lightVal;
bool aboveThres = false;

pthread_attr_t tattr;
pthread_t tid;
int newprio = 99;
sched_param param;

//
bool monitoring = false;

int HH,MM,SS;
int hour,minute,second = 0;
int startH,startM,startS;
int interval = 1;
int lastAlarm =0;
//
char alarmC = 32;
char labels[80] = "RTC Time       |Sys Timer     |Humidity   |Temp   |Light   |DAC out   |Alarm |";
char line [80] =  "---------------+--------------+-----------+-------+--------+----------+------+";
/*==============================================
 General set up
==============================================*/
void setup(){

	Blynk.begin(auth, serv, port);
	//Buttons
	pinMode(START, INPUT);
	pinMode(INTERVAL,INPUT);
	pinMode(RESET, INPUT);
	pinMode(ALARM_OFF,INPUT);

	pinMode(ALARM_LED, OUTPUT);
	pinMode(MONITOR_LED, OUTPUT);
	softToneCreate(BUZZER);

	pullUpDnControl(START, PUD_UP);
	pullUpDnControl(INTERVAL, PUD_UP);
	pullUpDnControl(RESET, PUD_UP);
	pullUpDnControl(ALARM_OFF, PUD_UP);

  wiringPiISR(START, INT_EDGE_FALLING,  &start_isr);
  wiringPiISR(INTERVAL, INT_EDGE_FALLING, &changeInterval_isr);
  wiringPiISR(RESET, INT_EDGE_FALLING,&reset_time_isr);
  wiringPiISR(ALARM_OFF, INT_EDGE_FALLING,&dismissAlarm_isr );


}

/*==============================================
 RTC
==============================================*/
int getCurrentTime(void){
	time_t s = 1;
	struct tm* current_time;

	// time in seconds
	s = time(NULL);

	// to get current time
	current_time = localtime(&s);

	HH = current_time->tm_hour;
	MM = current_time->tm_min;
	SS = current_time->tm_sec;

	return 0;
}

void getSystemTime(void){

	if (second + interval <= 59){
		second += interval;
	}

	else{
		second = 0;
		minute ++;
	}

	if (minute == 59){
		minute = 0;
		hour ++;
	}
	if (hour == 24){
		hour = 0;
	}
}


/*==============================================
 ADC & DAC
==============================================*/

int setup_SPI(){

	wiringPiSPISetup(ADC_CHANNEL, SPI_SPEED);//ADC
	wiringPiSPISetup(DAC_CHANNEL, SPI_SPEED);//DAC

	cout << "Done SPI setup" << endl;

	return 0;
}

void *read_ADC(void *threadargs){
	sleep(0.02);
	while(true)
	{while(! monitoring){
			sleep(0.02);
		}
	
		while(monitoring){// stop monitoring
	
			for(int i=0;i<3;i++){
				buffer[i][0]= startADC;
			}
	
			buffer[0][1] = temperature;
			buffer[1][1] = humidity;
			buffer[2][1] = light;
	
			wiringPiSPIDataRW(ADC_CHANNEL,buffer[0],3);
			wiringPiSPIDataRW(ADC_CHANNEL,buffer[1],3);
			wiringPiSPIDataRW(ADC_CHANNEL,buffer[2],3);
	
			int temp_reading = ((buffer[0][1] & 3 ) << 8 ) + buffer[0][2];
			int humid_reading = ((buffer[1][1] & 3 ) << 8 ) + buffer[1][2];
			int light_reading = ((buffer[2][1] & 3 ) << 8 ) + buffer[2][2];
	
			tempVal = (temp_reading*3.3) / float(1023);
			humidVal = (humid_reading*3.3) / float(1023);
			lightVal = light_reading;
	
		}
		//pthread_exit(NULL);}
}

void outputVoltage(float voltage){

	char  v = (char)(voltage);

	unsigned char value [2];

	value [0] = 0b01110000 | (v >> 6);
	value [1] = (v<<2);

	wiringPiSPIDataRW(DAC_CHANNEL, value, 2);
}

void alarmOn(void){
	digitalWrite(ALARM_LED,1);
	//digitalWrite(BUZZER,1);
	alarmC = 32;
}

/*==============================================
 Interrupts
 1. dismiss alarm
 2. change reading interval
 3. reset system time
 4. start/stop monitoring
==============================================*/
void dismissAlarm_isr(void){
	long interruptTime = millis();
	if (interruptTime - lastInt>200){
		digitalWrite(ALARM_LED,0);
		softToneWrite(BUZZER, 0);
		alarmC  =32;
	}
	lastInt = interruptTime;
}

void changeInterval_isr(void){
	long interruptTime = millis();
	if (interruptTime - lastInt>200){
		if (interval == 1){
			interval = 2;
		}
		else if (interval == 2){
			interval = 5;
		}
		else if (interval == 5){
			interval = 1;
		}
	}
	lastInt = interruptTime;
}

void reset_time_isr(void){
	long interruptTime = millis();
	if (interruptTime - lastInt>200){
		hour = 0;
		minute = 0;
		second =	0;

		std::system("clear");
		printf("%s\n", labels);
		printf("%s\n", line);

	}
	lastInt = interruptTime;
}

void start_isr(void){
	unsigned long interruptTime = millis();
	if (interruptTime - lastInt>200){
		if (!monitoring){
			monitoring = true;
			digitalWrite(MONITOR_LED,1);
		}
		else{
			monitoring = false;
			digitalWrite(MONITOR_LED,0);
		}
	}
	lastInt = interruptTime;
}
/*==============================================
 main
==============================================*/

int main(void){

	signal(SIGINT,safeExit);
	setup();
	setup_SPI();

	if (!monitoring){
		printf("%s\n", "*****PUSH START*****" );
		while (!monitoring){
			sleep(1);
		}
	}

	printf("%s\n", "--------------------------------SYSTEM ON----------------------------" );

	printf("%s\n", labels);
	printf("%s\n", line);

	//
	pthread_attr_init (&tattr);
	pthread_attr_getschedparam (&tattr, &param);
	param.sched_priority = newprio;
	pthread_attr_setschedparam (&tattr, &param);
	pthread_create (&tid, &tattr, read_ADC, (void *)1);

	for (;;){
		getCurrentTime();
		if (monitoring){

			int ambient =  (tempVal - ZERO_VOLTAGE)/TEMP_COEFF;

			Blynk.virtualWrite(V0, ambient);
			Blynk.virtualWrite(V1, lightVal);
			Blynk.virtualWrite(V2, humidVal);
			
			float dacVal = (lightVal*humidVal)/float(1023);
			
			Blynk.virtualWrite(V3, dacVal);
			if (aboveThres && (minute - lastAlarm > 3){
					aboveThres = false;
			}

			if (dacVal > UPPER_THRESHOLD || dacVal < LOWER_THRESHOLD){
				if (!aboveThres){
					alarmC = 42;
					lastAlarm = minute;
					alarmOn();
					Blynk.notify("Warning! Output voltage out of bounds");
					aboveThres = true;
				}				
			}

			outputVoltage(dacVal);

			printf("%i:%i:%-7i ", HH, MM, SS);
			printf(" |%i:%i:%-8i ", hour, minute, second);
			printf(" |%.2f %-5c |%i %-3c |%-7i |%.2f%-5c |%-5c |\n", humidVal, 86, ambient, 67,lightVal, dacVal, 86, alarmC);

			char buffer[80];
			int n = sprintf(buffer, " |%i:%i:%-8i ", hour, minute, second);
			Blynk.virtualWrite(V4,buffer);
			}
			else{
				printf("%i:%i:%-7i ", HH, MM, SS);
				printf(" |%i:%i:%-8i ", hour, minute, second);
				printf(" |%-11c|%-7c|%-8c|%-10c|%-5c |\n",32,32,32,32,32);
				Blynk.virtualWrite(V0, 0);
				Blynk.virtualWrite(V1, 0);
				Blynk.virtualWrite(V2, 0);
				Blynk.virtualWrite(V3, 0);
				char buffer[80];
				int n = sprintf(buffer, " |%i:%i:%-8i ", hour, minute, second);
				Blynk.virtualWrite(V4,buffer);
		}
		getSystemTime();
		sleep(interval);
	}
	pthread_join(tid, NULL);
	pthread_exit(NULL);

	return 0;
}

void cleanup(void){

	digitalWrite(MONITOR_LED,0);

	pinMode(ALARM_LED,OUTPUT);
	digitalWrite(ALARM_LED,0);

}

void safeExit(int sig){
	// pthread_join(tid, NULL);// this line causes issues
	// pthread_exit(NULL);
	cleanup();
	printf("%s\n","-----------------------------SYSTEM OFF------------------------" );
	exit(sig);
}

/*==============================================
 TODO list
3. get PWM to work correctly
4. actually output from the DAC
==============================================*/