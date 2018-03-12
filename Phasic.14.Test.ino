//CODE TEST

// include the library code:
#include <LiquidCrystal.h>
#include <SD.h>
#include <SPI.h>

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

const int numReadings = 20;
const int ptpTime = 150;
const int idleSwitch = 10; //10 = 1.6 seconds of showing per tip 1900 for 5 min
const int activeSwitch = 50; //to make active screen not fresh too fast
double lam = 0.5; //set according to data
const int stdDevReads = 20; //If using Std Dev also
File myFile;
File UseSum;
bool RawData = true; //decides what to show
double CurrentFlow = 0; //calc flow real quick
//NEED TO ALSO KNOW MAX/MIN FLOW POSSIBLE, anything higher than MAX
//should be ignored or called MAX.
double FlowEWMA[6] = {0, 5, 10, 15, 18, 20}; // thresholds for the 5 flowtypes is one on left until reaches right {off, vlow, low, med, high, vhigh} CALIBRATE[ ]
double EWMA = 389; //initialize to 'off' value                                                                                                             CAILBRATE[ ]
double MaxFlow = .00012; //should be in L/ms, set to MAX FLOW from sink                                                                                         CAILBRATE[ ]
double stdDEWMA = 6; //off value          CAILBRATE[ ]


bool useEWMA = false; //true for ewma , false if stdD                                                                                                       CAILBRATE[ ]

double thresh[2] = {500, 600}; //set to split code into one, two or three polynomials. 0-thresh[0], thresh[0]-thresh[1], thresh[1]-inf                     CAILBRATE[ ]

//ceAx^2+ceBx+ceC=flow 0-thresh[0]                                                                                                                         CAILBRATE[ ]
double ceA = 0;
double ceB = 0.005;
double ceC = -0.01;

//ceA1X^2+ceB1x+ceC1 = flow //for chunking at given threshold thresh[0]-thresh[1]                                                                          CAILBRATE[ ]
double ceA1 = 0;
double ceB1 = 0;
double ceC1 = 0;

//ceA1X^2+ceB1x+ceC1 = flow //for chunking at given threshold >thresh[1]                                                                                   CAILBRATE[ ]
double ceA2 = 0;
double ceB2 = 0;
double ceC2 = 0;

float stdD = 0; //all std dev only
float dev[stdDevReads];
int stdIndex = 0;

int ok = 1; // just for trimming of beignning data
int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
int total = 0;                  // the running total
int average = 200;              // the average
int count = 0;
int activeCounter = 0;
int idleCounter = 0;
double mxm = 0;
double mnm = 0;
double ptp = 0;
int inputPin = A0;
long now1 = 0;
long FlowTime[6];
int whichFlow = 0;
String lastFlow = "Off";
String flowNow = "Off";
String flow = "Off";
int flowCount = 0;
long OnTime = 0;
int JustOffAction = 0;

//PHASES
int PhaseNow = 0;
int PhaseDay[5] = {14, 21, 28, 35, 49}; //BL, PC, C, PR, A, M 50 days total
int whichAction = 0; //0: Big text 1: Waste not want not 2: Raining
int whichTip = 0;
bool tipSwitch = false;
long actionTip = 0;
int lastTip = 0;
long summaryDisplay = 0;

bool justOff = 0;
bool On = 0;
unsigned long startUse = 0;
unsigned long endUse = 0;
unsigned long thisElapsed = 0;
double thisEWMA = 0;
double thisStd = 0;
double thisEWMAAvg = 0;
double thisStdAvg = 0;
double thisAvgFlow = 0;
unsigned long thisCounter = 0;
float thisWater = 0; //remember what units you used here
float thisWaterSTD = 0; //basically can choose one or both of these estimations
//stuff for daily/weekly breakdown
unsigned long HourCheck = 0;
unsigned long DayCheck = 0;
int Yesterday = -1;
int Today = 0;
int thisHour = 0;
long checkTime = 0;
unsigned long todayOnTime = 0;
unsigned long todayTimeEWMA = 0;
unsigned long todayAvgEWMA = 0;
unsigned long DailyOnTime[70] = {0};
unsigned long DailyTimeEWMA[70] = {0};
unsigned long DailyStdDevTime[70] = {0};
double DailyWater[70] = {0};
int DailyUses[70] = {0};

