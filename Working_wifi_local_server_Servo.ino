
/*************************************************************
  Download latest Blynk library here:
    https://github.com/blynkkk/blynk-library/releases/latest

  Blynk is a platform with iOS and Android apps to control
  Arduino, Raspberry Pi and the likes over the Internet.
  You can easily build graphic interfaces for all your
  projects by simply dragging and dropping widgets.

    Downloads, docs, tutorials: http://www.blynk.cc
    Sketch generator:           http://examples.blynk.cc
    Blynk community:            http://community.blynk.cc
    Follow us:                  http://www.fb.com/blynkapp
                                http://twitter.com/blynk_app

  Blynk library is licensed under MIT license
  This example code is in public domain.

 *************************************************************
  WARNING!
    It's very tricky to get it working. Please read this article:
    http://help.blynk.cc/hardware-and-libraries/arduino/esp8266-with-at-firmware

  This example shows how value can be pushed from Arduino to
  the Blynk App.

  NOTE:
  BlynkTimer provides SimpleTimer functionality:
    http://playground.arduino.cc/Code/SimpleTimer

  App project setup:
    Value Display widget attached to Virtual Pin V5
 *************************************************************/

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

#include <Ethernet.h>
#include <ESP8266_Lib.h>
#include <BlynkSimpleShieldEsp8266.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>



// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
//char auth[] = "pzDdblzPJsTRNwAfFgEgjinRubBIXr52y";   // server on NAS
//char auth[] = "Uis_3k-m6AGy7eqvNpxrDVvUTHxf_M6A";
//char auth[] = "CXV8vI49W-QfwuLTTa8iLqJlQ_ufatGw";  // blynk server
char  auth[] = "-dv99jBjBpvacaTas2NNEEHs50c4aVzP";
int   analogPin;
double Voltage = 0;
double VRMS = 0;
double AmpsRMS = 0;
int    highPos;
int    lowPos;
int    activeServo;

Adafruit_PWMServoDriver servo = Adafruit_PWMServoDriver();                 // called this way, it uses the default address 0x40   
WidgetTerminal terminal(V8);

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Everest";
char pass[] = "67NorseSk!";

// Hardware Serial on Mega, Leonardo, Micro...
#define EspSerial Serial3

// or Software Serial on Uno, Nano...
//#include <SoftwareSerial.h>
//SoftwareSerial EspSerial(15,14); // RX, TX

// Your ESP8266 baud rate:
#define ESP8266_BAUD 115200

ESP8266 wifi(&EspSerial);

BLYNK_WRITE(V6)
{

      
}

BLYNK_WRITE(V15)
{
  activeServo = param.asInt();
}

BLYNK_WRITE(V9)
{
   highPos = param.asInt();  
}

BLYNK_WRITE(V10)
{
   lowPos = param.asInt(); 
}

BLYNK_WRITE(v12)
{
   if (param.asInt() == 1)
     {
        servo.setPWM(activeServo, 0, highPos); 
     }
   else
     {
      servo.setPWM(activeServo, 0, lowPos);
     }
   Blynk.virtualWrite(V11, servo.getPWM(activeServo));

}



BLYNK_READ(V2)
{
  int reading;
/*    Voltage = (5.0/1023) * analogRead(analogPin);
    VRMS = Voltage - (5.0 * 0.5) + .015;              //  set it to 0.000 if you want 0 amps at 0 volts
  Serial.print  (" Value of VRMS  ==> ");
  Serial.println(VRMS);
  
    AmpsRMS = (VRMS *1000)/66;
*/
    Blynk.virtualWrite (V2, analogRead(analogPin));
    reading = analogRead(analogPin);
    terminal.print ("reading on pin ");
    terminal.print(analogPin);
    terminal.print(" ==>  ");
    terminal.println(reading);
    terminal.flush();
    

}

BLYNK_WRITE(V1)
{
   
      analogPin = param.asInt();
       
       
 //      Blynk.virtualWrite (V10, servo.read());
 //   Blynk.virtualWrite(V6, servo.read());
}


BlynkTimer timer;

// This function sends Arduino's up time every second to Virtual Pin (5).
// In the app, Widget's reading frequency should be set to PUSH. This means
// that you define how often to send data to Blynk App.
void myTimerEvent()
{
  // You can send any value at any time.
  // Please don't send more that 10 values per second.
  Blynk.virtualWrite(V5, millis() / 1000);
}

void setup()
{
  // Debug console
  Serial.begin(115200);

  // Set ESP8266 baud rate
  EspSerial.begin(ESP8266_BAUD);
  delay(10);

  //Blynk.begin(auth, wifi, ssid, pass);
  // You can also specify server:
  //Blynk.begin(auth, wifi, ssid, pass, "blynk-cloud.com", 80);
  //Blynk.begin(auth, wifi, ssid, pass, IPAddress(192,168,1,66), 9443);
  Blynk.begin(auth, wifi, ssid, pass, "192.168.1.66", 32802);


  // Setup a function to be called every second
  timer.setInterval(1000L, myTimerEvent);
}

void loop()
{
  Blynk.run();
  timer.run(); // Initiates BlynkTimer
}
