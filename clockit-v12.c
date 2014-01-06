// Clockit SparkFun Alarm Clock Kit
// <clockit.c> Nathan Seidle 03/04/2009
// 
// Description:
// Basic Alarm Clock using 4-digit 7-segment display and ATmega328P micro-controller
//  -Functions: Display: time HH:MM, AM/PM, seconds => colon blink, alarm ON/OFF
//              Alarm: SET/ON/OFF/+9M Snooze, BUZZER

// revision history:
// 03/04/2009 Nathan Seidle <ns> clockit.c 
//            v?? ns  original code Copyright Spark Fun Electronics© 2009
// 02/24/2011 Jim Lindblom <jl>
//            v11 jl  modified for new common anode 7-segment 4-digit display
// 12/28/2013 Mark D. Shattuck <mds> 
//            v12 mds Update Makefile for AVRmega328P
//                    Change:
//                    -SIG_OVERFLOW1 => TIMER1_OVF_vect
//                    -SIG_OVERFLOW2 => TIMER2_OVF_vect
//                    Add new description
//                    Fix apostrophe cathode: Should be PC1 not PC2.
//                    Fix clear_display()
//                    -remove amMark/2 (not needed with correct clear_display)
//                    -cleaned up display_number() 
//					  Softened turn on buzz.
// 12/29/2013 v12 mds Changed Timer1 to CTC mode (WGM 12) since setting TCNT1 during 
//                    ISR loses a few clock cycles. Small effect ~10*1/16e6=0.625us
//                    per interrupt ~ 1s / month
	
// Detailed Description:
// Basic Alarm Clock using the Atmel 8-bit ATmega328P micro-controller and a common 
// anode 7-segment 4-digit LED panel.  Hardware is from SparkFun Clockit KIT-10930 
// (https://www.sparkfun.com/products/10930 ). The timebase is set by an external
// 16MHz crystal oscillator.  Three push buttons (UP/DOWN/SNOOZE) and one switch 
// (ALARM ON/OFF) control operations.  Time is indicated in 12-hour format HH:MM 
// using a dot (apostrophe) to indicate AM/PM.  Seconds are displayed by blinking 
// the colon.  Alarm set is controlled by the switch and indicated by the decimal 
// point on digit 4.  A piezo-electric buzzer sounds the alarm and the snooze 
// button silences the sound for 9 minutes.  A CLOCK SET mode is entered by pressing 
// and holding both the UP and DOWN buttons.  In CLOCK SET mode the UP and DOWN 
// buttons advance or decrease the time with accelerating rate as the button is 
// held.  Pressing SNOOZE ends CLOCK SET mode.  An ALARM SET mode is entered by 
// pressing and holding the SNOOZE button.  In ALARM SET mode the alarm time is
// displayed and the UP and DOWN buttons advance or decrease the time with
// accelerating rate as the button is held.  Pressing SNOOZE ends ALARM SET 
// mode.        

// Theory of Operation:
// 1) Three timers are used to generate interrupts to control the clock.
// -Timer0 is used in normal mode to create us delays.
// -Timer1 is used to determine the time.  In CTC mode (WGM mode 12) the timer counts
//  up to ICR1=15625 and generates a capture (TIMER1_CAPT_vect) interrupt.  The clock 
//  frequency is 16MHz.  The pre-scaler is set 1024, so each count is 1024/16=64us.
//  Therefore, 1s/64us = 15625 counts each second. Timer1's ISR updates the time 
//  HH:MM:SS and AM/PM.
// -Timer2 is used in normal mode to update the display every 256*64us = 16.384ms.
// 2) A form of pulse-width-modulation PWM is used to drive the display without the 
// need for limiting resistors.  However, it is possible to burn out the display if 
// the elements are left on too long.  (See display_time function for more details).
// 3) The alarm condition and the three buttons are polled using the function 
// check_alarm and check_button in main.

