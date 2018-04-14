#include <Wiegand.h>
#include <RelayShield.h>
#include <DS18B20.h>
#include <blynk.h>
#include <pid.h>
#include <OneWire.h>


SYSTEM_THREAD(ENABLED);

// ====== DS18B20 One Wire Temp Sensor variables =========================== 
// DS18B20 constants
const int      MAXRETRY          = 4; // retry 4 times max
const uint32_t msSAMPLE_INTERVAL = 1000;
const uint32_t msMETRIC_PUBLISH  = 30000;


// Setup variable for DS1820
char     szInfo[64];
double   celsius; // Define temp units
double   fahrenheit; // Not really necessary for calf feeded application, but kept in code
uint32_t msLastMetric;
uint32_t msLastSample;

DS18B20  ds18b20(D0, true); //Sets Pin D2 for Water Temp Sensor and 
                            // this is the only sensor on bus
                            // ****IMPORTANT**********
                            //REQUIRES 4.7k resistance between data and 
                            
// ================================Milk Pump Duration==================================

// Pump turns 1.2 l/min, so to deliver 1L, turn pump on for 50 secs
int pump_duration = 50; //in seconds
                            
// ================================Milk Heater PID Variables============================
// Define variables
double Setpoint, Input, Output;

//Specify which are the Inputs, Outputs and the Target
PID tempcontrol(&Input, &Output, &Setpoint,1,0,0, PID::DIRECT);

/* Time proportioning Control:
 * Essentially a really slow version of PWM.
 * first we decide on a control window size (e.g. 5000mS)
 */
int ControlWindowSize = 5000; //in ms
unsigned long windowStartTime;
unsigned long previousMillis = 0;

/*================================Vacuum Switch============================
* Set vacuumSwitch to D2
*/
int vacuumSwitch = D7;            

//================================Relays ===================================
//Create an instance of the RelayShield library, so we have something to talk to
RelayShield myRelays;

//==============================RFID Reader===================================
// Create an instance of Wiegand protocol library
WIEGAND wg;
String cardID;

//==============================Blynk/Pressure Switch===================================

char auth[] = "209e54a4687b40aba057ca7e89fcee41";

WidgetLED BlynkLEDon(V2); // LED for open door
WidgetLED BlynkLEDoff(V1); // LED for closed door


void setup() {
    
    //Start Blynk app
    Blynk.begin(auth);
    
    // Start serial port to transmit via USB   
    USBSerial1.begin(115200);
    
    // Start UART serial port (Serial1) to transmit via RX/TX pins on Photon:
    Serial1.begin(9600, SERIAL_8N1); // SERIAL_8N1 is bit Parity 8N according to Indicador De Peso EL05B manual 
    
    // setup pressure switch to read when on/off
    pinMode(vacuumSwitch, INPUT_PULLDOWN);
    
    //Initialize Wiegand library 
    wg.begin(D1,D1,D2,D2);
    
    //Initialize Relay library by setting PINMODE on all 4 relays
    myRelays.begin();
    
    
    // Calibratable => Target Water Temperature 
    Setpoint = 38;
    
    // Input is the what the getTemp function provides in celsous
    Input = celsius;
    /*
    ===PID Controller Settings===
    */
    // set Output range from 0 to ControlWindowSize)
    tempcontrol.SetOutputLimits(0, ControlWindowSize);
    // set Output sampling time to every half a second (500 ms)
    tempcontrol.SetSampleTime(1000);
    //turn the PID on and set mode to automatic
    tempcontrol.SetMode(PID::AUTOMATIC);
    
    //Start off with all relays turned off:
    myRelays.allOff();
    
    BlynkLEDon.off();
    BlynkLEDoff.on();
}

void loop() {
    
    //initialize the variables for PID
    windowStartTime = milliseconds;
    
    Blynk.run();
    
    // Request time synchronization from the Particle Cloud
    Particle.syncTime();
    
    //get temperature from water
    if (millis() - msLastSample >= msSAMPLE_INTERVAL){
        getTemp();
    }

    Input = celsius; // define where the inoput gets its temperature
    tempcontrol.Compute();
    
    /************************************************
    * turn the heater on or off based on output of PID
    ************************************************/
    if(millis() - windowStartTime>ControlWindowSize)
    { //time to shift the Relay Window
    windowStartTime += ControlWindowSize;

    }
    if(Output < millis() - windowStartTime) {
        myRelays.off(1);
    } 
   else 
        myRelays.on(1);
        
    // For debugging purposes just send serial straight through to make sure it is receiving data from UART    
    publishSerial();
    
    if ( wg.available()){
        String cardID = String (wg.getCode());
        Serial.print(", DECIMAL = ");
		Serial.print(cardID);
		Particle.publish("cardID", cardID, 60, PRIVATE);
        
        if (digitalRead(vacuumSwitch) && cardID =="5980741") {
            
            if (milliseconds - pump_duration * 1000 < )
            pumpOn();
            delay(pump_duration * 1000); // multiply by 1000 to get miliseconds
            Particle.publish("ternero_comio", cardID, 60, PRIVATE);
            pumpOff();
        }
        else {
            pumpOff();
            Particle.publish("ternero_no_comio", cardID, 60, PRIVATE);
        }
    }
    
}

// ====== DS18B20 One Wire functions =========================
void publishData(){
  sprintf(szInfo, "%2.2C", celsius);
  Particle.publish("dsTmp", szInfo, PRIVATE);
  msLastMetric = millis();
}

void getTemp(){
  float _temp;
  int   i = 0;

  do {
    _temp = ds18b20.getTemperature(); // get water temperature
  } while (!ds18b20.crcCheck() && MAXRETRY > i++);

  if (i < MAXRETRY) {
    celsius = _temp;
    fahrenheit = ds18b20.convertToFahrenheit(_temp);
    Serial.println(celsius);
  }
  else {
    celsius = fahrenheit = NAN;
    Serial.println("Invalid reading");
  }
  msLastSample = millis();
}
// =========================================================
void publishSerial(){
    while(USBSerial1.available())
    {                       
        Serial1.write(USBSerial1.read());
    }
    while(Serial1.available())
    {
        USBSerial1.write(Serial1.read());
    }
}

BLYNK_WRITE(V0) {
  static int oldParam = 0;
  if (param.asInt() && !oldParam) { // On button press
    toggleSwitch();
  }
  
  oldParam = param.asInt();
}

//BLYNK_WRITE(V3){
//  int pump_duration = param.asInt();
//}

void pumpOn(){
    myRelays.on(2);
}

void pumpOff(){
    myRelays.off(2);
}

void pressureSwitchOn(){
    myRelays.on(3);
    
}

void pressureSwitchOff(){
    myRelays.off(3);
    BlynkLEDon.off();
    BlynkLEDoff.on();
}

void toggleSwitch() {
    if (myRelays.isOn(3) == TRUE) {
        myRelays.off(3);
        if (myRelays.isOn(3) == FALSE) {
            BlynkLEDon.off();
            BlynkLEDoff.on();    
        }
        
    } 
    else {
    myRelays.on(3);
        if (myRelays.isOn(3) == TRUE){
            BlynkLEDon.on();
            BlynkLEDoff.off();
    }
    
    }
 }
