#include <stdio.h>
#include <stdlib.h>
#include "NUC1xx.h" 
#include "DrvSYS.h" 
#include "DrvGPIO.h"
#include "DrvTIMER.h"
#include "LCD_Driver.h"
#include "DrvADC.h"
#include "ScanKey.h"

#define false 0
#define true 1

//Global Variables
char state[15]={0};
int timeCounter = 0;
int programButton = 0;

//------------------------------TROUBLESHOOTING-----------------------------------
char conv[15] = {0};
char time[15] = {0};
char scan[15] = {0};
int knock = 0;
//--------------------------------------------------------------------------------

//Pin definition.
const int knockSensor = 6;         // Piezo sensor on pin GPA6.
const int programSwitch = 1;       // If this is high we program a new code. GPD1
const int lockMotor = 0;           // Gear motor used to turn the lock. GPD0
const int programKey = 5;	   // Key that will be pressed to program.

// Tuning constants.  Could be made vars and hoooked to potentiometers for soft configuration, etc.
const int threshold = 100;         // Minimum signal from the piezo to register as a knock
const int rejectValue = 25;        // If an individual knock is off by this percentage of a knock we don't unlock..
const int averageRejectValue = 15; // If the average timing of the knocks is off by this percent we don't unlock.
const int knockFadeTime = 150;     // milliseconds we allow a knock to fade before we listen for another one. (Debounce timer.)
const int lockTurnTime = 7000;     // milliseconds that we run the motor to get it to go a half turn.

const int maximumKnocks = 20;       // Maximum number of knocks to listen for.
const int knockComplete = 1200;     // Longest time to wait for a knock before we assume that it's finished.