// Hardware:
// AVRmega328P with 7-segment 4-digit display [YSD-439AB4B-35]
//
//               -----------------------------
//               |        AVR ATmega328P     |   
//               |---------------------------|
//         RESET-| 1  RESET           PC5 28 |-B
//          DIG1-| 2  PD0             PC4 27 |-G   
//          DIG2-| 3  PD1             PC3 26 |-A      
//             D-| 4  PD2             PC2 25 |-C+COL-C
//         COL-A-| 5  PD3             PC1 24 |-F+APOS-C
//          DIG3-| 6  PD4             PC0 23 |-E  
//           VCC-| 7  VCC            AGND 22 |-GND   
//           GND-| 8  GND            AREF 21 |-VCC   
//   16.000MHz-1-| 9  XTAL1          AVCC 20 |-VCC    
//   16.000MHz-2-| 10 XTAL2      [SCK]PB5 19 |-BTN1(UP)        
//            DP-| 11 PD5       [MISO]PB4 18 |-BTN2(DOWN)         
//          DIG4-| 12 PD6       [MOSI]PB3 17 |-APOS-A      
//  (SNOOZE)BTN3-| 13 PD7             PB2 16 |-BUZZ-2   
//     (ALARM)S1-| 14 PB0             PB1 15 |-BUZZ-1
//               -----------------------------
//         
//
//               ----------------------
//               |  4-digit 7-segment |   
//               |    Common Anode    |   
//               |--------------------|
//           PD0-| 1  DIG1       B 16 |-PC5
//           PD1-| 2  DIG2       G 15 |-PC4
//           PD2-| 3  D          A 14 |-PC3   
//           PD3-| 4  COL-A      C 13 |-PC2
//           PC0-| 5  E      COL-C 12 |-PC2       
//           PD4-| 6  DIG3       F 11 |-PC1   
//           PD5-| 7  DP    APOS-A 10 |-PB3      
//           PD6-| 8  DIG4  APOS-C  9 |-PC1
//               ----------------------
//         
//
//               ----------------------
//               | 6-PIN SPI Program  |   
//               |--------------------|
//        [MISO]-| 1  MISO      VCC 2 |-VCC
//         [SCK]-| 3  SCK      MOSI 4 |-[MOSI]
//         RESET-| 5  RESET     GND 6 |-GND   
//               ----------------------
              

/*  Original Description
	2-24-11
	by Jim Lindblom
	Layout was modified to use a new LED, used in v14 of the layout.
	New LED is common-anode, rather than common-cathode.
	
    3-4-09
    Copyright Spark Fun Electronics© 2009
    Nathan Seidle
	
	A basic alarm clock that uses a 4 digit 7-segment display. Includes alarm and snooze.
	Alarm will turn back on after 9 minutes if alarm is not disengaged. 
	
	Alarm is through a piezo buzzer.
	Three input buttons (up/down/snooze)
	1 slide switch (engage/disengage alarm)
	
	Display is with PWM of segments - no current limiting resistors!
	
	Uses external 16MHz clock as time base.
	
	Set fuses:
	avrdude -p m168 -P lpt1 -c stk200 -U lfuse:w:0xE6:m
	
	program hex:
	avrdude -p m168 -P lpt1 -c stk200 -U flash:w:clockit-v10.hex

*/

#define NORMAL_TIME
//#define DEBUG_TIME

#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define sbi(port, pin)   ((port) |= (uint8_t)(1 << pin))
#define cbi(port, pin)   ((port) &= (uint8_t)~(1 << pin))

#define FOSC 16000000 //16MHz internal osc
//#define FOSC 1000000 //1MHz internal osc
//#define BAUD 9600
//#define MYUBRR (((((FOSC * 10) / (16L * BAUD)) + 5) / 10) - 1)

#define STATUS_LED	5 //PORTB

#define TRUE	1
#define FALSE	0

// Common anodes
#define DIG_1	PORTD0
#define DIG_2	PORTD1
#define DIG_3	PORTD4
#define DIG_4	PORTD6
#define COL		PORTD3
#define AMPM	PORTB3

// Cathodes
#define SEG_A	PORTC3
#define SEG_B	PORTC5
#define SEG_C	PORTC2
#define SEG_D	PORTD2
#define SEG_E	PORTC0
#define SEG_F	PORTC1

#define DP		PORTD5
#define COL_C	PORTC2
#define AMPM_C	PORTC1

// Buttons
#define BUT_UP		PORTB5
#define BUT_DOWN	PORTB4
#define BUT_SNOOZE	PORTD7
#define BUT_ALARM	PORTB0

#define BUZZ1	PORTB1
#define BUZZ2	PORTB2

#define AM	1
#define PM	2

//Declare functions
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void ioinit (void);
void delay_ms(uint16_t x); // general purpose delay
void delay_us(uint16_t x);

