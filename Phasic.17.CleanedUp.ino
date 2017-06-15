/*
This code was modified on October 6, 2016 by Nicholas Benzoni under the guidance of Dr. Cassandra Telenko for. The code is supposed to read from a 6.3kHz piezo
electric transducer on an ARDUINO MEGA. The transducer should be attached to the actual metal part of the faucet of a sink. The code then estimates and records 
the flowrate used at the faucet, and does many other things, like give feedback to the users in various states. The code is intended to be run for at least 50 days, 
and up to 80. The phases used are generally outlined in He et al.'s paper "One Size Does Not Fit All", and adapted to water usage at the primary kitchen sink in a household.

There are three main states in this code with respect to what the faucet is doing;
1. Idle: the faucet is off. The device will be displaying Tips, changing regularly (or simply the word Baseline and the day it is)
2. Active: The faucet is currently running water. The code has 6 different actions for this phase; 
 i. More advanced baseline, also displaying the hour, mostly to tell if the code is recognizing on/off
 ii. Simply sayign Hello, so the user knows the device recognizes on/off in the Pre-Contemplation Phase
 iii. A declaration of what the given flow is good for be it high or low
 iv. A bar showing the relative flowrate of the sink from Off-Max
 v. Big text from OFF-HIGH, or same as iv
 vi. Same as ii.
3. Just Off: the faucet was just turned off a second or so ago, this state will do vartious things, as the active state, but most importantly shoes the esimate for how much
water was just used

I believe the code can be improved vastly and made more simple in many weays. It was my first Arduino project, so please excuse any bulkiness or inconvenience. Any questions
may be directed to my email nickbenzoni@gmail.com, though I will do my best to comment the code sufficiently enough to make such contact unnecessary.

Other hardware notes: Using a LM6132 op-amp, with a gain of about 20, achieved with a 560Ohm resistor, and a 10kOhm resistor. There is a 1MOhm resistor between the poisitive
and negative leads of the piezo transducer, and a 1kOhm resistor between the negative lead and ground. There is also a capacitor of 1-10microF between power and ground,
it is also VERY IMPORTANT to have an actual wire attached to the ground and earth ground, otherwise the signal will be very noisy when attached to only the wall-power adapter.
There is also a micro-sd card module ont he board attached to pins 50-54, and an LCD screen installed as described on adafruits website. The LCD is the 20x4 adafruit standard with
the white font on a blue background. Previous versions of this code are made for use with the adafruit screen with RGB backglight. 

The PCB file for the shield should be in some provided documentation, or Mr. Ahn Nguyen has it in a fodler titled Benzoni.
There should also be included the lasercut schematic for the packaging, which is to be done on a 1/8" 12"x24" piece of acrylic and makes 2.5 housings.
*/

// include the library code:
#include <LiquidCrystal.h>
#include <SD.h>
#include <SPI.h>

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

int inputPin = A0;              // pin on board that the amplification circuit is hooked up to

File myFile;                    //just for checking the SD card is working in the void setup
File UseSum;                    // saves to the usesummaries file, at each off-on-off instance

const int numReadings = 20;     //number of readings for the running average used in ptp calculations
const int ptpTime = 150;        //how many times the loop should run before actually outputting or changing anything, approximately proportionate to 1.6ms each
int count = 0;                  // timer for output for the ptp output above
const int idleSwitch = 10;      //10 = 1.6 seconds of showing per tip 1900 for 5 min
const int activeSwitch = 2;     //to make active screen not fresh too fast
double lam = 0.5;               //Lambda of EWMA calculations, larger = faster and less smooth, typically between 0.5-0.1
const int stdDevReads = 20;     //Number of readings for the standard deviation fo the signal

//All below values need to be set with EACH calibration

double FlowEWMA[6] = {0, 410, 420, 500, 550, 600}; // thresholds (EWMA or stdDEWMA) for {off, vlow, low, med, high, vhigh}                                 CALIBRATE[ ]
double EWMA = 389; //initialize to 'off' value                                                                                                             CAILBRATE[ ]
double MaxFlow = 0.15; //should be in L/ms, set to MAX FLOW from sink                                                                                      CAILBRATE[ ]
double stdDEWMA = 11; //off value                                                                                                                          CAILBRATE[ ]
bool useEWMA = false; //true for ptpEWMA , false if standard deviation (usually use standard deviation)                                                     CAILBRATE[ ]

double thresh[2] = {100, 600}; //set to split code into one, two or three polynomials. 0-thresh[0], thresh[0]-thresh[1], thresh[1]-inf                     CAILBRATE[ ]
//set thresh[0] very high if you want to only use a single polynomial to fit the data
//the polynomial values are retrieved from you Excel file, the y axis is flow in L/s, the x axis is either EWMA (useEWMA = true) 
//or stdD (standard deviation) (useEWMA = false)

//Polynomial 1 ceAx^2+ceBx+ceC=flow 0-thresh[0]                                                                                                            CAILBRATE[ ]
double ceA = 0;
double ceB = 0;
double ceC = 0;

