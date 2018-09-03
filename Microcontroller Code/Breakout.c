#include <avr/io.h>
#include "bit.h"
#include "timer.h"
#include "usart.h"

#define UP 1
#define DOWN -1
#define LEFT -1
#define MIDDLE 0
#define RIGHT 1

void ADC_init() {
	ADCSRA |= (1 << ADEN) | (1 << ADSC) | (1 << ADATE);
	// ADEN: setting this bit enables analog-to-digital conversion.
	// ADSC: setting this bit starts the first conversion.
	// ADATE: setting this bit enables auto-triggering. Since we are
	//        in Free Running Mode, a new conversion will trigger whenever
	//        the previous conversion completes.
}

void transmit_data(unsigned char data) {
	int i;
	for (i = 0; i < 8 ; ++i) {
		// Sets SRCLR to 1 allowing data to be set
		// Also clears SRCLK in preparation of sending data
		PORTB = 0x08;
		// set SER = next bit of data to be sent.
		PORTB |= ((data >> i) & 0x01);
		// set SRCLK = 1. Rising edge shifts next bit of data into the shift register
		PORTB |= 0x02;
	}
	// set RCLK = 1. Rising edge copies data from “Shift” register to “Storage” register
	PORTB |= 0x04;
	// clears all lines in preparation of a new transmission
	PORTB = 0x00;
}

void set_PWM(double frequency) {
	// Keeps track of the currently set frequency
	// Will only update the registers when the frequency
	// changes, plays music uninterrupted.
	static double current_frequency;
	if (frequency != current_frequency) {

		if (!frequency) TCCR3B &= 0x08; //stops timer/counter
		else TCCR3B |= 0x03; // resumes/continues timer/counter
		
		// prevents OCR3A from overflowing, using prescaler 64
		// 0.954 is smallest frequency that will not result in overflow
		if (frequency < 0.954) OCR3A = 0xFFFF;
		
		// prevents OCR3A from underflowing, using prescaler 64					// 31250 is largest frequency that will not result in underflow
		else if (frequency > 31250) OCR3A = 0x0000;
		
		// set OCR3A based on desired frequency
		else OCR3A = (short)(8000000 / (128 * frequency)) - 1;

		TCNT3 = 0; // resets counter
		current_frequency = frequency;
	}
}

void PWM_on() {
	TCCR3A = (1 << COM3A0);
	// COM3A0: Toggle PB6 on compare match between counter and OCR3A
	TCCR3B = (1 << WGM32) | (1 << CS31) | (1 << CS30);
	// WGM32: When counter (TCNT3) matches OCR3A, reset counter
	// CS31 & CS30: Set a prescaler of 64
	set_PWM(0);
}

void PWM_off() {
	TCCR3A = 0x00;
	TCCR3B = 0x00;
}

double PWM_freq = 0;

// Paddle Global Variables
unsigned short mid = 512;
unsigned short reading = 0;
unsigned char paddle_pos = 0;	// position of the middle of the paddle.
unsigned char paddle_data = 0;	// Paddle blocks to light on LED

// Ball Global Variables
unsigned char xpos = 0;		// Column of ball pos.
unsigned char ypos = 0;		// Row of ball pos.g
signed char xdir = 0;	// Current x-axis direction of ball
signed char ydir = 0;	// Current y-axis direction of ball 
unsigned char shot = 0; // Indicate ball has been shot at start

// Brick Global Variables
unsigned char layer1 = 0x00;
unsigned char layer2 = 0x00;
unsigned char layer3 = 0x00;

// Game Global Variables
unsigned char reset = 0;	// Reset game
unsigned char lost = 0;		// Indicate game over.
unsigned char victory = 0;	// Indicate game won. Player must be godlike.
unsigned char start = 0;	// Start game
unsigned char setup = 0;	// Indicate ball setup
unsigned char ballreset = 0;	// Indicate ball reset
unsigned short ballspeed = 250;	// Speed of ball
unsigned char brickhits = 0;	// Indicate how many bricks were hit.
unsigned char paddle_hit = 0;	// Indicate paddle hit for s.effect
unsigned char score = 0;		// Player's score.
unsigned char finalsend = 0;	// Flag to send final score during victory.

