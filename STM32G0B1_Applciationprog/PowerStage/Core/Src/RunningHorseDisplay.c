/*
 * RunningHorseDisplay.c
 *
 *  Created on: 5 Feb 2026
 *      Author: jorda
 */
#include "main.h"
#include "ssd1306.h"
#include "horse_anim.h"
#include "bitmap.h"

static int count =0;

const unsigned char* horse[] = {
		horse1,
		horse2,
		horse3,
		horse4,
		horse5,
		horse6,
		horse7,
		horse8,
		horse9,
		horse10

};
void runninghorse(){


			SSD1306_Clear();
			SSD1306_DrawBitmap(0,0,horse[1],128,64,1);
		  	SSD1306_UpdateScreen();

		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[2],128,64,1);
		  	  SSD1306_UpdateScreen();

		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[3],128,64,1);
		  	  SSD1306_UpdateScreen();

		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[4],128,64,1);
		  	  SSD1306_UpdateScreen();

		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[5],128,64,1);
		  	  SSD1306_UpdateScreen();

		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[6],128,64,1);
		  	  SSD1306_UpdateScreen();


		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[7],128,64,1);
		  	  SSD1306_UpdateScreen();

		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[8],128,64,1);
		  	  SSD1306_UpdateScreen();


		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[9],128,64,1);
		  	  SSD1306_UpdateScreen();


		  	  SSD1306_Clear();
		  	SSD1306_DrawBitmap(0,0,horse[0],128,64,1);
		  	  SSD1306_UpdateScreen();
}
void runningHorse(){
	count++;
	if(count>=10){
		count=0;
	}
//	SSD1306_Clear();
	SSD1306_Fill(0);
	SSD1306_DrawBitmap(0, 0, horse[count], 128, 64, 1);
	SSD1306_UpdateScreen();
}