//Polynomial 2 ceA1X^2+ceB1x+ceC1 = flow //for chunking at given threshold thresh[0]-thresh[1]                                                              CAILBRATE[ ]
double ceA1 = 0;
double ceB1 = 0;
double ceC1 = 0;

//Polynomial ceA1X^2+ceB1x+ceC1 = flow //for chunking at given threshold >thresh[1]                                                                          CAILBRATE[ ]
double ceA2 = 0;
double ceB2 = 0;
double ceC2 = 0;

float stdD = 0;         //all std dev only
float dev[stdDevReads]; //array fed into the standard deviation function 
int stdIndex = 0;       //used to control the above array

int ok = 1;                     // just for trimming of beignning data during calibration, as the serial buffer will have data in it to be ignored
int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
int total = 0;                  // the running total
int average = 200;              // the average
int activeCounter = 0;          //timer for active screen
int idleCounter = 0;            //timer for idle screen

//these valeus are used to generate a peak to peak signal of the raw sensor reading
//mxm = maximum, mnm = minimum, ptp = peak to peak of a given number of averages of readings
//this whole system could probably be improved
double mxm = 0;
double mnm = 0;
double ptp = 0;

//variables used to determine the type of flow, from Off, Very Low, Low, Med, High, Very High
int whichFlow = 0;
String lastFlow = "Off";
String flowNow = "Off";
String flow = "Off";
int flowCount = 0;
int JustOffAction = 0;

//PHASES
int PhaseNow = 0;
int PhaseDay[5] = {14, 21, 28, 35, 49}; //BL, PC, C, PR, A, M 50 days total
int whichAction = 0; //0: Big text 1: Waste not want not 2: Raining
int whichTip = 0;    //which tip from #1-#52 to display
long actionTip = 0;  //timer to change the tip every set number of hours in ms

//These are boolean values to determine if the sink is off or JUST turned off
bool justOff = 0;
bool On = 0;
bool tipSwitch = false;

//the "this" values are for saving the current use, which are saved in
// the UseSums file, after each off-on-off recognition, so there is a single
// line int he file for each usage
unsigned long startUse = 0;
unsigned long endUse = 0;
unsigned long thisElapsed = 0;
double thisEWMA = 0;
double thisStd = 0;
double thisEWMAAvg = 0;
double thisStdAvg = 0;
double thisAvgFlow = 0;
unsigned long thisCounter = 0;
float thisWater = 0; //water used on a single off-on-off instance

//stuff for daily/weekly breakdown
int Today = 0;
int thisHour = 0;
double CurrentFlow = 0; //flow calculated live while sink is on (for bar display)
long checkTime = 0; //just for knwoing when 3 minutes has passed for the program to check the hour and day

unsigned long HourCheck = 0; //for timing in the whatHour() and whatDay() functions
unsigned long DayCheck = 0;

// All the below variables are used to determine the daily statistics
// each entry in each array represents data of a single day
unsigned long todayOnTime = 0;
unsigned long todayTimeEWMA = 0;
unsigned long todayAvgEWMA = 0;
unsigned long DailyOnTime[80] = {0}; //total time a sink is on in a given day
unsigned long DailyTimeEWMA[80] = {0}; //EWMA * total time, so one can determine the average EWMA from a day
unsigned long DailyStdDevTime[80] = {0}; //stdD * total time, so one can determine the average stdD from a day
double DailyWater[80] = {0}; //daily estimated water by the polynomial
int DailyUses[80] = {0}; //total number of off-on-off instances in a given day

// All byte functions delcared below are for the custom characters
// for printing the big text in the active phase

byte drop[8] = {
  0b00000,
  0b00100,
  0b01110,
  0b11111,
  0b11111,
  0b01110,
  0b00000,
  0b00000
};

byte Full[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111
};
byte MCenter[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b01110,
  0b00100,
  0b00000
};
byte MTop[8] = {
  0b00000,
  0b10001,
  0b11011,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111
};
byte WBottom[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11011,
  0b10001,
  0b00000
};
byte Wcenter[8] = {
  0b00000,
  0b00100,
  0b01110,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111
};
byte GRight[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b11111,
  0b11111,
  0b11111,
  0b11111
};
byte ECenter[8] = {
  0b00000,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b00000
};