void siren(int duration);
void display_number(uint8_t number, uint8_t digit);
void display_time(uint16_t time_on);
void display_alarm_time(uint16_t time_on);
void clear_display(void);
void check_buttons(void);
void check_alarm(void);
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Declare global variables
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
uint8_t hours, minutes, seconds, ampm, flip;
uint8_t hours_alarm, minutes_alarm, seconds_alarm, ampm_alarm, flip_alarm;
uint8_t hours_alarm_snooze, minutes_alarm_snooze, seconds_alarm_snooze, ampm_alarm_snooze;//, flip_alarm;

uint8_t alarm_going;
uint8_t snooze;
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

ISR (TIMER1_CAPT_vect) 
{
	//Prescalar of 1024
	//Clock = 16MHz
	//15,625 clicks per second
	//64us per click	
    
	//<mds> Changed to CTC mode 
    //Setting TCNT1 loses a few clock cycle 
	//TCNT1 = 49911; //65536 - 15,625 = 49,911 - Preload timer 1 for 49,911 clicks. Should be 1s per ISR call

	//Debug with faster time!
    //TCNT1 = 63581; //65536 - 1,953 = 63581 - Preload timer 1 for 63581 clicks. Should be 0.125s per ISR call - 8 times faster than normal time
	
	flip_alarm = 1;
	
	if(flip == 0)
		flip = 1;
	else
		flip = 0;
		
	seconds++;
	if(seconds == 60)
	{
		seconds = 0;
		minutes++;
		if(minutes == 60)
		{
			minutes = 0;
			hours++;

			if(hours == 12)
			{
				if(ampm == AM)
					ampm = PM;
				else
					ampm = AM;
			}

			if(hours == 13) hours = 1;
		}
	}
}

ISR (TIMER2_OVF_vect) 
{
	display_time(10); //Display current time for 1ms
}


int main (void)
{
	ioinit(); //Boot up defaults
	
	while(1)
	{
		check_buttons(); //See if we need to set the time or snooze
		check_alarm(); //See if the current time is equal to the alarm time
	}
	
    return(0);
}

//Check to see if the time is equal to the alarm time
void check_alarm(void)
{
	//Check wether the alarm slide switch is on or off
	if( (PINB & (1<<BUT_ALARM)) != 0)
	{
		if (alarm_going == FALSE)
		{
			//Check to see if the time equals the alarm time
			if( (hours == hours_alarm) && (minutes == minutes_alarm) && (seconds == seconds_alarm) && (ampm == ampm_alarm) && (snooze == FALSE) )
			{
				//Set it off!
				alarm_going = TRUE;
			}

			//Check to see if we need to set off the alarm again after a ~9 minute snooze
			if( (hours == hours_alarm_snooze) && (minutes == minutes_alarm_snooze) && (seconds == seconds_alarm_snooze) && (ampm == ampm_alarm_snooze) && (snooze == TRUE) )
			{
				//Set it off!
				alarm_going = TRUE;
			}
		}
	}
	else
		alarm_going = FALSE;
}