// Variables.
int secretCode[maximumKnocks] = {50, 25, 25, 50, 100, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // Initial setup: "Shave and a Hair Cut, two bits."
int knockReadings[maximumKnocks];   // When someone knocks this array fills with delays between knocks.
int knockSensorValue = 0;           // Last reading of the knock sensor.
int programButtonPressed = false;   // Flag so we remember the programming button setting at the end of the cycle.

void Heartbeat_TMR0_Callback(void){
	GPC_12 = ~GPC_12;
	TIMER0->TISR.TIF = 1;//clear flag
}

void TimeCount_TMR1_Callback(void){
	timeCounter = timeCounter + 20;
	TIMER1->TISR.TIF = 1;//clear flag
	//------------------------------TROUBLESHOOTING-----------------------------------
	//sprintf(time,"time %2.3f",(double)timeCounter/1000);
	//print_lcd(3,time);
	//--------------------------------------------------------------------------------
}

void Knock_Callback(void){
	knock++;
	GPA_13 = ~GPA_13;
}

void InitClock(){
	UNLOCKREG(); //unlock the protected registers    
	DrvSYS_SetOscCtrl(E_SYS_OSC22M,1); //select the 22MHz RC clock   
	while(DrvSYS_GetChipClockSourceStatus(E_SYS_OSC22M) != 1); //wait until the clock is stable     
	DrvSYS_SelectHCLKSource(7); //HCLK clock source 0: external 12MHz
	LOCKREG(); //Lock the protected registers
	DrvSYS_SetClockDivider(E_SYS_HCLK_DIV,2);
}
				 				
void InitGPIO(){
	GPA_12 = 1;
	GPC_12 = 1;
	GPC_13 = 1;
	GPC_14 = 1;
	GPC_15 = 1;
}

void InitADC(){
	//Disable Digital Input on ADC
	DrvGPIO_DisableDigitalInputBit(E_GPA,knockSensor);
				
	//Initialize/setup ADC (first 6 steps)
	DrvADC_Open(ADC_SINGLE_END, ADC_CONTINUOUS_OP,0x40, INTERNAL_RC22MHZ, 1);
}
void Init_KnockInt(){
	DrvGPIO_EnableEINT0(E_IO_RISING, E_MODE_EDGE,(GPIO_EINT0_CALLBACK)Knock_Callback);
}	
void InitLCD(){
	Initial_panel();
	clr_all_panel();
}
		
void InitHeartbeat(){
	DrvSYS_SelectIPClockSource(E_SYS_TMR0_CLKSRC,7); //select 22 MHz for Timer0 clock source     
	DrvTIMER_Init();
	DrvTIMER_Open(E_TMR0,2,E_PERIODIC_MODE); //Timer 0, 1/2  second, periodic 	
	DrvTIMER_SetTimerEvent(E_TMR0,1,(TIMER_CALLBACK)Heartbeat_TMR0_Callback,1);//Setup user defined interrupt function 
	DrvTIMER_EnableInt(E_TMR0); //Enable timer ISR 
	DrvTIMER_ClearIntFlag(E_TMR0);//Clear interrupt flag
	DrvTIMER_Start(E_TMR0); //Enable timer - start counting TCSR.CEN = 1
}
				
void InitTimeCount (){
	DrvSYS_SelectIPClockSource(E_SYS_TMR1_CLKSRC,7); //select 22 MHz for Timer1 clock source     
	DrvTIMER_Init();
	DrvTIMER_Open(E_TMR1,50,E_PERIODIC_MODE); //Timer 1, 1/50 s, periodic 	
	DrvTIMER_SetTimerEvent(E_TMR1,1,(TIMER_CALLBACK)TimeCount_TMR1_Callback,1);//Setup user defined interrupt function 
	DrvTIMER_EnableInt(E_TMR1); //Enable timer ISR 
	DrvTIMER_ClearIntFlag(E_TMR1);//Clear interrupt flag
	DrvTIMER_Start(E_TMR1); //Enable timer - start counting TCSR.CEN = 1
}

void GreenLED_ON(){GPA_13 = 0;}
void GreenLED_OFF(){GPA_13 = 1;}
void RedLED_ON(){GPC_14 = 0; GPC_15 = 0;}
void RedLED_OFF(){GPC_14 = 1; GPC_15 = 1;}
void RedLED_Pattern(){
	int i = 0;
	GPC_14 = 0; GPC_15 = 1;
  	for (i=0;i<10;i++){
      		DrvSYS_Delay(100000); 
      		GPC_14 = 1; GPC_15 = 0;
      		DrvSYS_Delay(100000); 
      		GPC_14 = 0; GPC_15 = 1;   
  	}
}

void Motor_ON(){GPD_0 = 1;}
void Motor_OFF(){GPD_0 = 0;}

void setup() {
	
	InitClock();
	InitGPIO();
	//InitADC();
	Init_KnockInt();
	InitLCD();
	OpenKeyPad();
	InitHeartbeat();
	InitTimeCount();
				
	//Mask port D bits 15:2
	DrvGPIO_SetPortMask(E_GPD, 0xFFFC); 
  
	DrvGPIO_Open(E_GPD, lockMotor, E_IO_OUTPUT); 	//Lock Motor
  	DrvGPIO_Open(E_GPD, programSwitch, E_IO_INPUT); //Program Switch
	
	GPC_12 = 0; // Heartbeat
	Motor_OFF(); //Motor output
 	GreenLED_ON(); // Green LED on, everything is go.
}

long map(long x,long in_min,long in_max,long out_min,long out_max){
	return (x-in_min)*(out_max-out_min)/(in_max-in_min)+out_min;
}

int validateKnock(){
  int i=0;
 
  // simplest check first: Did we get the right number of knocks?
  int currentKnockCount = 0;
  int secretKnockCount = 0;
  int maxKnockInterval = 0;   // We use this later to normalize the times.
  
  for (i=0;i<maximumKnocks;i++){
    if (knockReadings[i] > 0){
      currentKnockCount++;
    }
    if (secretCode[i] > 0){ 
      secretKnockCount++;
    }
    
    if (knockReadings[i] > maxKnockInterval){ 	// collect normalization data while we're looping.
      maxKnockInterval = knockReadings[i];
    }
  }
  
  // If we're recording a new knock, save the info and get out of here.
  if (programButtonPressed==true){
      for (i=0;i<maximumKnocks;i++){ // normalize the times
      	secretCode[i]= map(knockReadings[i],0, maxKnockInterval, 0, 100);
      }
	  
      // And flash the lights in the recorded pattern to let us know it's been programmed.
      GreenLED_OFF(); 
      RedLED_OFF();		
      DrvSYS_Delay(1000000); 
      GreenLED_ON();            
      RedLED_ON();   		
      DrvSYS_Delay(50000); 
			
      for (i = 0; i < maximumKnocks ; i++){
        GreenLED_OFF();    
        RedLED_OFF();		
				
        // only turn it on if there's a delay
        if (secretCode[i] > 0){                                   
          DrvSYS_Delay(map(secretCode[i],0, 100, 0, maxKnockInterval)*1000); // Expand the time back out to what it was.  Roughly. 
          GreenLED_ON();    
          RedLED_ON();
        }
        DrvSYS_Delay(50000);
      }
	
	return false; 	// We don't unlock the door when we are recording a new knock.
	programButton = 0;
  }
  
  if (currentKnockCount != secretKnockCount){
    return false; 
  }
  
  /*  Now we compare the relative intervals of our knocks, not the absolute time between them.
      (ie: if you do the same pattern slow or fast it should still open the door.) */
  int totaltimeDifferences=0;
  int timeDiff=0;
  for (i=0;i<maximumKnocks;i++){ // Normalize the times
    knockReadings[i]= map(knockReadings[i],0, maxKnockInterval, 0, 100);      
    timeDiff = abs(knockReadings[i]-secretCode[i]);
    if (timeDiff > rejectValue){ // Individual value too far out of whack
      return false;
    }
    totaltimeDifferences += timeDiff;
  }
  // It can also fail if the whole thing is too inaccurate.
  if (totaltimeDifferences/secretKnockCount>averageRejectValue){
    return false; 
  } 
  return true;
}


void triggerDoorUnlock(){
  sprintf(state,"Welcome!     ");
  print_lcd(0,state);
  int i=0;
  
  Motor_ON(); // turn the motor on for a bit.
  GreenLED_ON();   //Green LED is high
  
  // Blink the green LED a few times for more visual feedback.
  for (i=0; i < 10; i++){   
      GreenLED_OFF();    
      DrvSYS_Delay(100000); 
      GreenLED_ON();
      DrvSYS_Delay(100000); 
  }
	
  DrvSYS_Delay(lockTurnTime*1000);  // Wait a bit.
  Motor_OFF(); // Turn the motor off. 
	
}

void listenToSecretKnock(){ 
  int i = 0;
	
  // First lets reset the listening array.
  for (i=0;i<maximumKnocks;i++){
    knockReadings[i]=0;
  }
  
  int currentKnockNumber=0;      // Incrementer for the array.
  int startTime = timeCounter;  // Reference for when this knock started.
  int now = timeCounter;
  
  GreenLED_OFF();   // we blink the LED for a bit as a visual indicator of the knock.
  if (programButtonPressed==true){
     RedLED_OFF();     		// and the red ones too if we're programming a new knock.
  }
  DrvSYS_Delay(knockFadeTime*1000); // wait for this peak to fade before we listen to the next one.
  GreenLED_ON(); 
  if (programButtonPressed==true){
     RedLED_ON();                            
  }
  do {
		
    //listen for the next knock or wait for it to timeout. 

	//------------------------------TROUBLESHOT---------------------------------------
	//sprintf(conv,"knock %4d",knock);
	//print_lcd(2,conv);	
	//--------------------------------------------------------------------------------
    
	//if (knockSensorValue >= threshold){    //got another knock...
	if(knock > 0){
		knock = 0;
			
     		//record the delay time.
      		now = timeCounter; // count time since we began running the current program. Arduino: now=millis();
      		knockReadings[currentKnockNumber] = now-startTime;
      		currentKnockNumber ++;   //increment the counter
      		startTime = now;   // and reset our timer for the next knock
      		GreenLED_OFF();    //Green LED is low
		
      		if (programButtonPressed==true){
        		RedLED_OFF();          // and the red ones too if we're programming a new knock.
      		}
		
      		DrvSYS_Delay(knockFadeTime*1000);   // again, a little delay to let the knock decay.
      		GreenLED_ON();    //Green LED is high  
      
		if (programButtonPressed==true){
        		RedLED_ON();     // and the red ones too if we're programming a new knock.
		}	
	}
		
    	now = timeCounter;
    
    //did we timeout or run out of knocks?
  } while ((now-startTime < knockComplete) && (currentKnockNumber < maximumKnocks));
  
  //we've got our knock recorded, lets see if it's valid
  if (programButtonPressed == false){   // only if we're not in progrmaing mode.
    if (validateKnock() == true){
      triggerDoorUnlock(); 
	    
    } else {
      sprintf(state,"Wrong Knock     ");
      print_lcd(0,state);
      GreenLED_OFF();   //Green LED is low 
	    
      for (i=0;i<10;i++){					
        RedLED_ON();  // We didn't unlock, so blink the red LEDs as visual feedback.
        DrvSYS_Delay(100000); 
        RedLED_OFF();
        DrvSYS_Delay(100000); 
      }
	    
      GreenLED_ON();   //Green LED is high 
    }
	  
  } else { // if we're in programming mode we still validate the lock, we just don't do anything with the lock
	 
    validateKnock();
    // and we blink the reds alternately to show that program is complete.
    sprintf(state,"New lock stored");
    print_lcd(0,state);
    RedLED_Pattern();
  }

  programButton = 0;
}



void loop() {
  // Listen for any knock at all.
	
	if(Scankey() == programKey){
		programButton = programKey;
	}
	
	//------------------------------TROUBLESHOT---------------------------------------
	//sprintf(scan,"scan %d",programButton);
	//print_lcd(1,scan);
	//--------------------------------------------------------------------------------
		
  if (programButton == programKey){  // is the program button pressed?
    
	programButtonPressed = true;    // Yes, so lets save that state
    	RedLED_ON();        // and turn on two red lights too so we know we're programming.
		
	sprintf(state,"Programming     ");
	print_lcd(0,state);
	  
  } else {
	programButtonPressed = false;
    	RedLED_OFF(); 
		
	sprintf(state,"Listening      ");
	print_lcd(0,state);	
  }
  
  //if (knockSensorValue >= threshold){
	if(knock > 0){
		knock = 0;
		listenToSecretKnock();
  	}
} 

int main (void) { 
	setup();
	while(1){ 
		loop();
		DrvSYS_Delay(50000); //50ms				
	 }
}