byte drop[8] = { //create drop image for LCD char
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
  lcd.print(F("Phasic 11"));

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
    myFile.println("New Data:");
    myFile.close();
    delay(100);
  }
  else {
    lcd.clear();
    lcd.write("File failed to open");
    delay(30000);
  }

  lcd.clear(); //delete after calibration step
  lcd.write("Turn on faucet!");
  delay(500);
}


void loop() {

  // TAKE READING AND CALCULATE STANDARD DEVIATION AND RUNNING AVERAGE -=-=-=-=-=-=-=-=-=-=-=-
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

    ////if (flowNow != "Off") { //if the flow is not off then save it to SD
    //      //this is saving a full string to sd of the whole on usage.
    //      myFile = SD.open("Slow.txt", FILE_WRITE);
    //      if (myFile) {
    //        myFile.print(millis());
    //        myFile.print("\t");
    //        myFile.print(stdD);
    //        myFile.print("\t");
    //        myFile.println(stdD);
    //        myFile.close();
    //      }
    //    //}
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
            thisAvgFlow = (ceA * thisStdAvg * thisStdAvg + ceB * thisStdAvg + ceC);
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
        //Add values to the daily totals
        DailyOnTime[Today] += thisElapsed;
        DailyTimeEWMA[Today] += thisEWMAAvg * thisElapsed;
        DailyStdDevTime[Today] += thisStdAvg * thisElapsed;
        DailyWater[Today] += thisWater; //OR this Water, or some combo!!! <------------- DAILY H2O calc.
        DailyUses[Today]++;
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
          if (stdD <= thresh[0]) {
            CurrentFlow = (ceA * stdDEWMA * stdDEWMA + ceB * stdDEWMA + ceC);
          } else if (thisStdAvg > thresh[0] && thisStdAvg <= thresh[1]) {
            CurrentFlow = (ceA1 * stdDEWMA * stdDEWMA + ceB1 * stdDEWMA + ceC1);
          } else {
            CurrentFlow = (ceA2 * stdDEWMA * stdDEWMA + ceB2 * stdDEWMA + ceC2);
          }
        }

        if (CurrentFlow > (MaxFlow*1000)) { //make sure not less than 0 or more than possible
          CurrentFlow = (MaxFlow*1000);
        }
        if (CurrentFlow < 0) {
          CurrentFlow = 0;
        }
        if (!RawData) {
          //bar from off to max
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Current Flow:");
          lcd.setCursor(0, 2);
          lcd.print("OFF");
          lcd.setCursor(3, 1);
          
          for (int i = 3; i <= 16 * (CurrentFlow / (MaxFlow*1000)); i++) {
            lcd.setCursor(i, 1);
            lcd.write((byte)0);
            lcd.setCursor(i, 2);
            lcd.write((byte)0);
            lcd.setCursor(i, 3);
            lcd.write((byte)0);
          }
          lcd.setCursor(17, 2);
          lcd.print("MAX");
        } else {
          //Raw
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Current Flow:");
          lcd.setCursor(0, 2);
          lcd.print(stdDEWMA);
          lcd.setCursor(3, 1);
          lcd.print(flowNow);
        }
      }
    }

    //IDLE STATE ********************* IDLE STATE ********************* IDLE STATE ********************* IDLE STATE ********************* IDLE STATE *********************
    else {
      idleCounter++;              //next active, random is (min, max-1)
      //JUST OFF ACTION STATE +++ +++ +++ +++ +++ JUST OFF ACTION STATE +++ +++ +++ +++ +++  JUST OFF ACTION STATE +++ +++ +++ +++ +++  JUST OFF ACTION STATE +++ +++ +++ +++ +++
      if (justOff) {


        //Display the right after breakdown
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.write("You just used about ");
        lcd.setCursor(0, 1);
        lcd.print(thisWater);
        lcd.write( " liters.");

        if (idleCounter > 75) { //ten seconds
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
          whichTip = random(1, 53); //MAX+1 and Min should be range of tips for given state
          //convert random int into char array for being called filename for each tip
          whichTip = -1;
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
            lcd.write("liters have been");
            lcd.setCursor(0, 2);
            lcd.write("used at this sink.");
          }
          idleCounter = 0;
          //SET PHASE ____________________________ SET PHASE __________________________ SET PHASE ___________________ SET PHASE ___________________
          //in the set phase area I need to have each phase specify what range of tips to be used
          // which action and post action screens to be used.
        } //idle tip end
      }//closed here
    }
    //ok from here down
    OnTime = 0; //reset ontime
    for (int i = 0; i < 4 ; i ++) { //sums how much time the sink has been on for this run
      OnTime += FlowTime[i];
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
        now1 = millis(); //reset to a normalized 0 after the flow changed
        flowCount = 0;
      }
    }

    mxm = 0; //reset PTP
    mnm = 0;
    count = 0; //reset count
  } //end of the output step (count) ------------------------------------------------------------------------- END OF LOOOOOOOOP END OF LOOOOOOOOP **************
  count++; //add to count for PTP window
  if (millis() - checkTime > 1800000) { //jus to make sure the day and hour are checked every 3 mins
    checkTime = millis();
    thisHour = whatHour();
    Today = whatDay();
  }
} // should be loop

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