//Checks buttons for system settings
void check_buttons(void)
{
	uint8_t i;
	uint8_t sling_shot = 0;
	uint8_t minute_change = 1;
	uint8_t previous_button = 0;
	
	//If the user hits snooze while alarm is going off, record time so that we can set off alarm again in 9 minutes
	if ( (PIND & (1<<BUT_SNOOZE)) == 0 && alarm_going == TRUE)
	{
		alarm_going = FALSE; //Turn off alarm
		snooze = TRUE; //But remember that we are in snooze mode, alarm needs to go off again in a few minutes
		
		seconds_alarm_snooze = 0;
		minutes_alarm_snooze = minutes + 9; //Snooze to 9 minutes from now
		hours_alarm_snooze = hours;
		ampm_alarm_snooze = ampm;
		
		if(minutes_alarm_snooze > 59)
		{
			minutes_alarm_snooze -= 60;
			hours_alarm_snooze++;

			if(hours_alarm_snooze == 12)
			{
				if(ampm_alarm_snooze == AM) 
					ampm_alarm_snooze = PM;
				else
					ampm_alarm_snooze = AM;
			}

			if(hours_alarm_snooze == 13) hours_alarm_snooze = 1;
		}
		
	}

	//Check for set time
	if ( (PINB & ((1<<BUT_UP)|(1<<BUT_DOWN))) == 0)
	{
		delay_ms(1000);

		if ( (PINB & ((1<<BUT_UP)|(1<<BUT_DOWN))) == 0)
		{
			//You've been holding up and down for 2 seconds
			//Set time!

			//siren(500); //Make some noise to show that you're setting the time

			while( (PINB & ((1<<BUT_UP)|(1<<BUT_DOWN))) == 0) //Wait for you to stop pressing the buttons
				display_time(1000); //Display current time for 1000ms

			while(1)
			{
				if ( (PIND & (1<<BUT_SNOOZE)) == 0) //All done!
				{
					for(i = 0 ; i < 3 ; i++)
					{
						display_time(250); //Display current time for 100ms
						clear_display();
						delay_ms(250);
					}
					
					while((PIND & (1<<BUT_SNOOZE)) == 0) ; //Wait for you to release button
					
					break; 
				}

				if ( (PINB & (1<<BUT_UP)) == 0)
				{
					//Ramp minutes faster if we are holding the button
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
					if(previous_button == BUT_UP) 
						sling_shot++;
					else
					{	
						sling_shot = 0;
						minute_change = 1;
					}
						
					previous_button = BUT_UP;
					
					if (sling_shot > 5)
					{
						minute_change++;
						if(minute_change > 30) minute_change = 30;
						sling_shot = 0;
					}
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
					
					minutes += minute_change;
					if (minutes > 59)
					{
						minutes -= 60;
						hours++;

						if(hours == 13) hours = 1;

						if(hours == 12)
						{
							if(ampm == AM) 
								ampm = PM;
							else
								ampm = AM;
						}
					}
					delay_ms(100);
				}
				
				if ( (PINB & (1<<BUT_DOWN)) == 0)
				{
					//Ramp minutes faster if we are holding the button
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
					if(previous_button == BUT_DOWN) 
						sling_shot++;
					else
					{
						sling_shot = 0;
						minute_change = 1;
					}
						
					previous_button = BUT_DOWN;
					
					if (sling_shot > 5)
					{
						minute_change++;
						if(minute_change > 30) minute_change = 30;
						sling_shot = 0;
					}
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=


					minutes -= minute_change;
					if(minutes > 60)
					{
						minutes = 59;
						hours--;
						if(hours == 0) hours = 12;

						if(hours == 11)
						{
							if(ampm == AM) 
								ampm = PM;
							else
								ampm = AM;
						}
					}
					delay_ms(100);
				}
				
				//clear_display(); //Blink display
				//delay_ms(100);
			}
		}
	}


	//Check for set alarm
	if ( (PIND & (1<<BUT_SNOOZE)) == 0)
	{
		TIMSK2 = 0;
		display_alarm_time(1000);

		if ( (PIND & (1<<BUT_SNOOZE)) == 0)
		{
			//You've been holding snooze for 2 seconds
			//Set alarm time!

			//Disable the regular display clock interrupt
			TIMSK2 = 0;

			while( (PIND & (1<<BUT_SNOOZE)) == 0) //Wait for you to stop pressing the buttons
			{
				clear_display();
				delay_ms(250);

				display_alarm_time(250); //Display current time for 1000ms
			}

			while(1)
			{
				display_alarm_time(100); //Display current time for 100ms
				
				if ( (PIND & (1<<BUT_SNOOZE)) == 0) //All done!
				{
					for(i = 0 ; i < 4 ; i++)
					{
						display_alarm_time(250); //Display current time for 100ms
						clear_display();
						delay_ms(250);
					}
					
					while((PIND & (1<<BUT_SNOOZE)) == 0) ; //Wait for you to release button
					
					TIMSK2 = (1<<TOIE2); //Re-enable the timer 2 interrupt
					
					break; 
				}

				if ( (PINB & (1<<BUT_UP)) == 0)
				{
					//Ramp minutes faster if we are holding the button
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
					if(previous_button == BUT_UP) 
						sling_shot++;
					else
					{	
						sling_shot = 0;
						minute_change = 1;
					}
						
					previous_button = BUT_UP;
					
					if (sling_shot > 5)
					{
						minute_change++;
						if(minute_change > 30) minute_change = 30;
						sling_shot = 0;
					}
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

					minutes_alarm += minute_change;
					if (minutes_alarm > 59)
					{
						minutes_alarm -= 60;
						hours_alarm++;
						if(hours_alarm == 13) hours_alarm = 1;

						if(hours_alarm == 12)
						{
							if(ampm_alarm == AM) 
								ampm_alarm = PM;
							else
								ampm_alarm = AM;
						}
					}
					//delay_ms(100);
				}
				
				if ( (PINB & (1<<BUT_DOWN)) == 0)
				{
					//Ramp minutes faster if we are holding the button
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
					if(previous_button == BUT_DOWN) 
						sling_shot++;
					else
					{	
						sling_shot = 0;
						minute_change = 1;
					}
						
					previous_button = BUT_DOWN;
					
					if (sling_shot > 5)
					{
						minute_change++;
						if(minute_change > 30) minute_change = 30;
						sling_shot = 0;
					}
					//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

					minutes_alarm -= minute_change;
					if(minutes_alarm > 60)
					{
						minutes_alarm = 59;
						hours_alarm--;
						if(hours_alarm == 0) hours_alarm = 12;

						if(hours_alarm == 11)
						{
							if(ampm_alarm == AM) 
								ampm_alarm = PM;
							else
								ampm_alarm = AM;
						}
					}
					//delay_ms(100);
				}
				
				//clear_display(); //Blink display
				//delay_ms(100);
			}
		}
		else
			TIMSK2 = (1<<TOIE2); //Re-enable the timer 2 interrupt

	}

}