enum Paddle_States {Pad_Init, Pad_Wait, Pad_Setup, Pad_Control, Pad_Reset} p_state;
enum Ball_States {Ball_Init, Ball_Wait, Ball_Setup, Ball_Cycle, Ball_Out, Ball_Reset} b_state;
enum Game_States {Game_Init, Game_Wait, Game_Setup, Game_Status, Game_Reset, Game_Lost, Game_Won} g_state;
enum LED_States {LED_Init, LED_Paddle, LED_Ball, LED_Brick1, LED_Brick2, LED_Brick3, LED_Brick4} l_state;
enum Music_States {Music_Init, Music_Wait};

void Paddle_Tick() {
	switch(p_state) {
		case Pad_Init:
			p_state = Pad_Wait;
			break;
			
		case Pad_Wait:
			if (start)
				p_state = Pad_Setup;
			break;
		
		case Pad_Setup:
			p_state = Pad_Control;
			break;
		
		case Pad_Control:
			if (reset)
				p_state = Pad_Reset;
			if (lost)
				p_state = Pad_Wait;
			if (victory)
				p_state = Pad_Wait;
			break;
		
		case Pad_Reset:
			if (!reset)
				p_state = Pad_Setup;
			break;
		
		default:
			p_state = Pad_Init;
			break;
	}
	
	switch(p_state) {
		case Pad_Init:
			break;
			
		case Pad_Wait:
			start = ~PINA & 0x02;
			paddle_data = 0x00;
			paddle_pos = 0x00;
			break;
		
		case Pad_Setup:
			paddle_data = 0x07;
			paddle_pos = 0x02;
			break;
		
		case Pad_Control:
			reading = ADC;			// Read analog from joystick
			reset = ~PINA & 0x02;	// Read reset button
			
			if (reading > mid + 100) {		// Move paddle right
				if(paddle_data < 0xE0) {
					paddle_data = paddle_data * 2;
					paddle_pos = paddle_pos * 2;
				}
			}
			else if (reading < mid - 100) {	// Move paddle left
				if (paddle_data > 0x07) {
					paddle_data = paddle_data / 2;
					paddle_pos = paddle_pos / 2;
				}
			}
			
			break;
		
		case Pad_Reset:
			paddle_data = 0x00;
			reset = ~PINA & 0x02;
			if(reset)
				ballreset = 1;
			break;
		
		default:
			break;
	}
}

