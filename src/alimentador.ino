
#include <OneWire.h>
#include <blynk.h>
#include <DS18B20.h>
#include <RelayShield.h>
#include <Wiegand.h>

SYSTEM_THREAD(ENABLED);

// ====== DS18B20 One Wire Temp Sensor variables =========================== 
// DS18B20 constants
const int      MAXRETRY          = 4; // retry 4 times max
const uint32_t msSAMPLE_INTERVAL = 1000;
const uint32_t msMETRIC_PUBLISH  = 60000;


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
int pump_cutoff = 50000; //in miliseconds

// Wait 30 seconds to confirm that a calf didn't eat
int eating_timeout = 30000;
int pump_duration_ms;

uint32_t msLastPumpSample;
                            
// ================================Milk Heater Variables============================
// Define variables
const double SETPOINT = 37.0; // Target water temperature
const double OFFSET = -2.0; // Offset for fine tuning.

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
String PreviousCardID;

int cardID_read_since_ms;
uint32_t msLastCardSample;


//==============================Blynk/Pressure Switch===================================

char auth[] = "209e54a4687b40aba057ca7e89fcee41";

WidgetLED BlynkLEDon(V2); // LED for open door
WidgetLED BlynkLEDoff(V1); // LED for closed door

WidgetLED HeaterLED(V5); // LED for closed door

BlynkTimer timer;


// As soonas connected, sync with Blynk timeserver
BLYNK_CONNECTED() {
    Blynk.syncAll();
}

// =======================================================================================

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
    
    //Start off with all relays turned off:
    myRelays.allOff();
    
    BlynkLEDon.off();
    BlynkLEDoff.on();
    HeaterLED.off();
    
    //BlynkTimer timer;
    
}

void loop() {
    
    Blynk.run();
    
    // Request time synchronization from the Particle Cloud
    Particle.syncTime();
    checkApp();
    
    //get temperature from water
    if (millis() - msLastSample >= msSAMPLE_INTERVAL){
        getTemp();
       
        if (millis() - msLastMetric >= msMETRIC_PUBLISH) {
            publishData();
        }
    }

    // Water temperature BANG - BANG controller
    controlWaterTemp();
    
    
    // For debugging purposes just send serial straight through to make sure it is receiving data from UART    
    publishSerial();
    
    // Measure duration of events
    pump_duration_ms = millis() - msLastPumpSample;
    cardID_read_since_ms = millis() - msLastCardSample;
    
    if (digitalRead(vacuumSwitch)==LOW) {
             myRelays.on(2);
   			BlynkLEDoff.off();
   			BlynkLEDon.on();
    }
   else if(digitalRead(vacuumSwitch==HIGH)){
            myRelays.off(2);
            BlynkLEDon.off();
            BlynkLEDoff.on(); 
    }
    
    
    if (wg.available()){
        // Get Card ID and set msLastCardSample to millis (start counting milliseconds as soon as you read a card)
        getCardID();
        // if calf is allowed and starts sucking, allow pump to run
        if (cardID =="5980741") {
            //Particle.publish("card_id_read", cardID, 60, PRIVATE);
                pumpOn();
                delay(50000);
                Particle.publish("pump_on", cardID, 60, PRIVATE);
                }
                
                
            //}
        else {
                pumpOff();
                // if pump_duration_ms didn't reach the cutoff time, then send to Google Sheets that calf didn't finish eating
                if (pump_duration_ms != pump_cutoff) {
                    Particle.publish("ternero_no_termino_de_comer", cardID, 60, PRIVATE);  
                }
                // otherwise at the end of the pumping, this calf is considered fed.
                else {
                    Particle.publish("ternero_comio", cardID, 60, PRIVATE);
                    }
                }
            }
         // if card ID has been read, but no vacuum switch signal present, keep pump off 
        /*else if (!digitalRead(vacuumSwitch) && cardID =="5980741") {
            pumpOff();
            // inside this loop, only confirm calf didn't eat if timeout occurred or a new cardID has been detected
            if (cardID_read_since_ms > eating_timeout || PreviousCardID != cardID ) {
                // In this case, the CardID sent to Google Sheets is the PreviousCardID since we assume that was the card associated to the calf that didn't eat.
                Particle.publish("ternero_no_comio", PreviousCardID, 60, PRIVATE);
            }
        }
        */
    
	timer.setInterval(2000L, checkApp);
    
}
    


void checkApp()
{
	Blynk.syncVirtual(V0, V6);
}


// ====== DS18B20 One Wire functions =========================
void publishData(){
  sprintf(szInfo, "%4.2f C", celsius);
  // Particle.publish("Water Temp", szInfo, 60, PRIVATE);
  Blynk.virtualWrite(V4, szInfo);
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

//TODO: Heater App Button On / Off
BLYNK_WRITE(V6) {
  int pinData = param.asInt(); 

  if (pinData == 1)
  {
    manualHeater("heater_on");
  }

  else if (pinData == 0)
  {
    manualHeater("heater_off");
  }
}

BLYNK_WRITE(V0) 
 {
  int pinData = param.asInt(); 

  if (pinData == 1)
  {
   myRelays.on(2);
   BlynkLEDoff.off();
   BlynkLEDon.on();   
  }

  else if (pinData == 0)
  {
   myRelays.off(2);
   BlynkLEDon.off();
   BlynkLEDoff.on();   
  }
}

//BLYNK_WRITE(V3){
//static int pump_cutoff = 10000;
//  pump_cutoff = param.asInt() * 1000 ;
//}


void getCardID(){
    String cardID = String (wg.getCode());
    Serial.print(", DECIMAL = ");
	Serial.print(cardID);
	Particle.publish("cardID", cardID, 60, PRIVATE);
	if (cardID != NULL ) {
	    msLastCardSample = millis();
	    PreviousCardID = cardID;
	}
}
void controlWaterTemp(){
    if (celsius < SETPOINT + OFFSET)  {
        myRelays.on(1);
        if (myRelays.isOn(1) == TRUE){
            HeaterLED.on();
        }
        else if (myRelays.isOn(1) == FALSE) {
          HeaterLED.off();  
        }
    } 
    else {
        myRelays.off(1);
        
    }
}

// this function automagically gets called upon a matching POST request
int manualHeater(String command)
{
// look for the matching argument "unlock_front" <-- max of 64 characters long
	  if (command == "heater_on")
	  {
		myRelays.on(3);
        if (myRelays.isOn(3) == TRUE){
            HeaterLED.on();
	 
		return 1;
	  }
	  else return -1;
	 }
	 if (command == "heater_off")
	  {
		myRelays.off(3);
        if (myRelays.isOn(3) == FALSE){
            HeaterLED.off();
	 
		return 1;
	  }
	  else return -1;
	 }
	 
	 
}

// Pump Logic
void pumpOn(){
myRelays.on(2);
msLastPumpSample = millis();

}

void pumpOff(){
    myRelays.off(2);
}



void toggleSwitch() {
    if (myRelays.isOn(3) == TRUE) {
        myRelays.off(3);
        if (myRelays.isOn(3) == FALSE) {
             
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