void clear_display(void)
{
	cbi(PORTB, AMPM); // AMPM anode off=0
	PORTC = 0b00111111;  // Set BGACFE cathode off=1
	PORTD &= 0b10100100; // Set DIG4,DIG3,COL,DIG2,DIG1 anodes off=0 PD752=NC=1
	PORTD |= 0b00100100; // Set DP,D cathode off=1 PD764310=NC=0
}

void display_number(uint8_t number, uint8_t digit)
{
	clear_display();
	
	//Set the digit/colon/ampm (common anode)
	switch(digit)
	{
		case 1:
			PORTD |= (1<<DIG_1);//Select Digit 1
			break;
		case 2:
			PORTD |= (1<<DIG_2);//Select Digit 2
			break;
		case 3:
			PORTD |= (1<<DIG_3);//Select Digit 3
			break;
		case 4:
			PORTD |= (1<<DIG_4);//Select Digit 4
			break;
		case 5:
			PORTD |= (1<<COL);//Select Digit COL
			break;
		case 6:
			sbi(PORTB, AMPM); // AMPM anode on=1
		default: 
			break;
	}
	
	//Set number cathode
	switch(number)
	{
		case 0:
			PORTC &= 0b11010000; //Segments ABCEF
			PORTD &= 0b11111011; //Segments D
			break;
		case 1:
			PORTC &= 0b11011011; //Segments BC
			break;
		case 2:
			PORTC &= 0b11000110; //Segments ABEG
			PORTD &= 0b11111011; //Segments D
			break;
		case 3:
			PORTC &= 0b11000011; //Segments ABCG
			PORTD &= 0b11111011; //Segments D
			break;
		case 4:
			PORTC &= 0b11001001; //Segments BCGF
			break;
		case 5:
			PORTC &= 0b11100001; //Segments ACFG
			PORTD &= 0b11111011; //Segments D
			break;
		case 6:
			PORTC &= 0b11100000; //Segments AFGCE
			PORTD &= 0b11111011; //Segments D
			break;
		case 7:
			PORTC &= 0b11010011; //Segments ABC
			break;
		case 8:
			PORTC &= 0b11000000; //Segments ABCEFG
			PORTD &= 0b11111011; //Segments D
			break;
		case 9:
			PORTC &= 0b11000001; //Segments ABCFG
			PORTD &= 0b11111011; //Segments D
			break;

		case 10:
			//Colon
			PORTC &= 0b11111011; //Segments [C] 
			break;

		case 11:
			//Alarm dot
			PORTD &= 0b11011111; //Segments DP
			break;

		case 12:
			//AMPM dot
			PORTC &= 0b11111101; //Segments [F]
			break;

		default: 
			break;
	}
	
}