void setup() {

  // initialize serial communication with computer:
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  // initialize all the readings to 0:
  for (int thisReading = 0; thisReading < numReadings; thisReading++) {
    readings[thisReading] = 0;
  }

  for (int thisReading = 0; thisReading < stdDevReads; thisReading++) {
    dev[thisReading] = 0;
  }

  // set up the LCD's number of columns and rows:
  lcd.begin(20, 4);

  //first display which program is being used
  lcd.print(F("Phasic 17 Cleaned Up"));

  //create all custom characters for BIG TEXT
  lcd.createChar((byte)0, Full);
  lcd.createChar(1, MCenter);
  lcd.createChar(2, MTop);
  lcd.createChar(3, WBottom);
  lcd.createChar(4, Wcenter);
  lcd.createChar(5, GRight);
  lcd.createChar(6, ECenter);
  lcd.createChar(7, drop);

  //initialize SD
  // value of 53 is specific to ARDUINO MEGA, will be different for other Arduino boards
  if (!SD.begin(53)) {
    Serial.println(("SD Failed"));
    lcd.clear();
    lcd.print("SD Failed. Try again.");
    delay(30000);
    return;
  }
  lcd.clear();
  lcd.print("SD Initialized");
  delay(100);

  //Open file, make sure it worked
  myFile = SD.open("AllOn.txt", FILE_WRITE);
  if (myFile) {
    lcd.clear();
    lcd.write("File opened");
    myFile.println("File opened");
    myFile.close();
    delay(100);
  }
  else {
    lcd.clear();
    lcd.write("File failed to open");
    delay(30000);
  }
  
// Fills in values of empty arrays to the array values stored on the sd cards
  if (CheckArrays()) {
    lcd.clear();
    lcd.write("Memory ready.");
    delay(100);
  } else {
    lcd.clear();
    lcd.write("Arrays not intialized.");
    delay(3000);
  }
  
  //check the last saved day and hour and set values as so
  Today = whatDay();
  thisHour = whatHour();
  lcd.clear(); 
  lcd.write("Ready!");
  delay(500);
}


