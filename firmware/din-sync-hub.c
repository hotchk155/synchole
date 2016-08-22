////////////////////////////////////////////////////////////
//
//      /////                          //              //
//     //                             //              //   
//    //     //   //  /////    ///// //////   /////  //    ////
//    ////  //   //  //   // //     //   // //   // //   //   //
//      // //   //  //   // //     //   // //   // //   //////
//     // //   //  //   // //     //   // //   // //   // 
// /////  //////  //   //  ///// //   //  /////   ///  //////
//           //
//          //  MIDI TO DIN SYNCH24 HUB
//     /////    hotchk155/2016 - Sixty-four pixels ltd.
//              Code for PIC12F1822 - Compiled with SourceBoost C
//
// This work is licensed under the Creative Commons license
// Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)
// To view a copy of this license, please visit:
// https://creativecommons.org/licenses/by-nc/4.0/
//
// Full repository with hardware information:
// https://github.com/hotchk155/din-synch-hub
//
// Firmware version 
// 1 19Nov15 Initial Version
// 2 12Dec15 New PCB - output pins switched
// 3 14Aug16 Initial release version - switch added
//
////////////////////////////////////////////////////////////
#include <system.h>
#include <memory.h>

#define FIRMWARE_VERSION 3

// configuration words: 16MHz internal oscillator block, reset disabled
#pragma DATA _CONFIG1, _FOSC_INTOSC & _WDTE_OFF & _MCLRE_OFF & _CLKOUTEN_OFF
#pragma DATA _CONFIG2, _WRT_OFF & _PLLEN_OFF & _STVREN_ON & _BORV_19 & _LVP_OFF
#pragma CLOCK_FREQ 16000000
typedef unsigned char byte;

// define the I/O pins. Note that 
// PORTA.5 is the UART RX pin
// PORTA.3 is left as VPP only
#define P_LED1		lata.4
#define P_LED2		lata.2
#define P_RUN		lata.1
#define P_CLK		lata.0
#define P_SWITCH	porta.3

// Timer settings
volatile byte timerTicked = 0;		// Timer ticked flag (tick once per ms)
#define TIMER_0_INIT_SCALAR		5	// Timer 0 is an 8 bit timer counting at 250kHz
#define MIDILED_HIGH_TIME 		1 // milliseconds
#define BEATLED_HIGH_TIME 		30 // milliseconds

// The pulse width is supposed to be at 50% duty cycle (i.e. half the 
// clock pulse period. However if we don't have a previous MIDI clock
// tick we don't know the period, so we'll use a default pulse width
// of 5 milliseconds
#define DEFAULT_CLOCK_LENGTH_USECS 5000

// Switch debounce time
#define SWITCH_DEBOUNCE_MS	50

volatile byte bRunning = 0;				// clock running flag
volatile byte bBeatCount = 0;			// beat count (used to flash beat LED)	
volatile byte bMidiLEDCount = 0;		// ms before MIDI activity LED goes off
volatile byte bBeatLEDCount = 0;		// ms before beat LED goes off

////////////////////////////////////////////////////////////
// INTERRUPT HANDLER 
void interrupt( void )
{
	unsigned int usecsSinceLastClock;
	unsigned int usecsPulseLength;

	// TIMER0 OVERFLOW
	// Timer 0 overflow is used to 
	// create a once per millisecond
	// signal for blinking LEDs etc
	if(intcon.2)
	{
		tmr0 = TIMER_0_INIT_SCALAR;
		timerTicked = 1;
		intcon.2 = 0;
	}
	
	// COMPARE INTERRUPT
	// This we configure this interrupt to fire when Timer1 matches CCPR1H:CCPR1L.
	// This is our signal to end the output clock pulse (at 50% duty)
	if(pir1.2) 
	{	
		// drive output clock low
		P_CLK = 0;
		pir1.2 = 0;
	}
	
	// TIMER1 OVERFLOW
	// If Timer1 overflows this means that 
	// we will give up timing between MIDI
	// clock messages (very slow BPM) and 
	// will instead revert to default clock
	// pulse width
	if(pir1.0)
	{
		// drive the output clock low
		t1con.0 = 0; // stop the timer
		pir1.0 = 0;
	}
		
	// SERIAL PORT RECEIVE
	if(pir1.5)
	{

		// get the byte
		byte b = rcreg;
				
		switch(b)
		{
			/////////////////////////////////////////////////////////////
			// MIDI CLOCK
			case 0xf8:	
				P_CLK = 1;	// send clock line high

				// the clock was not started, or it overflowed, so 
				// we need to use a default pulse length
				if(!t1con.0) {
					usecsPulseLength = DEFAULT_CLOCK_LENGTH_USECS;
				} else {
					t1con.0 = 0; 
					// capture timer 1 value (microseconds since the last
					// MIDI tick)
					usecsSinceLastClock = (((unsigned int)tmr1h<<8)|tmr1l);
					
					// divide by 2 to get pulse width at 50% duty
					usecsPulseLength = usecsSinceLastClock / 2;
				}
										
				// Schedule the end of the clock pulse
				ccpr1h = usecsPulseLength>>8;
				ccpr1l = (byte)usecsPulseLength;
				
				// reset the timer 1 and start it
				tmr1l = 0;
				tmr1h = 0;	
				t1con.0 = 1; 

				// Ping the beat LED every 24 pulses
				if(++bBeatCount == 24) {
					bBeatCount = 0;
					bBeatLEDCount = BEATLED_HIGH_TIME;
				}
				
				// Indicate MIDI activity
				bMidiLEDCount = MIDILED_HIGH_TIME;
				break;

			/////////////////////////////////////////////////////////////
			// MIDI CLOCK START / CONTINUE
			case 0xfa: // start
			case 0xfb: // continue
				P_RUN = 1;
				bBeatCount = 0;
				bRunning = 1;
				// ping the beat LED for the first beat
				bBeatLEDCount = BEATLED_HIGH_TIME;
				break;

			/////////////////////////////////////////////////////////////
			// MIDI CLOCK STOP
			case 0xfc: 
				P_RUN = 0;
				bRunning = 0;
				break;
		}	
		pir1.5 = 0;			
	}
}