//Displays current time
//Brightness level is an amount of time the LEDs will be in - 200us is pretty dim but visible.
//Amount of time during display is around : [ BRIGHT_LEVEL(us) * 5 + 10ms ] * 10
//Roughly 11ms * 10 = 110ms
//Time on is in (ms)
void display_time(uint16_t time_on)
{
	//uint16_t bright_level = 1000;
	uint16_t bright_level = 50;
	//uint16_t bright_level = 100;
	
	//time_on /= 11; //Take the time_on and adjust it for the time it takes to run the display loop below
	
	for(uint16_t j = 0 ; j < time_on ; j++)
	{

#ifdef NORMAL_TIME
		//Display normal hh:mm time
		if(hours > 9)
		{
			display_number(hours / 10, 1); //Post to digit 1
			delay_us(bright_level);
		}

		display_number(hours % 10, 2); //Post to digit 2
		delay_us(bright_level);

		display_number(minutes / 10, 3); //Post to digit 3
		delay_us(bright_level);

		display_number(minutes % 10, 4); //Post to digit 4
		delay_us(bright_level);
#else
		//During debug, display mm:ss
		display_number(minutes / 10, 1); 
		delay_us(bright_level);

		display_number(minutes % 10, 2); 
		delay_us(bright_level);

		display_number(seconds / 10, 3); 
		delay_us(bright_level);

		display_number(seconds % 10, 4); 
		delay_us(bright_level);
#endif
		
		//Flash colon for each second
		if(flip == 1) 
		{
			display_number(10, 5); //Post to digit COL
			delay_us(bright_level);
		}
		
		//Indicate wether the alarm is on or off
		if( (PINB & (1<<BUT_ALARM)) != 0)
		{
			display_number(11, 4); //Turn on dot on digit 4
			delay_us(bright_level);

			//If the alarm slide is on, and alarm_going is true, make noise!
			if(alarm_going == TRUE && flip_alarm == 1)
			{
				clear_display();
				siren(500);
				flip_alarm = 0;
			}
		}
		else
		{
			snooze = FALSE; //If the alarm switch is turned off, this resets the ~9 minute addtional snooze timer
			
			hours_alarm_snooze = 88; //Set these values high, so that normal time cannot hit the snooze time accidentally
			minutes_alarm_snooze = 88;
			seconds_alarm_snooze = 88;
		}
		
		//Check whether it is AM or PM and turn on dot
		if(ampm == AM)
		{
			display_number(12, 6); //Turn on dot on apostrophe
			delay_us(bright_level);
		}

		clear_display();
		delay_us(bright_level);
	}
}

//Displays current alarm time
//Brightness level is an amount of time the LEDs will be in - 200us is pretty dim but visible.
//Amount of time during display is around : [ BRIGHT_LEVEL(us) * 5 + 10ms ] * 10
//Roughly 11ms * 10 = 110ms
//Time on is in (ms)
void display_alarm_time(uint16_t time_on)
{
	uint16_t bright_level = 50;
	
	//time_on /= 11; //Take the time_on and adjust it for the time it takes to run the display loop below

	for(uint16_t j = 0 ; j < time_on ; j++)
	{
		//Display normal hh:mm time
		if(hours_alarm > 9)
		{
			display_number(hours_alarm / 10, 1); //Post to digit 1
			delay_us(bright_level);
		}

		display_number(hours_alarm % 10, 2); //Post to digit 2
		delay_us(bright_level);

		display_number(minutes_alarm / 10, 3); //Post to digit 3
		delay_us(bright_level);

		display_number(minutes_alarm % 10, 4); //Post to digit 4
		delay_us(bright_level);

		
		//During debug, display mm:ss
		/*display_number(minutes_alarm / 10, 1); 
		delay_us(bright_level);

		display_number(minutes_alarm % 10, 2); 
		delay_us(bright_level);

		display_number(seconds_alarm / 10, 3); 
		delay_us(bright_level);

		display_number(seconds_alarm % 10, 4); 
		delay_us(bright_level);

		display_number(10, 5); //Display colon
		delay_us(bright_level);*/
		
		//Flash colon for each second
		if(flip == 1) 
		{
			display_number(10, 5); //Post to digit COL
			delay_us(bright_level);
		}
		
		//Check whether it is AM or PM and turn on dot
		if(ampm_alarm == AM)
		{	
			display_number(12, 6); //Turn on dot on apostrophe
			delay_us(bright_level);
		}

		clear_display();
		delay_ms(1);
	}
	
}