void loop() {

  // TAKE READING AND CALCULATE STANDARD DEVIATION AND RUNNING AVERAGE
  total = total - readings[readIndex]; // subtract the last reading:
  readings[readIndex] = analogRead(inputPin); // read from the sensor:
  total = total + readings[readIndex]; // add the reading to the total:
  readIndex++; // advance to the next position in the array:
  average = total / numReadings; // calculate the average

  dev[stdIndex] = readings[readIndex - 1]; //add reading to running stdDev
  stdD = standard_deviation(dev, stdDevReads); //calc stdev
  stdIndex++;

  if (stdIndex >= stdDevReads) {
    stdIndex = 0; //return to beginning of running std dev calc
  }

  if (readIndex >= numReadings) { // if we're at the end of the array...
    readIndex = 0; // ...wrap around to the beginning:
  }

  //PTP Calculations gather max and min
  if (count <= ptpTime) {
    //check if average is max or min, cut super high
    if (average >= mxm && average < 800) {
      mxm = average; // high values for ptp
    }
    if (average <= mnm) { //low values for ptp
      mnm = average;
    }
  }

  //Demarcate buffer data, for DAQ DELETE FOR REAL one
  if ( ok == 1) {
    lcd.clear();
    lcd.write("OK");
    Serial.println("Start Here:");
    ok = 0;
    lcd.clear();
  }

  // EWMA CALCULATED ------------------------------------------------------- EWMA CALCULATED -------------------------------------------------------
  if (count >= ptpTime) {
    ptp = mxm - mnm; //set ptp
    EWMA = lam * ptp + (1 - lam) * EWMA ;//EWMA Calculation
    stdDEWMA = lam * stdD + (1 - lam) * stdDEWMA;

    Serial.print(millis()); //Serial output for Processing/DAQ ----- // out for real one
    Serial.print("\t");
    Serial.print(EWMA);
    Serial.print("\t");
    Serial.println(stdD);

    //Check if flow was just TURNED ON (from off)
    if (flowNow != "Off" && On == 0 && lastFlow == "Off") {
      //turn flow state ON
      On = 1;
      startUse = millis();
      thisStd = 0;
      thisEWMA = 0;
      //Which just off action?
    }

    //Check if flow was just TURNED OFF (from any state) -------- JUST TURNED OFF STATE CHANGE
    //Save newline for this use
    if (On == 1 && flowNow == "Off") {
      //Turn state OFF
      On = 0;
      justOff = 1;
      //calculate length of last usage
      endUse = millis();
      thisElapsed = endUse - startUse;
      //save all of it in one nice 5 column file
      UseSum = SD.open("UseSums.txt", FILE_WRITE);
      if (UseSum) {
        thisEWMAAvg = thisEWMA / thisCounter;
        thisStdAvg = thisStd / thisCounter;
        //estimate water as a second order polynomial
        if (useEWMA) { //EWMA Used for flow calc
          if (thisEWMAAvg <= thresh[0]) {
            thisAvgFlow = (ceA * thisEWMAAvg * thisEWMAAvg + ceB * thisEWMAAvg + ceC); // STD DEV (ceA * thisStdAvg * thisStdAvg + ceB * thisStdAvg + ceB);
          } else if (thisEWMAAvg > thresh[0] && thisEWMAAvg <= thresh[1]) {
            thisAvgFlow = (ceA1 * thisEWMAAvg * thisEWMAAvg + ceB1 * thisEWMAAvg + ceC1);
          } else {
            thisAvgFlow = (ceA2 * thisEWMAAvg * thisEWMAAvg + ceB2 * thisEWMAAvg + ceC2);
          }
        }

        else { //stdD Used
          if (thisStdAvg <= thresh[0]) {
            thisAvgFlow = (ceA * thisStdAvg * thisStdAvg + ceB * thisStdAvg + ceB);
          } else if (thisStdAvg > thresh[0] && thisStdAvg <= thresh[1]) {
            thisAvgFlow = (ceA1 * thisStdAvg * thisStdAvg + ceB1 * thisStdAvg + ceC1);
          } else {
            thisAvgFlow = (ceA2 * thisStdAvg * thisStdAvg + ceB2 * thisStdAvg + ceC2);
          }
        }


        if (thisAvgFlow > MaxFlow) { //can never go mroe than emasured max flow
          thisAvgFlow = MaxFlow;
        }
        if (thisAvgFlow < 0) { //or below 0 flow...
          thisAvgFlow = 0;
        }

        thisWater = thisElapsed * thisAvgFlow; //CHOOSE WHICH METHOD TO CALCULATE WATER
        //Check what day / hour it is
        UseSum.print(Today); //DAY:HR:TIME:EWMA:STDD:H2O
        UseSum.print("\t");
        UseSum.print(thisHour);
        UseSum.print("\t");
        UseSum.print(thisElapsed);
        UseSum.print("\t");
        UseSum.print(thisEWMAAvg);
        UseSum.print("\t");
        UseSum.print(thisStdAvg);
        UseSum.print("\t");
        UseSum.println(thisWater);
        UseSum.close();
        //Add values to the daily totals
        DailyOnTime[Today] += thisElapsed;
        DailyTimeEWMA[Today] += thisEWMAAvg * thisElapsed;
        DailyStdDevTime[Today] += thisStdAvg * thisElapsed;
        DailyWater[Today] += thisWater; //OR this Water, or some combo!!! <------------- DAILY H2O calc.
        DailyUses[Today]++;
        if (!SaveArrays()) { //Save arrays or show error
          lcd.clear();
          lcd.write("Arrays not saved,");
          lcd.setCursor(0, 1);
          lcd.write("please contact");
          lcd.setCursor(0, 2);
          lcd.write("gtwaterstudy@gatech.");
          lcd.setCursor(0, 3);
          lcd.write("Or 6263908943");
          delay(30000);
        }
      }
      else { //save use sum or show error
        lcd.clear();
        lcd.print("ERROR UseSum not opened");
        delay(30000);
      }

      //Reset all values to 0
      thisEWMA = 0;
      thisStd = 0;
      thisCounter = 0;
      startUse = 0;
      endUse = 0;
    }

    //ACTIVE STATE ====================== ACTIVE STATE ==================== ACTIVE STATE ================== ACTIVE STATE ================ ACTIVE STATE ================
    if (On) {
      thisStd += stdD; //to determine the average STD and EWMA of this usage, for calculating total used
      thisEWMA += EWMA;
      thisCounter ++;
      activeCounter++;
      if (activeCounter > activeSwitch) {


        if (useEWMA) { //EWMA Used for flow calc
          if (thisEWMAAvg <= thresh[0]) {
            CurrentFlow = (ceA * EWMA * EWMA + ceB * EWMA + ceC);
          } else if (thisEWMAAvg > thresh[0] && thisEWMAAvg <= thresh[1]) {
            CurrentFlow = (ceA1 * EWMA * EWMA + ceB1 * EWMA + ceC1);
          } else {
            CurrentFlow = (ceA2 * EWMA * EWMA + ceB2 * EWMA + ceC2);
          }
        }
        else { //stdD Used
          if (thisStdAvg <= thresh[0]) {
            CurrentFlow = (ceA * stdDEWMA * stdDEWMA + ceB * stdDEWMA + ceC);
          } else if (thisStdAvg > thresh[0] && thisStdAvg <= thresh[1]) {
            CurrentFlow = (ceA1 * stdDEWMA * stdDEWMA + ceB1 * stdDEWMA + ceC1);
          } else {
            CurrentFlow = (ceA2 * stdDEWMA * stdDEWMA + ceB2 * stdDEWMA + ceC2);
          }
        }

        if (CurrentFlow > (MaxFlow * 1000)) { //make sure not less than 0 or more than possible
          CurrentFlow = (MaxFlow * 1000);
        }
        if (CurrentFlow < 0) {
          CurrentFlow = 0;
        }

        if (whichAction == 0) { //Baseline
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Baseline Phase");
          lcd.setCursor(0, 1);
          lcd.print("Day ");
          lcd.print(Today);
          lcd.setCursor(0, 2);
          lcd.print("Hour ");
          lcd.print(thisHour);
        } else if (whichAction == 1) { //Hello!
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Hello!");
          lcd.setCursor(0, 1);
          lcd.print("Day ");
          lcd.print(Today);
          lcd.setCursor(0, 2);
          lcd.print("Hour ");
          lcd.print(thisHour);
        } else if ( whichAction == 3) { //Waste not, want not
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Is the water running");
          lcd.setCursor(0, 1);
          lcd.print("in the background?");
        } else if (whichAction == 4) { //Low flow / high good for
          if (whichFlow == 3 || whichFlow == 4 || whichFlow == 5) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("High flow is best");
            lcd.setCursor(0, 1);
            lcd.print("for filling pots,");
            lcd.setCursor(0, 2);
            lcd.print("cups, and ");
            lcd.setCursor(0, 3);
            lcd.print("waterbottles");
          } else {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Low flow is good");
            lcd.setCursor(0, 1);
            lcd.print("for most uses.");
          }
        } else if ( whichAction == 5) { //bar from off to max
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Current Flow:");
          lcd.setCursor(0, 2);
          lcd.print("OFF");
          lcd.setCursor(3, 1);
          for (int i = 3; i <= 16 * (CurrentFlow / (1000 * MaxFlow)); i++) {
            lcd.setCursor(i, 1);
            lcd.write((byte)0);
            lcd.setCursor(i, 2);
            lcd.write((byte)0);
            lcd.setCursor(i, 3);
            lcd.write((byte)0);
          }
          lcd.setCursor(17, 2);
          lcd.print("MAX");
        }  else if ( whichAction == 6) { //BIG TEXT
          lcd.clear(); // Populate LCD Screen with Live Usage Information
          lcd.write("Raw Signal: ");
          lcd.print(EWMA);
          lcd.setCursor(0, 1);
          //Just to print the big text
          displayBigtext(whichFlow);
        }
        activeCounter = 0; //reset active timer, to make sure the screen does not refresh too fast.
      }
    }

    //IDLE STATE ********************* IDLE STATE ********************* IDLE STATE ********************* IDLE STATE ********************* IDLE STATE *********************
    else {
      idleCounter++;              //next active, random is (min, max-1)
      //JUST OFF ACTION STATE +++ +++ +++ +++ +++ JUST OFF ACTION STATE +++ +++ +++ +++ +++  JUST OFF ACTION STATE +++ +++ +++ +++ +++  JUST OFF ACTION STATE +++ +++ +++ +++ +++
      if (justOff) {
        if (JustOffAction == 0) { //baseline day prints same thing
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Baseline Day ");
          lcd.print(Today);
        }
        if (JustOffAction == 1) { //print goodbye after turn sink off
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Goodbye!");
        }
        if (JustOffAction == 2) { //estimate
          //Display the right after breakdown
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.write("You just used about ");
          lcd.setCursor(0, 1);
          lcd.print(thisWater);
          lcd.write( " liters.");
        }
        if (JustOffAction == 3) {
          //Say how much you have used today versus yesterday
          //convert to percentage
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.write("Today's usage so far");
          lcd.setCursor(0, 1);
          lcd.write("about ");
          lcd.print(DailyWater[Today]);
          lcd.write(" L.");
        }
        if (JustOffAction == 4) {
          // Another justoff action state
        }
        if (idleCounter > 93) { //ten seconds
          //No longer "just turned off"
          justOff = 0;
          thisElapsed = 0;
          thisEWMAAvg = 0;
          thisStdAvg = 0;
          idleCounter = 0;
        }
      } else {
        if (idleCounter > idleSwitch) { // :::::::::::::::: IDLE TIPS ::::::::::::::::::::: IDLE IDLE IDLE :::::::::::::::::::::
          char thisTip [8] = "###.TXT";
          //int whichTip = random(1, 12); //MAX+1 and Min should be range of tips for given state
          //convert random int into char array for being called filename for each tip
          if (whichTip > 0) {//convert tip number to char array and call on sd card
            thisTip[0] = whichTip / 100 + '0';
            thisTip[1] = (whichTip % 100 / 10) + '0';
            thisTip[2] = whichTip % 100 % 10 + '0';
            displayText77(thisTip);
          } else if (whichTip == 0) { //baseline
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Baseline Day ");
            lcd.print(Today);
          }  else if (whichTip == -1) { //display daily use
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.write("Today about ");
            lcd.print(DailyWater[Today]);
            lcd.setCursor(0, 1);
            lcd.write("have been used at");
            lcd.setCursor(0, 2);
            lcd.write("this sink.");
          }
          idleCounter = 0;
          //SET PHASE ____________________________ SET PHASE __________________________ SET PHASE ___________________ SET PHASE ___________________
          //in the set phase area I need to have each phase specify what range of tips to be used
          // which action and post action screens to be used.
          
          if (Today < PhaseDay[0]) { //BASELINE PHASE
            //No tips just display date
            whichTip = 0;
            PhaseNow = 0;
            JustOffAction = 0;
            whichAction = 0;
          }

          else if (Today >= PhaseDay[0] && Today < PhaseDay[1]) { //PRECONTEMPLATION
            if (thisHour == 0 && !tipSwitch) { //change tip at 12AM
              whichTip++;
              tipSwitch = true;
            } else if (thisHour == 14 && tipSwitch) { //and at 2PM
              whichTip++;
              tipSwitch = false;
            }
            if (whichTip == 11) {
              whichTip = 1;
            }
            PhaseNow = 1;
            JustOffAction = 1;
            whichAction = 1;
            //Tip range 1-10
          }

          else if (Today >= PhaseDay[1] && Today < PhaseDay[2]) { //CONTEMPLATION
            PhaseNow = 2;
            JustOffAction = 1;
            whichAction = 4;
            if (thisHour == 0 && !tipSwitch) { //change tip at 12AM
              whichTip++;
              tipSwitch = true;
            } else if (thisHour == 14 && tipSwitch) { //and at 2PM
              whichTip++;
              tipSwitch = false;
            }
            if (whichTip == 18) {
              whichTip = 9;
            }
            //Tip range 9-17
          }

          else if (Today >= PhaseDay[2] && Today < PhaseDay[3]) { //PREPARATION
            JustOffAction = 2;
            PhaseNow = 3;
            whichAction = 5;
            if (millis() - actionTip > 14400000) { //change tip every 4 hours
              whichTip++;
              actionTip = millis();
            }
            if (whichTip == 31) {
              whichTip = 15;
            }
            //tip range 15-30
          }

          else if (Today >= PhaseDay[3] && Today < PhaseDay[4]) { //ACTION
            if (millis() - actionTip > 3600000) { //change tip each hour
              whichTip++;
              actionTip = millis();
            }
            if (whichTip == 53) {
              whichTip = 25;
            }
            whichAction = random(5, 7);
            JustOffAction = random(2, 4);
            PhaseNow = 4;
            //tip range 25-52
          }

          else { //MAINTENANCE Goes until device off
            PhaseNow = 5;
            JustOffAction = random(2, 4);
            whichAction = 1;
            if (millis() - actionTip > 3600000) { //change tip each hour
              whichTip = random(1, 53);
              actionTip = millis();
            }
          } // maintenance end
        } //idle tip end
      }//closed here
    }
    // classify flow into 6 categories
    if (useEWMA) {
      flow = classifyFlow(EWMA);
    } else {
      flow = classifyFlow(stdDEWMA);
    }

    //the flow has changed to a new kind, and the time spent at
    if ( flow != flowNow) {
      flowCount ++;
      //make sure flow has changed for 3 periods before switching
      if (flowCount >= 3) {
        lastFlow = flowNow;
        flowNow = flow; //the current flow is registered
        flowCount = 0;
      }
    }

    mxm = 0; //reset PTP
    mnm = 0;
    count = 0; //reset count
  } //end of the output step (count)
  count++; //add to count for PTP window
  if (millis() - checkTime > 180000) { //jus to make sure the day and hour are checked every 3 mins
    checkTime = millis();
    thisHour = whatHour();
    Today = whatDay();
  }
} // ------------------------------------------------------------------------- END OF VOID LOOP -------------------------------------------------------------------





