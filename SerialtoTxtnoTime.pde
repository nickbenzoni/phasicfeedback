/*
This program is simply used to read and save the serial write from
the arduino. Simply rename "filename" to Off, Low etc., and set the program to also include
any relevant information about this data poitn (though it will be saved in the first
row which can be annoying.
*/
import processing.serial.*;
Serial mySerial;
int i = 0;
String filename = "CMax2";
String program = "GranularRec.SD.StdDev";


PrintWriter output;
void setup() {
   mySerial = new Serial( this, Serial.list()[2], 9600 );
   output = createWriter( filename + ".txt" );
   output.println(program);
   
}

void draw() { 
    if (mySerial.available() > 0 ) {
         String value = mySerial.readStringUntil('\n');
         if ( value != null && value.substring(0) != "0" 
         && value.substring(0,value.length()-2) != null) { //make sure string isnt empty or 0
              output.println(value.substring(0,value.length()-2)); // trim \n off
         }
    } 
    
    
}

void keyPressed() {
    output.flush();  // Writes the remaining data to the file
    output.close();  // Finishes the file
    exit();  // Stops the program
}