//Make noise for time_on in (ms)
void siren(int duration)
{

	for(int i = 0 ; i < duration ; i++)
	{
		cbi(PORTB, BUZZ1);
		sbi(PORTB, BUZZ2);
		delay_us(300);

		sbi(PORTB, BUZZ1);
		cbi(PORTB, BUZZ2);
		delay_us(300);
	}

	cbi(PORTB, BUZZ1);
	cbi(PORTB, BUZZ2);

	delay_ms(50);
	
	for(int i = 0 ; i < duration ; i++)
	{
		cbi(PORTB, BUZZ1);
		sbi(PORTB, BUZZ2);
		delay_us(300);

		sbi(PORTB, BUZZ1);
		cbi(PORTB, BUZZ2);
		delay_us(300);
	}

	cbi(PORTB, BUZZ1);
	cbi(PORTB, BUZZ2);
}

void ioinit(void)
{
    //1 = output, 0 = input 
    DDRB = 0b11111111 & ~((1<<BUT_UP)|(1<<BUT_DOWN)|(1<<BUT_ALARM)); //Up, Down, Alarm switch  
    DDRC = 0b11111111;
    DDRD = 0b11111111 & ~(1<<BUT_SNOOZE); //Snooze button
	

	PORTB = (1<<BUT_UP)|(1<<BUT_DOWN)|(1<<BUT_ALARM); //Enable pull-ups on these pins
	PORTD = 0b10100100; //Enable pull-up on snooze button
	PORTC = 0b00111111;

    //Init Timer0 for delay_us
	TCCR0B = (1<<CS01); //Set Prescaler to clk/8 : 1click = 0.5us(assume we are running at external 16MHz). CS01=1 
	
	//Init Timer1 for second counting <mds> CTC mode
	TCCR1B = (1<<CS12)|(1<<CS10); //Set prescaler to clk/1024 :1click = 64us (assume we are running at 16MHz)
	//<mds> set CTC mode
	TCCR1B |= (1<<WGM12)|(1<<WGM13); // Mode 12 CTC mode
	TIMSK1 = (1<<ICIE1); //Enable overflow interrupts
	ICR1 = 15625; // SET TOP to 1s
    //TCNT1 = 49911; //65536 - 15,625 = 49,911 - Preload timer 1 for 49,911 clicks. Should be 1s per ISR call
	
	//Init Timer2 for updating the display via interrupts
	TCCR2B = (1<<CS22)|(1<<CS21)|(1<<CS20); //Set prescalar to clk/1024 : 1 click = 64us (assume 16MHz)
	TIMSK2 = (1<<TOIE2);
	//TCNT2 should overflow every 16.384 ms (256 * 64us)
	
	hours = 88;
	minutes = 88;
	seconds = 88;

	alarm_going = FALSE;
	
	sei(); //Enable interrupts

	siren(20); //Make some noise at power up
	
	hours = 12;
	minutes = 00;
	seconds = 00;
	ampm = AM;

	hours_alarm = 11;
	minutes_alarm = 55;
	seconds_alarm = 00;
	ampm_alarm = PM;

	hours_alarm_snooze = 12;
	minutes_alarm_snooze = 00;
	seconds_alarm_snooze = 00;
	ampm_alarm_snooze = AM;
	
	snooze = FALSE;

	//Segment test
	/*while(1)
	{
		PORTD = 0;
		PORTC = 0xFF;
		delay_ms(1000);

		PORTD = 0xFF;
		PORTC = 0xFF;
		delay_ms(1000);
	}*/	
}

//General short delays
void delay_ms(uint16_t x)
{
	for (; x > 0 ; x--)
	{
		delay_us(250);
		delay_us(250);
		delay_us(250);
		delay_us(250);
	}
}

//General short delays
void delay_us(uint16_t x)
{
	x *= 2; //Correction for 16MHz
	
	while(x > 256)
	{
		TIFR0 = (1<<TOV0); //Clear any interrupt flags on Timer2
		TCNT0 = 0; //256 - 125 = 131 : Preload timer 2 for x clicks. Should be 1us per click
		while( (TIFR0 & (1<<TOV0)) == 0);
		
		x -= 256;
	}

	TIFR0 = (1<<TOV0); //Clear any interrupt flags on Timer2
	TCNT0 = 256 - x; //256 - 125 = 131 : Preload timer 2 for x clicks. Should be 1us per click
	while( (TIFR0 & (1<<TOV0)) == 0);
}