//This function prints OFF LOW MED or HIGH to the lcd screen in 3x3 letters
//OFF if arg is 0 LOW if arg is 1 or 2, med if arg is 3 HIGH if arg is 4 or 5
void displayBigtext(int flownumber) {
  if (flownumber == 0) {
    printOff();
  }
  else if (flownumber == 1 || flownumber == 2) {
    printLow();
  } else if (flownumber == 3) {
    printMed();
  } else if (flownumber == 4 || flownumber == 5) {
    printHigh();
  } else {
    return;
  }
}

//Classifies flow into off, vlow, low, med, high, vhigh dependign on the thresholds set 
//in the FlowEWMA array
String classifyFlow(double EWMA){
  String flowRate = "Off";
  if (EWMA < FlowEWMA[1]) {
    whichFlow = 0; // move register
    flowRate = "Off"; //Name the flow rate
  }
  if (EWMA >= FlowEWMA[1] && EWMA <= FlowEWMA[2]) {
    whichFlow = 1;
    flowRate = "Very Low";
  }
  if (EWMA > FlowEWMA[2] && EWMA <= FlowEWMA[3]) {
    whichFlow = 2;
    flowRate = "Low";
  }
  if (EWMA > FlowEWMA[3] && EWMA <= FlowEWMA[4]) {
    whichFlow = 3;
    flowRate = "Med";
  }
  if (EWMA > FlowEWMA[4] && EWMA <= FlowEWMA[5]) {
    whichFlow = 4;
    flowRate = "High";
  }
  if (EWMA > FlowEWMA[5]) {
    whichFlow = 5;
    flowRate = "Very High";
  }
  return flowRate;
}

