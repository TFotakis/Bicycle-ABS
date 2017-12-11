/*
*	Milestone3.c
*
*	Authors: Kritharakis Emmanuel, Fotakis Tzanis
*	Created on: 10 November 2017
*	AVR: Atmel ATMega328P
*	Created with: Atmel Studio 7
*
*	The code below tries to create an Anti-Lock Braking System (ABS) for bicycles using a sliding 
*	potentiometer as input to check the brake's lever position, two Photo-Interrupter sensors, one for
*	each wheel, to get the wheels' frequencies and two servomechanisms to move each wheel's brake caliper.
*
*	It sets PB1 and PB2 as PWM outputs using OCR1A and OCR1B compare registers respectively for connecting
*	the front and rear servomechanism. Also, it uses ADC0 (PC0) as analog input signal from the sliding 
*	potentiometer, whose position is read which controls each PWM's duty cycle. More specifically, it 
*	linearly sets the PB1's (front servo) and PD2's (rear servo) PWM duty cycle to minimum when the 
*	potentiometer is at maximum position and the exact reverse when the potentiometer is at minimum 
*	position as long as there is no frequency difference between the two wheels. If there is, then the
*	slower wheel's brakes release until it reaches the speed of the faster ones, and then it brakes again.
*	The whole functionality is interrupt driven, using the ADC's "Conversion Completed" Interrupt and 
*	INT0 & INT1 pins to check the Photo-Interrupter sensors' pulse's width.
*/

// ------------------------------------------ Calibration ------------------------------------------------
#define F_CPU 16000000UL	// 16 MHz Clock Frequency
#define DIFFERENCE_THRESHOLD 50
// -------------------------------------------------------------------------------------------------------

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

volatile uint32_t microsFrontWheel=0;
volatile uint32_t microsRearWheel=0;
uint32_t startFrontWheel=0;
uint32_t startRearWheel=0;
uint32_t frontWheelPeriod = -1;
uint32_t rearWheelPeriod = -1;
int checkWheelsFrequenciesReturnValue=0;

// Initializes the ADC component to convert the ACD0 input with a 128 prescaler and auto conversion
 void ADCinit(){
	ADMUX = 1 << REFS0; // AVCC with external capacitor at AREF pin, ADC0 selected
	ADMUX |= 1<<ADLAR; // ADC Left Adjust Result to use ADCH register for 8-bit operations (ignore 2 Least Significant Bits)
	ADCSRA = 1 << ADEN; // Analog to Digital Enable
	ADCSRA |= 1<<ADATE; // Auto Trigger Enable Conversion
	ADCSRA |= 1<<ADIE; // ADC Conversion Complete Interrupt activated
	ADCSRA |= 1<<ADPS2 | 1<<ADPS1 | 1<<ADPS0; // Set prescaler to clk/128
	ADCSRA |= 1<<ADSC; // Start Conversions
}

// Initializes Timer/Counter2 in CTC mode to trigger an interrupt every 160 clock ticks or 10 us
void MicrosTimerInit(){
	TCCR2A = 1<<WGM21; // Set Timer 2 to CTC mode, TOP = OCR2A, Immediate update of OCR2A, TOV Flag set on MAX, Normal port operation, OC2A disconnected
	TCCR2B = 1 << CS21; // Set prescaler to clk/8
	TIMSK2 = 1 << OCIE2A; // Enable CTC interrupt
	OCR2A = 20; // Set TOP value to 20
}

// Initializes the front and rear Photo-Interrupter Sensors on INT0 & INT1 respectively to trigger 
// interrupts on any of their state change
void PhotoInterruptersInit(){
	EIMSK = 1<<INT1 | 1<<INT0; // Enable INT0 and INT1
	EICRA = 0<<ISC11 | 1<<ISC10 | 0<<ISC01 | 1<<ISC00; // Trigger INT0 and INT1 on any state change
}