String classifyFlow(double EWMA) //outputs the Flow string type, also adds up how long ----------- CLASSIFY FLOW
{ //the flow is at a certain rate
  String flowRate = "Off";
  if (EWMA < FlowEWMA[1]) {
    whichFlow = 0; // move register
    flowRate = "Off"; //Name the flow rate
  }
  if (EWMA >= FlowEWMA[1] && EWMA <= FlowEWMA[2]) {
    FlowTime[1] += millis() - now1;
    whichFlow = 1;
    flowRate = "Very Low";
  }
  if (EWMA > FlowEWMA[2] && EWMA <= FlowEWMA[3]) {
    FlowTime[2] += millis() - now1;
    whichFlow = 2;
    flowRate = "Low";
  }
  if (EWMA > FlowEWMA[3] && EWMA <= FlowEWMA[4]) {
    FlowTime[3] += millis() - now1;
    whichFlow = 3;
    flowRate = "Med";
  }
  if (EWMA > FlowEWMA[4] && EWMA <= FlowEWMA[5]) {
    FlowTime[3] += millis() - now1;
    whichFlow = 4;
    flowRate = "High";
  }
  if (EWMA > FlowEWMA[5]) {
    FlowTime[4] += millis() - now1;
    whichFlow = 5;
    flowRate = "Very High";
  }
  return flowRate;
}

float standard_deviation(float data[], int n) //Standard Dev to be used when needed
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

int whatHour() { //reads a file called Day and outputs the 1-2 digit int ------------------ WHAT HOUR
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
    for (int i = 0; i <= 69; i++) { //DailyOnTime
      Array.print(DailyOnTime[i]);
      if (i < 69) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 69; i++) { //DailyTimeEWMA
      Array.print(DailyTimeEWMA[i]);
      if (i < 69) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 69; i++) { //DailyStdDevTime
      Array.print(DailyStdDevTime[i]);
      if (i < 69) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 69; i++) { //DailyWater
      Array.print(DailyWater[i]);
      if (i < 69) { //dont print on last one
        Array.print("\t");
      } else {
        Array.print("\n");
      }
    }
    for (int i = 0; i <= 69; i++) { //DailyUses
      Array.print(DailyUses[i]);
      if (i < 69) { //dont print on last one
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