float standard_deviation(float data[], int n) //Calculates the standard deviation of an array
{
  float mean = 0.0, sum_deviation = 0.0;
  int i;
  for (i = 0; i < n; ++i)
  {
    mean += data[i];
  }
  mean = mean / n;
  for (i = 0; i < n; ++i)
    sum_deviation += (data[i] - mean) * (data[i] - mean);
  return sqrt(sum_deviation / n);
}

int whatDay() { //reads a file called Day and outputs the 1-2 digit int ---------------------- WHAT DAY ------------
  File Day;
  String inString = "";
  int i = 0;
  int LastDay = 0;
  //read what day it remembers it being from Days.txt
  Day = SD.open("Days.txt");
  if (Day) {
    while (Day.available()) {
      char inDay = Day.read();
      inString += inDay;
    }
  } else {
    lcd.clear();
    lcd.print("Error open Days");
    delay(30000);
  }
  Day.close();

  LastDay = (int)inString.toFloat();
  inString = "";
  return LastDay;
}

//Most imporatnat timing funcitons, will check if an hour has passed, a day has passed, and will 
// read and write from and to the Days and Hour .txt files, this keeps track of where the program is even
// if the arduino gets turned off, so it wont lose its place in the phases or arrays
int whatHour() { 
  File Hour;
  File Day;
  String inHour = "";
  String inString = "";
  int LastHour = 0;
  int LastDayRead = 0;
  //read what day it remembers it being from Days.txt
  Hour = SD.open("HOUR.TXT");
  //Read hour file
  if (Hour) {
    while (Hour.available()) {
      char hourread = Hour.read();
      inHour += hourread;
    }
  } else {
    lcd.clear();
    lcd.print("ERROR Hour File");
    delay(30000);
  }
  Hour.close();
  LastHour = (int)inHour.toFloat(); //Set Last Hour to read from file
  inHour = "";
  //Has time elapsed to change the file?
  //Is there an hour difference between current millis() and last
  //HourCheck?
  if ((millis() - HourCheck) > 3600000) { //change back to 24 and delete modifier
    HourCheck = millis();
    LastHour++;
    if (LastHour == 24) { //keep it on 24hr clock, if day changed, change day
      LastHour = 0;
      Day = SD.open("Days.txt"); //open and read day
      if (Day) {
        while (Day.available()) {
          char inDay = Day.read();
          inString += inDay;
        }
        LastDayRead = (int)inString.toFloat();
        inString = "";
        LastDayRead++; //change day
      } else {
        lcd.clear();
        lcd.print("ERROR Read Day");
        delay(30000);
      }


      SD.remove("Days.txt");
      Day = SD.open("Days.txt", FILE_WRITE);
      if (Day) {
        Day.print(LastDayRead);
      } else {
        lcd.clear();
        lcd.print("ERROR Day File");
        delay(3000);
      }
      Day.close();
    }
    SD.remove("HOUR.TXT"); //change hour file if hour passed
    Hour = SD.open("HOUR.TXT", FILE_WRITE);
    if (Hour) {
      Hour.print(LastHour);
    }
    else {
      lcd.print("Error changing Hour");
      delay(30000);
    }
    Hour.close();
  }
  return LastHour;
}