void Ball_Tick() {
	switch(b_state) {
		case Ball_Init:
			b_state = Ball_Wait;
			break;
			
		case Ball_Wait:
			if (start)
				b_state = Ball_Setup;
			break;
			
		case Ball_Setup:
			if (shot)
				b_state = Ball_Cycle;
			if (reset)
				b_state = Ball_Reset;
			break;
		
		case Ball_Cycle:
			if (lost)
				b_state = Ball_Out;
			if (setup)
				b_state = Ball_Setup;
			if (ballreset)
				b_state = Ball_Reset;
			if (victory)
				b_state = Ball_Wait;
			break;
		
		case Ball_Out:
			if (ballreset)
				b_state = Ball_Reset;
			break;
		
		case Ball_Reset:
			if (!reset)
				b_state = Ball_Setup;
			break;
		
	}
	
	switch(b_state){
		case Ball_Init:
			break;
			
		case Ball_Wait:
			xpos = 0;
			ypos = 0;
			break;
			
		case Ball_Setup:
			setup = 1;
			ballreset = 0;
			brickhits = 0;
			PWM_freq = 0;
			set_PWM(PWM_freq);
			
			// Checks buttons
			reset = ~PINA & 0x02;
			shot = ~PINA & 0x04;
			if (shot)
				setup = 0;
			
			// Setup ball
			xdir = MIDDLE;
			ydir = UP;
			xpos = paddle_pos;
			ypos = 0x40;
			
			break;
			
		case Ball_Cycle:
			setup = 0;
			PWM_freq = 0;
			brickhits = 0;
			paddle_hit = 0;
			reset = ~PINA & 0x02;
			unsigned char xposnum = 0;
			unsigned char tmpxpos = xpos;
			unsigned char omit = 0;
			
			while (tmpxpos != 0x01) {
				tmpxpos = tmpxpos / 2;
				xposnum++;
			}
		
			// Prevents ball from going middle again after start.
			if (ypos == 0x08 && xdir == MIDDLE) {
				if (paddle_pos <= 0x08)
					xdir = LEFT; 
				else 
					xdir = RIGHT;
				ydir = DOWN;
				layer3 = layer3 & ~xpos; // Removes brick
				brickhits++;
			}
			
			// Change x-axis direction of ball upon hitting brick/wall
			if (xpos == 0x01 && xdir == LEFT)
				xdir = RIGHT;
			else if (xpos == 0x80 && xdir == RIGHT)
				xdir = LEFT;
				
			else if (xdir == LEFT) { // Checks ball left-side collision with brick
				if (ypos == 0x04) {
					if (GetBit(layer3, xposnum - 1)) {
						xdir = RIGHT;
						layer3 = layer3 & ~(xpos / 2);
						brickhits++;
					}
				}
				if (ypos == 0x02) {
					if (GetBit(layer2, xposnum - 1)) {
						xdir = RIGHT;
						layer2 = layer2 & ~(xpos / 2);
						brickhits++;
					}
				}
				if (ypos == 0x01) {
					if (GetBit(layer1, xposnum - 1)) {
						xdir = RIGHT;
						layer1 = layer1 & ~(xpos / 2);
						brickhits++;
					}
				}	
			}
			else if (xdir == RIGHT) { // Checks ball right-side collision with brick
				if (ypos == 0x04) {
					if (GetBit(layer3, xposnum + 1)) {
						xdir = LEFT;
						layer3 = layer3 & ~(xpos * 2);
						brickhits++;
					}
				}
				if (ypos == 0x02) {
					if (GetBit(layer2, xposnum + 1)) {
						xdir = LEFT;
						layer2 = layer2 & ~(xpos * 2);
						brickhits++;
					}
				}
				if (ypos == 0x01) {
					if (GetBit(layer1, xposnum + 1)) {
						xdir = LEFT;
						layer1 = layer1 & ~(xpos * 2);
						brickhits++;
					}
				}
			}
				
			
			// Change y-axis direction of ball upon hitting brick/wall 
			if (ypos == 0x01)
				ydir = DOWN;
			
			// Handles ball hitting bricks as it comes down 
			if (ypos == 0x01 && ydir == DOWN) {	// Ball hits layer 2 from top
				if(GetBit(layer2,xposnum - 1) && xdir == LEFT) {
					xdir = RIGHT;
					layer2 = layer2 & ~(xpos / 2);
					brickhits++;
				}
				else if(GetBit(layer2, xposnum + 1) && xdir == RIGHT) {
					xdir = LEFT;
					layer2 = layer2 & ~(xpos * 2);
					brickhits++;
				}
			}
			else if (ypos == 0x02 && ydir == DOWN) {	// Ball hits layer 3 from top
				if(GetBit(layer3, xposnum)) {
					ydir = UP;
					layer3 = layer3 & ~xpos;
					brickhits++;
				}
				else if(GetBit(layer3,xposnum - 1) && xdir == LEFT) {
					xdir = RIGHT;
					ydir = UP;
					layer3 = layer3 & ~(xpos / 2);
					brickhits++;
					if(GetBit(layer1,xposnum + 1)) {
						xdir = LEFT;
						ydir = DOWN;
						layer1 = layer1 & ~(xpos * 2);
						brickhits++;
						omit = 1;
					}
				}
				else if(GetBit(layer3, xposnum + 1) && xdir == RIGHT) {
					xdir = LEFT;
					ydir = UP;
					layer3 = layer3 & ~(xpos * 2);
					brickhits++;
					if(GetBit(layer1,xposnum - 1)) {
						xdir = RIGHT;
						ydir = DOWN;
						layer1 = layer1 & ~(xpos / 2);
						brickhits++;
						omit = 1;
					}
				}
			}	
				
			else if (ypos == 0x02 && ydir == UP) {
				if(GetBit(layer1,xposnum)) {
					ydir = DOWN;
					layer1 = layer1 & ~xpos; 
					brickhits++;
				}
				else if(GetBit(layer1, xposnum - 1) && xdir == LEFT) {
					ydir = DOWN;
					xdir = RIGHT;
					layer1 = layer1 & ~(xpos / 2);
					brickhits++;
				}
				else if(GetBit(layer1, xposnum + 1) && xdir == RIGHT) {
					ydir = DOWN;
					xdir = LEFT;
					layer1 = layer1 & ~(xpos * 2);
					brickhits++;
				}
			}
			else if (ypos == 0x04 && ydir == UP) {
				if(GetBit(layer2,xposnum)) {
					ydir = DOWN;
					layer2 = layer2 & ~xpos;
					brickhits++;
				}
				else if(GetBit(layer2, xposnum - 1) && xdir == LEFT) {
					ydir = DOWN;
					xdir = RIGHT;
					layer2 = layer2 & ~(xpos / 2);
					brickhits++;
				}
				else if(GetBit(layer2, xposnum + 1) && xdir == RIGHT) {
					ydir = DOWN;
					xdir = LEFT;
					layer2 = layer2 & ~(xpos * 2);
					brickhits++;
				}
				
			}
			else if (ypos == 0x08 && ydir == UP) {
				if(GetBit(layer3,xposnum)) {
					ydir = DOWN;
					layer3 = layer3 & ~xpos;
					brickhits++;
				}
				else if(GetBit(layer3, xposnum - 1) && xdir == LEFT) {
					ydir = DOWN;
					xdir = RIGHT;
					layer3 = layer3 & ~(xpos / 2);
					brickhits++;
				}
				else if(GetBit(layer3, xposnum + 1) && xdir == RIGHT) {
					ydir = DOWN;
					xdir = LEFT;
					layer3 = layer3 & ~(xpos * 2);
					brickhits++;
				}
			}
			
			
			
			// Change y and/or x direction of ball upon hitting paddle
			else if (ypos == 0x40 && ydir == DOWN) {
				if(xpos == (paddle_pos / 4)) { // Hit on left edge
					if (xdir == LEFT)
						lost = 1;
					xdir = LEFT;
				}
					
				if(xpos == (paddle_pos * 4)) {// Hit on right edge
					if (xdir == RIGHT)
						lost = 1;
					xdir = RIGHT;
				}
					
				if((xpos >= paddle_pos / 4) && (xpos <= paddle_pos * 4) && !lost) {
					ydir = UP;
					paddle_hit = 1;
				}
					
				else
					lost = 1;
			}
			
			// Moves the ball
			if (!omit) {
				if (xdir == LEFT && xpos > 0x01)
					xpos = xpos / 2;
				else if (xdir == RIGHT && xpos < 0x80)
					xpos = xpos * 2;
				if (ydir == UP && ypos > 0x01)
					ypos = ypos / 2;
				else
					ypos = ypos * 2;
			}
			
			// Register number of brick hits and make sound effects.
			if(brickhits == 1)
				PWM_freq = 220;
			else if (brickhits >= 2)
				PWM_freq = 440;
			else
				PWM_freq = 0;
				
			if(paddle_hit)
				PWM_freq = 110;
			
			set_PWM(PWM_freq);
			
			if(brickhits >= 1)
				score = score + brickhits;
			
			break;
			
		case Ball_Out:
			reset = ~PINA & 0x02;
			break;
			
		case Ball_Reset:
			reset = ~PINA & 0x02;
			xpos = 0;
			ypos = 0;
			break;
	}
}

