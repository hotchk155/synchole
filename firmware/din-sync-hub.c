////////////////////////////////////////////////////////////
//
// DIN SYNCH HUB
//
// Code for PIC12F1822
// Compiled with SourceBoost C
//
// hotchk155/2015
//
// Firmware version 
// 1.00 
//
////////////////////////////////////////////////////////////

#include <system.h>
#include <memory.h>

// 8MHz internal oscillator block, reset disabled
#pragma DATA _CONFIG1, _FOSC_INTOSC & _WDTE_OFF & _MCLRE_OFF &_CLKOUTEN_OFF
#pragma DATA _CONFIG2, _WRT_OFF & _PLLEN_OFF & _STVREN_ON & _BORV_19 & _LVP_OFF
#pragma CLOCK_FREQ 8000000
typedef unsigned char byte;

// define the I/O pins. Note that 
// PORTA.5 is the UART RX pin
// PORTA.3 is left as VPP only
#define P_LED1		lata.4
#define P_LED2		lata.2
#define P_RUN		lata.0
#define P_CLK		lata.1


// Timer settings
volatile byte timerTicked = 0;		// Timer ticked flag (tick once per ms)
#define TIMER_0_INIT_SCALAR		5	// Timer 0 is an 8 bit timer counting at 250kHz

#define CLOCK_HIGH_TIME 		5 // milliseconds
#define MIDILED_HIGH_TIME 		5 // milliseconds
#define BEATLED_HIGH_TIME 		10 // milliseconds

volatile byte bRunning = 0;
volatile byte bBeatCount = 0;
volatile byte bClockSignalCount = 0;
volatile byte bMidiLEDCount = 0;
volatile byte bBeatLEDCount = 0;
volatile unsigned millisSinceLastTick = 0;

////////////////////////////////////////////////////////////
// INTERRUPT HANDLER CALLED WHEN CHARACTER RECEIVED AT 
// SERIAL PORT
void interrupt( void )
{
	// TIMER 0 ROLLOVER (PER MS)
	if(intcon.2)
	{
		tmr0 = TIMER_0_INIT_SCALAR;
		++millisSinceLastTick;
		if(bClockSignalCount) {
			if(!--bClockSignalCount)
				P_CLK = 0;
		}
		timerTicked = 1;
		intcon.2 = 0;
	}
	// check if this is serial rx interrupt
	if(pir1.5)
	{

		// get the byte
		byte b = rcreg;
				
		switch(b)
		{
			case 0xf8:	// CLOCK
				P_CLK = 1;
				//if(millisSinceLastTick) { 
				//	bClockSignalCount = millisSinceLastTick/2;
				//}
				//else {
					bClockSignalCount = CLOCK_HIGH_TIME;
				//}
				tmr0 = TIMER_0_INIT_SCALAR;
				millisSinceLastTick = 0;
				bMidiLEDCount = MIDILED_HIGH_TIME;
				if(++bBeatCount == 24) {
					bBeatCount = 0;
					bBeatLEDCount = BEATLED_HIGH_TIME;
				}
				break;
			case 0xfa: // START 
			case 0xfb: // CONTINUE
				P_RUN = 1;
				bBeatCount = 0;
				bBeatLEDCount = BEATLED_HIGH_TIME;
				bRunning = 1;
				break;
			case 0xfc: // STOP
				P_RUN = 0;
				bRunning = 0;
				break;
		}				
	}
}

////////////////////////////////////////////////////////////
// INITIALISE SERIAL PORT FOR MIDI
void init_usart()
{
	pir1.1 = 1;		//TXIF 		
	pir1.5 = 0;		//RCIF
	
	pie1.1 = 0;		//TXIE 		no interrupts
	pie1.5 = 1;		//RCIE 		interrupt on receive
	
	baudcon.4 = 0;	// SCKP		synchronous bit polarity 
	baudcon.3 = 1;	// BRG16	enable 16 bit brg
	baudcon.1 = 0;	// WUE		wake up enable off
	baudcon.0 = 0;	// ABDEN	auto baud detect
		
	txsta.6 = 0;	// TX9		8 bit transmission
	txsta.5 = 0;	// TXEN		transmit enable
	txsta.4 = 0;	// SYNC		async mode
	txsta.3 = 0;	// SEDNB	break character
	txsta.2 = 0;	// BRGH		high baudrate 
	txsta.0 = 0;	// TX9D		bit 9

	rcsta.7 = 1;	// SPEN 	serial port enable
	rcsta.6 = 0;	// RX9 		8 bit operation
	rcsta.5 = 1;	// SREN 	enable receiver
	rcsta.4 = 1;	// CREN 	continuous receive enable
		
	spbrgh = 0;		// brg high byte
	spbrg = 15;		// brg low byte (31250)	
	
}

////////////////////////////////////////////////////////////
// ENTRY POINT
void main()
{ 
	// osc control / 8MHz / internal
	osccon = 0b01110010;
	
	// enable serial receive interrupt
	intcon.7 = 1; 
	intcon.6 = 1; 
	pie1.5 = 1;
	
	// Configure timer 0 (controls systemticks)
	// 	timer 0 runs at 4MHz
	// 	prescaled 1/16 = 250kHz
	// 	rollover at 250 = 1kHz
	// 	1ms per rollover	
	option_reg.5 = 0; // timer 0 driven from instruction cycle clock
	option_reg.3 = 0; // timer 0 is prescaled
	option_reg.2 = 0; // }
	option_reg.1 = 1; // } 1/16 prescaler
	option_reg.0 = 1; // }
	intcon.5 = 1; 	  // enabled timer 0 interrrupt
	intcon.2 = 0;     // clear interrupt fired flag	

	// configure io
	trisa = 0b00100000;              	
	ansela = 0b00000000;
	porta=0;

	apfcon.7=1; // RX on RA5
	apfcon.2=1;	// TX on RA4

	// startup flash
	P_LED1=1; delay_ms(200);
	P_LED1=0; delay_ms(200);
	P_LED1=1; delay_ms(200);
	P_LED1=0; delay_ms(200);
	P_LED1=1; delay_ms(200);
	P_LED1=0; delay_ms(200);

	// initialise USART
	init_usart();

	// loop forever		
	for(;;)
	{
		if(timerTicked) {
			timerTicked = 0;

//			if(bClockSignalCount)
			//{
				//if(!--bClockSignalCount) 
					//P_CLK = 0;
			//}

			P_LED1 = !!bMidiLEDCount;
			if(bRunning) {
				P_LED2= !!bBeatLEDCount;
			}
			else {
				P_LED2= 0;
			}
		
			if(bMidiLEDCount)
				--bMidiLEDCount;
			if(bBeatLEDCount)
				--bBeatLEDCount;
		}
	}
}