void displayText77( char OpenFile[8]) { //tip names should be in format "###.txt", and have an empty final character
  int idx = 0;
  int y = 0;
  int x = 0;
  char tRead;
  char tLast;
  char tPrinted;
  File tipFile;

  tipFile = SD.open(OpenFile);
  lcd.clear();
  if (tipFile) {
    while (tipFile.available()) {
      tPrinted = tLast;
      tLast = tRead;
      tRead = tipFile.read();
      if (idx >= 1) {
        if (x == 19) {
          if (tPrinted == ' ') {
            lcd.print(' ');
            x++;
          } else if (tRead != ' ' && tLast != ' ' && tPrinted != ' ') {
            lcd.print("-");
            x++;
          }
        }
        if (x == 20) {
          y++;
          x = 0;
        }
        lcd.setCursor(x, y);
        if (x == 0 && tLast == ' ') {
          ;//do nothing and skip this char
        }
        else {
          lcd.print(tLast);
          x++;
        }
      }
      idx++;
    }
    tipFile.close();
  } else {
    lcd.clear();
    lcd.print("File not found");
    tipFile.close();
  }
}

bool SaveArrays() { //seems to work at 12PM 9-21, except saves it three times...
  /*need to save each of the six arrays, in this order, to the sd file Array.txt
    unsigned long DailyOnTime[70] = {0};
    unsigned long DailyTimeEWMA[70] = {0};
    unsigned long DailyStdDevTime[70] = {0};
    double DailyWater[70] = {0};
    int DailyUses[70] = {0};
  */
  if (!SD.remove("ARRAYS.TXT")) {
    lcd.clear();
    lcd.print("ERROR Remove array"); //remove old one to make new one
    delay(5000);
  }
  File Array = SD.open("Arrays.txt", FILE_WRITE);
  if (Array) {
    for (int i = 0; i <= 79; i++) { //DailyOnTime
      Array.print(DailyOnTime[i]);
      if (i < 79) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 79; i++) { //DailyTimeEWMA
      Array.print(DailyTimeEWMA[i]);
      if (i < 79) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 79; i++) { //DailyStdDevTime
      Array.print(DailyStdDevTime[i]);
      if (i < 79) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 79; i++) { //DailyWater
      Array.print(DailyWater[i]);
      if (i < 79) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 79; i++) { //DailyUses
      Array.print(DailyUses[i]);
      if (i < 79) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    Array.close();
    return true;
  }
  else {
    return false;
  }


}

bool CheckArrays() {
  /* Need to have a file that reads each array from the file, and fills it in (in case of unplug)
      in this order, each entry is seperated by a \t and each array is seperated by a \n
    unsigned long DailyOnTime[70] = {0}; ArrayNo = 0
    unsigned long DailyTimeEWMA[70] = {0}; ArrayNo = 1
    unsigned long DailyStdDevTime[70] = {0}; ArrayNo = 2
    double DailyWater[70] = {0}; ArrayNo = 3
    int DailyUses[70] = {0}; ArrayNo = 4
  */
  File Array;
  String inString = "";
  int NewlineCounter = 0;
  int TimesRan = 0;
  int ArrayNo = 0;
  long startTime = 0;
  int i = 0;
  Array = SD.open("ARRAYS.TXT");
  lcd.clear();
  lcd.print("Checking arrays");
  lcd.setCursor(0, 1);
  if (Array) {
    while (Array.available()) {
      lcd.setCursor(0, 1);
      lcd.print(TimesRan++);
      char inChar = Array.read();
      if (inChar == '\n') { //see if the array is over (demarcated by \n)
        ArrayNo++;
        i = 0;
        continue; //dont write this char to anything
      }
      if (ArrayNo == 0) {
        if (inChar != '\t') {
          inString += (char)inChar;
        } else { //the character was a \t, so write and move to next
          DailyOnTime[i] = (unsigned long)inString.toFloat();
          inString = ""; //after writing the stroing to an array, reinitialize that beezy!!!
          i++;
        }
      }
      if (ArrayNo == 1) {
        if (inChar != '\t') {
          inString += (char)inChar;
        } else { //the character was a \t, so write and move to next
          DailyTimeEWMA[i] = (unsigned long)inString.toFloat();
          inString = "";
          i++;
        }
      }
      if (ArrayNo == 2) {
        if (inChar != '\t') {
          inString += (char)inChar;
        } else { //the character was a \t, so write and move to next
          DailyStdDevTime[i] = (unsigned long)inString.toFloat();
          inString = "";
          i++;
        }
      }
      if (ArrayNo == 3) {
        if (inChar != '\t') {
          inString += (char)inChar;
        } else { //the character was a \t, so write and move to next
          DailyWater[i] = inString.toFloat();
          inString = "";
          i++;
        }
      }
      if (ArrayNo == 4) {
        if (inChar != '\t') {
          inString += (char)inChar;
        } else { //the character was a \t, so write and move to next
          DailyUses[i] = (int)inString.toFloat();
          inString = "";
          i++;
        }
      }
    }
    return true;
  }
  else {
    return false;
  }

}

void printOff() {
  //write O
  lcd.setCursor(0, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(0, 2);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  lcd.setCursor(0, 3);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  //Write F
  lcd.setCursor(4, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(4, 2);
  lcd.write((byte)0);
  lcd.write((byte)6);
  lcd.write(" ");
  lcd.setCursor(4, 3);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write(" ");
  //Write F
  lcd.setCursor(8, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(8, 2);
  lcd.write((byte)0);
  lcd.write((byte)6);
  lcd.write(" ");
  lcd.setCursor(8, 3);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write(" ");
}

void printMed() {
  //write M
  lcd.setCursor(0, 1);
  lcd.write((byte)0);
  lcd.write((byte)2);
  lcd.write((byte)0);
  lcd.setCursor(0, 2);
  lcd.write((byte)0);
  lcd.write((byte)1);
  lcd.write((byte)0);
  lcd.setCursor(0, 3);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  //Write E
  lcd.setCursor(4, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(4, 2);
  lcd.write((byte)0);
  lcd.write((byte)6);
  lcd.write(" ");
  lcd.setCursor(4, 3);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  //Write D
  lcd.setCursor(8, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.setCursor(8, 2);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  lcd.setCursor(8, 3);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write(" ");
}

void printHigh() {
  //write H
  lcd.setCursor(0, 1);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  lcd.setCursor(0, 2);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(0, 3);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);

  //write I, no serif
  lcd.setCursor(4, 1);
  lcd.write((byte)0);
  lcd.setCursor(4, 2);
  lcd.write((byte)0);
  lcd.setCursor(4, 3);
  lcd.write((byte)0);

  //write G
  lcd.setCursor(6, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(6, 2);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)5);
  lcd.setCursor(6, 3);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);

  //write H
  lcd.setCursor(10, 1);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  lcd.setCursor(10, 2);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(10, 3);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
}

void printLow() {
  //write L
  lcd.setCursor(0, 1);
  lcd.write((byte)0);
  lcd.write("  ");
  lcd.setCursor(0, 2);
  lcd.write((byte)0);
  lcd.write("  ");
  lcd.setCursor(0, 3);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  //Write O
  lcd.setCursor(4, 1);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.setCursor(4, 2);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  lcd.setCursor(4, 3);
  lcd.write((byte)0);
  lcd.write((byte)0);
  lcd.write((byte)0);
  //Write W
  lcd.setCursor(8, 1);
  lcd.write((byte)0);
  lcd.write(" ");
  lcd.write((byte)0);
  lcd.setCursor(8, 2);
  lcd.write((byte)0);
  lcd.write((byte)4);
  lcd.write((byte)0);
  lcd.setCursor(8, 3);
  lcd.write((byte)0);
  lcd.write((byte)3);
  lcd.write((byte)0);
};
