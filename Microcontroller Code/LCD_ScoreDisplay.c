#include <avr/io.h>
#include "io.c"
#include "timer.h"
#include "usart.h"

unsigned char USARTReceiver = 0;
unsigned char score = 0;
unsigned char digit = 0;
unsigned char zeroes = 0;
unsigned char cursorpos = 1;
unsigned char displayonce = 0;

enum LCD_States{LCD_Start, LCD_Reset, LCD_Score} LCD_state;
	
void LCD_Tick()
{
	switch(LCD_state) {
		case LCD_Start:
			LCD_state = LCD_Reset;
			break;
			
		case LCD_Reset:
			if (USART_HasReceived(0)) {
				LCD_state = LCD_Score;
				LCD_DisplayString(1, "Score: ");
				LCD_Cursor(8);
			}
			break;
			
		case LCD_Score:
			break;
			
		default:
			LCD_state = LCD_Start;
			break;
	}
	
	switch(LCD_state) {
		case LCD_Start:
			break;
			
		case LCD_Reset:
			if (!displayonce)
				LCD_DisplayString(1, "Red=Load/Reset  Green=Shoot");
			displayonce = 1;
			break;
			
		case LCD_Score:
			cursorpos = 36;
			
			LCD_Cursor(cursorpos);
			if (USART_HasReceived(0)) {
				score = USARTReceiver;
				USARTReceiver = USART_Receive(0);
				// Refresh score
				cursorpos = 7;
				LCD_Cursor(cursorpos);
				LCD_WriteData(' ');
				LCD_WriteData(' ');
				LCD_WriteData(' ');
				LCD_Cursor(cursorpos);
				
				if (score >= 100) {
					digit = score / 100;
					score = score % 100;
					LCD_WriteData(digit + '0'); // Writes hundredth place digit
				
					if (score)
						digit = score / 10;
					score = score % 10;
					LCD_WriteData(digit + '0'); // Writes tenth place digit
					LCD_WriteData(score + '0'); // Writes first place digit
				}
				else if (score >= 10) {
					digit = score / 10;
					score = score % 10;
					LCD_WriteData(digit + '0'); // Writes tenth place digit
					LCD_WriteData(score + '0'); // Writes first place digit
				}
				else
					LCD_WriteData(score + '0'); // Writes first place digit
			}
			break;
			
		default:
			break;
	}
}

int main(void)
{
	DDRA = 0xFF; PORTA = 0x00; // LCD data lines
	DDRB = 0xFF; PORTB = 0x00; // LCD control lines
	
	// Initializes the LCD display
	LCD_init();
	initUSART(0);
	
	TimerSet(50);
	TimerOn();
	
	while(1){
		LCD_Tick();
		while(!TimerFlag){}
		TimerFlag = 0;
	}
}