// Initializes PWM signal on PB1 & PB2 for front & back servo respectively
void ServoPWMinit(){
	DDRB = 1<<DDB1 | 1<<DDB2; // Set PB1 & PB2 as outputs for OC1A and OC1B respectively
	TCCR1A=1<<COM1A1 | 1<<COM1B1 | 1<<WGM11; //Non-Inverting mode - Set OC1A/OC1B on compare match when up-counting. Clear OC1A/OC1B on compare match when down counting.
	TCCR1B=1<<WGM13 | 1<<WGM12; // Fast PWM
	TCCR1B|=1<<CS11; // Set prescaler to clk/8
	ICR1=40000;	// PWM Frequency = 50Hz (Period = 20ms Standard).
}

// Check for difference between the frequencies of the two wheels
// Returns 0 when equal, 1 when FrontPeriod - RearPeriod > DIFFERENCE_THRESHOLD, 
// -1 when FrontPeriod - RearPeriod < DIFFERENCE_THRESHOLD
void checkWheelsFrequencies(){
	// If there are no new pulse periods measurements return the last decision
	// Considers the state when the bike is stopped and no pulses are sent from the servos
	// but still want to brake
	if (frontWheelPeriod == -1 || rearWheelPeriod == -1) return;
	int32_t difference = frontWheelPeriod-rearWheelPeriod;
	// Reinitialize for the new measurements
	frontWheelPeriod=-1;
	rearWheelPeriod=-1;
	if(difference>DIFFERENCE_THRESHOLD) checkWheelsFrequenciesReturnValue = 1;
	else if (difference<-DIFFERENCE_THRESHOLD) checkWheelsFrequenciesReturnValue = -1;
	else checkWheelsFrequenciesReturnValue = 0;
}

// Sets the Servo PWM duty cycle to PB1 & PB2 for controlling the front & rear servo
// MinValue = 0 - MaxValue = 235 
void setServoPosition(int value){
	checkWheelsFrequencies();
	if(value>235) value=235; else if(value<0) value=0; // Check for valid value boundaries
	// If front frequency < rear frequency cut the front brake down, else apply the value
	OCR1A = 1000 + (checkWheelsFrequenciesReturnValue == 1 ? 0 : value<<4);
	// If rear frequency < front frequency cut the front brake down, else apply the value
	OCR1B = 1000 + (checkWheelsFrequenciesReturnValue == -1 ? 0 : value<<4);
}

// ADC Interrupt Service Routine
// Sets the Servo Position linearly inverted to the Slider position
ISR (ADC_vect){
	// Slider value inversion and offsetting (256 Slider values - ADCH - 128 values offset = 128 - ADCH)
	setServoPosition(128 - ADCH); // Set servos' positions equally to the sliders inverted position
}

// Counting clock ticks for each wheel's Photo-Interrupter Sensor
ISR(TIMER2_COMPA_vect){
	microsFrontWheel++;
	microsRearWheel++;
}

// Front Photo-Interrupter Sensor Interrupt Service Routine
// Calculates the sensor's pulse width in tenths of microseconds
ISR(INT0_vect){
	// if interrupt is triggered on the rising edge store the starting time
	// else if interrupt is triggered on the falling edge sud starting time with current 
	// time to calculate the pulse's period
	if(PIND & 1<<PORTD2){ 
		startFrontWheel = microsFrontWheel;
	}else{ 
		frontWheelPeriod = microsFrontWheel-startFrontWheel;
		microsFrontWheel=0; // Restart time counting
	}
}

// Rear Photo-Interrupter Sensor Interrupt Service Routine
// Calculates the sensor's pulse width in tenths of microseconds
ISR(INT1_vect){
	// if interrupt is triggered on the rising edge store the starting time
	// else if interrupt is triggered on the falling edge sud starting time with current
	// time to calculate the pulse's period
	if(PIND & 1<<PORTD3){
		startRearWheel = microsRearWheel;
	}else{
		rearWheelPeriod = microsRearWheel-startRearWheel;
		microsRearWheel=0; // Restart time counting
	}
}

int main(void){
	ADCinit();
	MicrosTimerInit();
	PhotoInterruptersInit();
	ServoPWMinit();
	sei();
	while(1);
}