////////////////////////////////////////////////////////////
// INITIALISE SERIAL PORT FOR MIDI
void init_usart()
{
	pir1.1 = 1;		//TXIF 		
	pir1.5 = 0;		//RCIF
	
	pie1.1 = 0;		//TXIE 		no interrupts
	pie1.5 = 1;		//RCIE 		enable
	
	baudcon.4 = 0;	// SCKP		synchronous bit polarity 
	baudcon.3 = 1;	// BRG16	enable 16 bit brg
	baudcon.1 = 0;	// WUE		wake up enable off
	baudcon.0 = 0;	// ABDEN	auto baud detect
		
	txsta.6 = 0;	// TX9		8 bit transmission
	txsta.5 = 0;	// TXEN		transmit disable
	txsta.4 = 0;	// SYNC		async mode
	txsta.3 = 0;	// SEDNB	break character
	txsta.2 = 0;	// BRGH		high baudrate 
	txsta.0 = 0;	// TX9D		bit 9

	rcsta.7 = 1;	// SPEN 	serial port enable
	rcsta.6 = 0;	// RX9 		8 bit operation
	rcsta.5 = 1;	// SREN 	enable receiver
	rcsta.4 = 1;	// CREN 	continuous receive enable
		
	spbrgh = 0;		// brg high byte
	spbrg = 31;		// brg low byte (31250)		
}

////////////////////////////////////////////////////////////
// ENTRY POINT
void main()
{ 
	// osc control / 16MHz / internal
	osccon = 0b01111010;
		
	// configure io
	trisa = 0b00100000;              	
	ansela = 0b00000000;
	porta=0;

	apfcon.7=1; // RX on RA5
	apfcon.2=1;	// TX on RA4

	// enable serial receive interrupt
	intcon.7 = 1; //global interrupt enable
	intcon.6 = 1; // peripheral interrupt enable
	
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

	// configure Timer 1 (controls clock pulses)
	// to count at 1MHz and interrupt on overflow
	t1con.7 = 0; // }
	t1con.6 = 0; // } instruction clock source
	t1con.5 = 1; // }
	t1con.4 = 0; // } prescaler 1:4
	t1con.3 = 0; // 0 timer1 osc circuit disabled
	t1con.2 = 0; // t1 synch off
	t1con.1 = 0; // reserved	
	tmr1l = 0;	 // reset timer
	tmr1h = 0;
	pir1.0 = 0;  // clear timer interrupt flag
	pie1.0 = 1;  // Enable timer overflow interrupt
	t1con.0 = 1; // Timer1 starts disabled

	// Configure Compare module 1 to interrupt
	// on a match between tmr1h:tmr1l and ccpr1h:ccpr1l
	ccp1con.3 = 1; //	}
	ccp1con.2 = 0; //	} Generate software interrupt
	ccp1con.1 = 1; //	} from capture/compare module 1
	ccp1con.0 = 0; //	} when timer1 matches CCPR1H:CCPR1L
	pie1.2 = 1;    // enable interrupt 
			
	// Flash MIDI activity LED on startup
	P_LED1=1; delay_ms(200);
	P_LED1=0; delay_ms(200);
	P_LED1=1; delay_ms(200);
	P_LED1=0; 

	// initialise USART
	init_usart();

	int debounce = 0;
	// loop forever		
	for(;;)
	{
		// once per ms this flag is set...
		if(timerTicked) {
			timerTicked = 0;

			// refresh LEDs
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
			
			// is the switch pressed?	
			if(debounce >= 0) {
				if(debounce > 0) {				
					// we are debouncing the switch
					--debounce;
				}
				else if(P_SWITCH) {
					// debounce period is over - just waiting for
					// the switch to be released
					debounce = -1;
				}
			}
			else if(!P_SWITCH) {			
				// new switch press
				if(bRunning) {
					P_RUN = 0;					
					bRunning = 0;
				}
				else {
					P_RUN = 1;					
					bRunning = 1;
					bBeatCount = 0;
					bBeatLEDCount = BEATLED_HIGH_TIME;
				}
				debounce = SWITCH_DEBOUNCE_MS;				
			}
		}
	}
}