void LED_Tick() {
	switch(l_state) {
		case LED_Init:
			l_state = LED_Paddle;
			break;
			
		case LED_Paddle:
			l_state = LED_Ball;
			break;
			
		case LED_Ball:
			l_state = LED_Brick1;
			break;
		
		case LED_Brick1:
			l_state = LED_Brick2;
			break;
			
		case LED_Brick2:
			l_state = LED_Brick3;
			break;
			
		case LED_Brick3:
			l_state = LED_Paddle;
			break;
			
		default:
			l_state = LED_Init;
			break;
	}
	
	switch(l_state) {
		case LED_Init:
			break;
			
		case LED_Paddle:
			PORTC = 0x80;
			transmit_data(~paddle_data);
			break;
			
		case LED_Ball:
			PORTC = ypos;
			transmit_data(~xpos);
			break;
			
		case LED_Brick1:
			PORTC = 0x01;
			transmit_data(~layer1);
			break;
			
		case LED_Brick2:
			PORTC = 0x02;
			transmit_data(~layer2);
			break;
		
		case LED_Brick3:
			PORTC = 0x04;
			transmit_data(~layer3);
			break;
			
		default:
			l_state = LED_Init;
			break;
			
	}
}

void Game_Tick() {
	switch(g_state) {
		case Game_Init:
			g_state = Game_Wait;
			break;
			
		case Game_Wait:
			if (start)
				g_state = Game_Setup;
			break;
			
		case Game_Setup:
			if (shot)
				g_state = Game_Status;
			break;
			
		case Game_Status:
			if (reset)
				g_state = Game_Reset;
			if (lost)
				g_state = Game_Lost;
			if (victory)
				g_state = Game_Won;
			break;
			
		case Game_Reset:
			if (!reset)
				g_state = Game_Setup;
			break;
				
		case Game_Lost:
			if (start)
				g_state = Game_Reset;
			break;
			
		case Game_Won:
			if (start)
				g_state = Game_Reset;
			break;
		
		default:
			g_state = Game_Init;
			break;
	}
	switch(g_state) {
		case Game_Init:
			break;
		
		case Game_Wait:
			break;
		
		case Game_Setup:
			layer1 = 0xFF;
			layer2 = 0xFF;
			layer3 = 0xFF;
			break;
		
		case Game_Status:
			reset = ~PINA & 0x02;
			
			if (layer1 == 0 && layer2 == 0 && layer3 == 0) {
				if (ballspeed > 150) {
					ballspeed = ballspeed - 50;
					layer1 = 0xFF;
					layer2 = 0xFF;
					layer3 = 0xFF;
					setup = 1;
					if(USART_IsSendReady(0))
						USART_Send(score, 0);
				}
				
				else {
					victory = 1;
					finalsend = 1;
				}
			}
			
			if (brickhits) {
				if(USART_IsSendReady(0))
					USART_Send(score, 0);
			}
			
			break;
			
		case Game_Lost:
			PWM_freq = 55;
			set_PWM(PWM_freq);
			break;
			
		case Game_Won:
			PWM_freq = 0;
			set_PWM(PWM_freq);
			if (finalsend) {
				if(USART_IsSendReady(0))
					USART_Send(score, 0);
			}
			finalsend = 0;
			break;
		
		case Game_Reset:
			reset = ~PINA & 0x02;
			if (reset)
				ballreset = 1;
			lost = 0;
			victory = 0;
			score = 0;
			ballspeed = 250;
			if(USART_IsSendReady(0))
				USART_Send(score, 0);
			break;
		
		default:
			break;
	}
}

int main(void) {
	DDRA = 0x00; PORTA = 0xFF;
	DDRB = 0xFF; PORTB = 0x00;
	DDRC = 0xFF; PORTC = 0x00;
	DDRD = 0xFF; PORTD = 0x00;
	
	ADC_init();
	initUSART(0);

	// Set the timer and turn it on
	TimerSet(1);
	TimerOn();
	PWM_on();
	
	unsigned char game_ticks = 0;
	unsigned char ball_ticks = 0;

	while(1)
	{
		if (game_ticks >= 100) {
			Paddle_Tick();
			if (setup)
				Ball_Tick();
			Game_Tick();
			game_ticks = 0;
		}
		if (ball_ticks >= ballspeed && !setup) {
			Ball_Tick();
			ball_ticks = 0;
		}
		LED_Tick();
		

		while(!TimerFlag);
		TimerFlag = 0;
		
		game_ticks++;
		ball_ticks++;
	}
	
	return 0;
}

