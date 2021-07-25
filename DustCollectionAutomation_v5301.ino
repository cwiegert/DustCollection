/*
 * This code is for the project at 
 * http://www.iliketomakestuff.com/how-to-automate-a-dust-collection-system-arduino
 * All of the components are list on the url above.
 * 
This script was created by Bob Clagett for I Like To Make Stuff
For more projects, check out iliketomakestuff.com

Includes Modified version of "Measuring AC Current Using ACS712"
http://henrysbench.capnfatz.com/henrys-bench/arduino-current-measurements/acs712-arduino-ac-current-tutorial/

Parts of this sketch were taken from the keypad and servo sample sketches that comes with the keypad and servo libraries.

Uses https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library

This new version of the code ortiginally written by Bob Clagett is modified to make it easier to implement in a new shop, and not have to re-write code
when moving the machines around.   The DustGatesDefinition.cfg  is to be stored on an SD card and manage the configuration of the top level parameters
have a section for dust gates and a final section for votage switches.   The parameter in each voltage switch maps to a main dust gate for that maching, 
and the code below rolls through the comma delimited bit map to set all the other gates open or closed based on the 0,1 flag

Because of this config file, there is no need to modify code if you are taking a machine off line, moving it around or changing gate configuration.  
Simply modify the layout in the gate definition, gate bitmap or voltage sensor definition.    Using a '#" at the beinning of the voltage sensor or gate 
will inactivate the unit, as the code below ignores commented lines.

When an Amerpage chnage is detected, the main gate index is used to look up the main gate in the structure.   Upon finding it, the gate bitmap is parsed 
and gates are opened and closed.  

Each voltage sensor is mapped to an analog port for monitoring.   That port is set in the voltage sensor config definition, and can be modified if you 
want to add/remove machines.
  

NEW VERSION - Cory Wiegert   06/20/2020
    instead of hard coding the dust gates and machines in the setup, will use an SD Card and configure the machines via delimited config line
    can add and configure the dust gates dynamically, instead of having to recompile and upload new code

              Cory D. Wiegert  07/19/2020   v5.0    Adding the wifi and Blynk libraries and code to be able to control individual gates and dust collector
                                                    with the mobile app configured on the phone.   moved most of the setup and reading of the config file
                                                    to functions instead of running it all in 1 routine in setup()
              Cory D. Wiegert 07/24/2020    v5.1    Adding Blynk setup as stand alone function for a new Mobile app to control gates and dust collector manually

              Cory D. Wiegert 07/27/2020    v5.11   Need to debug the checkForAmperageChange function to ensure it is using ampThreshold as a compare 

              Cory D. Wiegert 07/31/2020    v5.12    Implemented the check for the change threshold from the config file.   fixed the bug in BLYNK_WRITE(V1)
              Cory D. Wiegert 08/03/2020    v5.2    Added a tab menu to the Blynk app, to set the configuration of gate limits.   
                                                    Virtual buttons V18-V22 
              Cory D. Wiegert 08/11/2020    v5.3    Added encryption to the Blynk configuration string from the config file.   
                                                    Also added Encrypt tab on the Blynk app,  virtual Buttons V30 - V40
              Cory D. Wiegert 11/28/2020    v5.3.01   changed the delay on the open and close, testing the speed of the gate open and closing
                                                                                                    
****************************************************************************************************************************************/
 #define BLYNK_PRINT Serial
 #include <Adafruit_PWMServoDriver.h>
#include "SdFat.h"
#include <ESP8266_Lib.h>
#include <BlynkSimpleShieldEsp8266.h>
#include <AESLib.h>


 
   
    Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();                 // called this way, it uses the default address 0x40
   
    // you can also call it with a different address you want
    //Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x41);
    
 //   boolean     DEBUG = false;  
    boolean     DEBUG = true;  
    boolean     collectorIsOn = false;  
    boolean     toolON = false;
    int         DC_spindown;
    byte        initiation = true;
    uint8_t     servoCount;   
    int         NUMBER_OF_TOOLS = 0;
    int         NUMBER_OF_GATES = 0;
    int         dustCollectionRelayPin;     // the pin which will trigger the dust collector power, turns it on and off by high or low setting on the pin
    int         manualSwitchPin;            //for button activated gate, currently NOT implemented   
    float       mVperAmp;                   // use 100 for 20A Module and 66 for 30A Module
    long        debounce;                   // # of miliseconds to sample the current when reading.   may not need with the way we have implemented getVPP()
    int         blynkSelectedGate;          // used to store the selected gate in the Blynk menu item
    boolean     manualOveride = false;
    int         inspectionPin;
    int         highPos;                    // used as the open max position in the config screen of the Blynk app
    int         lowPos;                     // used as the closed max position in the config screen of the Blynk app
    boolean     bGo = false;
    char        cypherKey[17] = {'\0'};
    SdFat       sdCard;
    SdBaseFile  fConfig;
    char     auth[40] = {'\0'};
    char     ssid[32] = {'\0'};
    char     pass[32] = {'\0'};
    char     server[48] = {'\0'};
    char     port[16] = {'\0'};
    char     ESPSpeed[16] = {'\0'};
    char     BlynkConnection[16] = {'\0'};
    long     serverPort;
    long     ESP8266_BAUD;  


    
         
    // Hardware Serial on Mega, Leonardo, Micro...
    #define EspSerial Serial3
    ESP8266     wifi(&EspSerial);
    BlynkTimer  timer;
    WidgetTerminal terminal(V8);
    WidgetTerminal confTerm(V22);
    WidgetTerminal encryptTerm(V40);
 
 /***********************************************
   *   Structure for the voltage sensor definition.  each instance
   *   variable is read and filled from the config file
   ***********************************************/
    struct      voltageSensor {
              int       switchID = -1;
              char      tool[32] = {'\0'};
              int       voltSensor;
              float     voltBaseline = 0;
              int       mainGate;
              float     VCC;
              float     ampThreshold;
              float     voltDiff;
              int       isON = false;
        }  toolSwitch[9];    
    
  /***********************************************
   *   Structure for the blast gate definition.  each instance
   *   variable is read and filled from the config file
   ***********************************************/
      struct    gateDefinition {
          int    gateID = -1;
          int    pwmSlot;
          char   gateName[32] = {'\0'};        // name of gate
          int    openPos;                   // min servo open position
          int    closePos;                   // max servo closed position
          bool   openClose;                  // is the gate open or closed?
          char   gateConfig [48] = {'\0'};      // string containing instructions on which gates to open and close
        }   blastGate[13];

BLYNK_WRITE(V1)       // turn on and off dust collector button
{
    if (manualOveride == true)
      if (param.asInt() == 1)
          if (collectorIsOn == false)
              turnOnDustCollection();
      else
        if (collectorIsOn == true)
          turnOffDustCollection();  
}

BLYNK_WRITE(V2)     // Gate drop down, selecting which gate is opened or closed
{
    blynkSelectedGate = param.asInt() - 1;
    
}

BLYNK_WRITE(V5)   // button for opening a gate.   Will set highlight back to middle button after processing click
{
  if (manualOveride == true)
    if (param.asInt() == 1)
      {
      
          terminal.print ( "opening machine => ");
          terminal.println ( blastGate[blynkSelectedGate].gateName);
          openGate (blynkSelectedGate);
          Blynk.virtualWrite(V3, blastGate[blynkSelectedGate].openPos);
          Blynk.syncVirtual(V3);
          runGateMap (blynkSelectedGate);
          Blynk.virtualWrite(V5, 2 );     // set the control to hold, so we can use the open and close buttons again
      }
    else if (param.asInt() == 3)
      {
        terminal.print("   param ==> ");
        terminal.println (param.asInt());
        closeGate (blynkSelectedGate, false);
        Blynk.virtualWrite(V4, blastGate[blynkSelectedGate].closePos);
        Blynk.syncVirtual(V4);
        Blynk.virtualWrite(V5, 2);   // set the control to hold, so we can use the open and close buttons again      
      }
}

BLYNK_WRITE(V6)     // changes operation from automated monitoring | manual use of Blynk app
  {
    if (param.asInt() ==1)
      manualOveride = true;
    else
      manualOveride = false;
  }

BLYNK_WRITE(V7)   //   text on screen to close all the gates in the system.   Similar to a reinitialization
  {
    if (manualOveride == true)
      if (param.asInt() == 1)
        {
          turnOffDustCollection();
          closeAllGates(false);
        }
  }

BLYNK_WRITE (V9)      // text button on screen to clear the terminal widget
  {
    terminal.clear();
  }


BLYNK_WRITE(V12)    // button that reads the voltage sensors of selected outlet
{
    terminal.println(param.asInt());
    if (param.asInt() == 2)
      {
        for (int c= 0; c<= 20; c++)
          {
            Serial.println(getVPP(inspectionPin));
            terminal.println(getVPP(inspectionPin));
            terminal.flush();
          }
      Blynk.virtualWrite(V12, 1);
      //delay(5000);                 //want to be able to walk from band saw to computer
      }      
}

BLYNK_WRITE(V11)    //  menu drop down to choose which outlet to monitor
{
  inspectionPin = toolSwitch[param.asInt()-1].voltSensor;
}

BLYNK_WRITE (V15)   //  text button to reset the SD card.   Useful if making changes to the config file, doesn't require a restart of the Arduino script
  {
      if (param.asInt() == 1)
        {
          if (manualOveride == true)
            {
                char     delim[2] = {char(222), '\0'};         // delimeter for parsing the servo config
                char     delimCheck = char(222);
                char     gateCheck = char(174);
                char     sectDelim = '^';
                char     checker;
                int      gates;
                int      outlets;
                 
                int      SD_WRITE = 53;
                char     fileName[32] = "DustGatesDefinition 53.cfg";
          
          
                if (fConfig.open(fileName))
                  {
                    checker = fConfig.read();
                    while (checker != delimCheck)           // read all of the comments, stop once you find the first line for servo config
                      {
                        if (DEBUG == true)
                          Serial.print(checker);
                        checker = fConfig.read();
                      }
                    readConfig (sectDelim, delim);                    // reads the first global config line from the config file.  
                    
                    gates = readGateConfig (sectDelim, delim);        // read the gates section of the config file
                    
                    outlets = readSensorConfig (sectDelim, delim);    // read the voltage sensor section of the config file
                    
                    fConfig.close();
                    terminal.println("reset all gates and sernsors");
                  }       // end of the open file If
                else
                  Serial.println(F("couldn't open the file to re-read the SD"));
                         // end of the else right above open file
            }             // end of if manualOveride
      }                   // end of checking for button pushed
  }

BLYNK_WRITE(V16)    // menu text, turns on the debug tracing.    Don't need to set the config file, can turn off debugging by clicking button again
{
    if (param.asInt() == 1)
      {
        DEBUG = true;
        Serial.begin (500000);
        delay(500);
      }
    else
        DEBUG = false;
}

BLYNK_WRITE(V18)
{
    blynkSelectedGate = param.asInt() - 1;
    Blynk.virtualWrite(V20, blastGate[blynkSelectedGate].openPos);
    Blynk.virtualWrite(V21, blastGate[blynkSelectedGate].closePos);
    Blynk.syncVirtual(V20);
    Blynk.syncVirtual(V21);
}

BLYNK_WRITE(V19)
{
   if (manualOveride == true)
     {
       if (param.asInt() == 1)
          { 
              confTerm.print ( "Opening machine => ");
              confTerm.println ( blastGate[blynkSelectedGate].gateName);     
              confTerm.print ("    setting PWM ==> ");
              confTerm.print( blastGate[blynkSelectedGate].pwmSlot);
              confTerm.print (" position ==> ");
              confTerm.println(highPos);
              confTerm.flush();
              pwm.setPWM(blastGate[blynkSelectedGate].pwmSlot, 0, (highPos - 20));
              delay(20);
              for (int c= (highPos - 20); c <= highPos; c+=1)          
                  pwm.setPWM (blastGate[blynkSelectedGate].pwmSlot, 0, c);
   
              //Blynk.syncVirtual(V3);
              
              Blynk.virtualWrite(V19, 2 );     // set the control to hold, so we can use the open and close buttons again
          }
      else if (param.asInt() == 3)
          {
              confTerm.print ( "Closing machine => ");
              confTerm.println ( blastGate[blynkSelectedGate].gateName);     
              confTerm.print ("    setting PWM ==> ");
              confTerm.print( blastGate[blynkSelectedGate].pwmSlot);
              confTerm.print (" position ==> ");
              confTerm.println(lowPos);
              confTerm.flush();
              pwm.setPWM(blastGate[blynkSelectedGate].pwmSlot, 0, (lowPos - 20));
              delay(20);
              for (int c= (lowPos - 20); c <= lowPos; c+=1)          
                  pwm.setPWM (blastGate[blynkSelectedGate].pwmSlot, 0, c);
          }
      Blynk.virtualWrite(V19, 2);   // set the control to hold, so we can use the open and close buttons again      
   }  
}

BLYNK_WRITE (V20)
{
  highPos = param.asInt();
}

BLYNK_WRITE (V21)
{
  lowPos = param.asInt();
}

BLYNK_WRITE(V23)
{
  confTerm.clear();
}

BLYNK_WRITE(V30)
  {
    
  }
BLYNK_WRITE(V31)
  {
     char   delim[2] = {char(222), '\0'};
     char   space = char(223);
     char   encryptString[96] = {'\0'};

     if (param.asInt() == 1)
       {
          // figure out what the decrypt algorythm is going to be
       }
     else if (param.asInt() == 3)
       {
         encryptTerm.println(ssid);
         encryptTerm.flush();
         
          encryptString[0] = char(222);
          encryptString[1] = char(174);
          encryptString[2] = char(222);
          strcat(encryptString, ssid);
          encryptTerm.println(ssid);
          encryptTerm.flush();
          encryptTerm.println(encryptString);
          encryptTerm.flush();
          strcat(encryptString, delim);
          strcat(encryptString, pass);
          strcat(encryptString, delim);
          strcat(encryptString, server);
          strcat(encryptString, delim);
          strcat(encryptString, port);
          strcat(encryptString, delim);
          strcat(encryptString, ESPSpeed);
          strcat(encryptString, delim);
          strcat(encryptString, BlynkConnection);
          strcat(encryptString, delim);
          strcat(encryptString, auth);
          strcat(encryptString, delim);
          for (int x = 0; x<= sizeof(encryptString); x++)
            if (encryptString[x] == '\0')
              encryptString[x] = 'z';
          encryptTerm.println(encryptString);
          encryptTerm.flush();
          Serial.println(encryptString);
          Serial.println(sizeof(encryptString));
          aes128_enc_multiple(cypherKey,encryptString, 96);
          Serial.println(encryptString);
          aes128_dec_multiple(cypherKey,encryptString, 96);
          Serial.println(encryptString);
          
       }
     //  after work is finished, 
     Blynk.virtualWrite(V31, 2);
  }

BLYNK_WRITE (V32)
{
    if (param.asInt() == 1)
      {
        
      }
}

BLYNK_WRITE (V33)
{
   strcpy(ssid, param.asStr());
   encryptTerm.println(ssid);
   encryptTerm.flush();
}

BLYNK_WRITE(V34)
  {
    strcpy(pass, param.asStr());
    encryptTerm.println(pass);
    encryptTerm.flush();
  }
BLYNK_WRITE(V35)
  {
    strcpy(server, param.asStr());
    encryptTerm.println(server);
    encryptTerm.flush();
  }
BLYNK_WRITE(V36)
  {
    strcpy(port, param.asStr());
    encryptTerm.println(port);
    encryptTerm.flush();
  }

BLYNK_WRITE (V37)
  {
    strcpy(ESPSpeed, param.asStr());
    encryptTerm.println (ESPSpeed);
    encryptTerm.flush();
  }
BLYNK_WRITE(V38)
  {
     strcpy(BlynkConnection, param.asStr());
     encryptTerm.println(BlynkConnection);
     encryptTerm.flush();
  }

BLYNK_WRITE(V39)
  {
    strcpy( auth, param.asStr());
    encryptTerm.println(auth);
    encryptTerm.flush();
  }

BLYNK_WRITE (V41)
  {
     if ( param.asInt() == 1)
       {
         char     delim[2] = {char(222), '\0'};         // delimeter for parsing the servo config
         
         startWifiFromConfig ('^', delim, 0);         
       } // end of If Param == 1
  }

    /*************************************
     *          SETUP
     *    
     ******************************************************************/
    void setup()
      { 
        
        char     delim[2] = {char(222), '\0'};         // delimeter for parsing the servo config
        char     delimCheck = char(222);
        char     gateCheck = char(174);
        char     sectDelim = '^';
        char     checker;
        int      gates;
        int      outlets;
        int         SD_WRITE = 53;
       
        char     fileName[32] = "DustGatesDefinition 53.cfg";

        // You should get Auth Token in the Blynk App.
        // Go to the Project Settings (nut icon).
          //  char  auth[] = "-dv99jBjBpvacaTas2NNEEHs50c4aVzP";        

       if ( DEBUG == true)
          {
            Serial.begin(500000);
            delay(1000);
          }
        
        pinMode(SD_WRITE, INPUT);           // initiate the SD card reader 
                   
        if (!sdCard.begin())
          {
              if (DEBUG == true)
               {
                  writeDebug("couldn't open the SD card", 1);
                  delay(5000);
               }
          }
        else
          {
   /*******************************************
    *   read the comments section of the config file, then start the other sections
    *   through the functions.   The readConfig will read the global settings for all gates
    *   readGateConfig() reads the configuration for the gates 
    *   readSensorConfig() -- reads the configurations for the outlet sensors
    *   startWifiConfig() -- reads the wifi settings, connects to the wifi and starts the blynk server
    *   
    ********************************************************/
            if (fConfig.open(fileName))
              {
                checker = fConfig.read();
                while (checker != delimCheck)           // read all of the comments, stop once you find the first line for servo config
                  {
                    if (DEBUG == true)
                      Serial.print(checker);
                    checker = fConfig.read();
                  }
                readConfig (sectDelim, delim);                    // reads the first global config line from the config file.  
                
                gates = readGateConfig (sectDelim, delim);        // read the gates section of the config file
                
                outlets = readSensorConfig (sectDelim, delim);    // read the voltage sensor section of the config file
               
                startWifiFromConfig (sectDelim, delim, 1);           // read the wifi section of the config file, start the wifi, and connect to the blynk server

            } else
              {
                Serial.print(F("Could not open config file ==>  "));
                Serial.println(fileName);
              }
        fConfig.close();    //don't need config file open any longer, configuration all into memory
    
       if (DEBUG== true)
         {
          for (int index = 0; index < NUMBER_OF_GATES; index++)
             {
                writeDebug (blastGate[index].gateName, 1);
                writeDebug ("   " + String (blastGate[index].gateID) + "      Gate pwmSlot ==> " + String(blastGate[index].pwmSlot) , 0);
                writeDebug("    " + String (blastGate[index].openPos) + "    " + String (blastGate[index].closePos) + "     ", 1);
                writeDebug ("    "+ String (blastGate[index].openClose) + "     " + String(blastGate[index].gateConfig), 1);
                writeDebug (String (toolSwitch[index].tool) + "    " + String (toolSwitch[index].switchID) + "   ", 1);
                writeDebug ("    "+String (toolSwitch[index].voltSensor) + "    " + String(toolSwitch[index].voltBaseline) + "    ", 1); 
                writeDebug ("    "+String(toolSwitch[index].mainGate), 1);
                writeDebug ("tools ==> " + String(index) + "   VCC from file ==> " +String (toolSwitch[index].VCC), 1); 
             }
         }

       pwm.begin();
       pwm.setPWMFreq(60);  // Default is 1000mS
       pinMode ( dustCollectionRelayPin, OUTPUT);
    
  // initialize all the gates, dust collector and the baseline voltage for the switches   
                          
       resetVoltageSwitches(); 
       setBlynkControls();  
       turnOffDustCollection();
       closeAllGates (true);              
     }

   }
 /***************************************************   
  *    MAIN Loop
  ***************************************************/
    void loop()
      {
         int   gateIndex = 0;
         int   activeTool = 50;
         if (manualOveride == false)
            {
              for(int i=0;i<NUMBER_OF_TOOLS;i++)       //  loop through all the tools to see if current has started flowing
                {
                  if (toolSwitch[i].switchID != -1)     //  only look at uncommented tools.   the # as first character of cfg file will disable switch
                    {
                        if ( checkForVoltageChange(i) )
                          {
                            if (toolSwitch[i].isON == true)
                              {
                                if (DEBUG == true)
                                    writeDebug(String (toolSwitch[i].tool) + "  has been turned off, dust collector should go off", 1);
                                  
                                // the running tool is off, and we need to stop looking at other tools and turn off the dust collector */
                               
                                toolSwitch[i].isON = false;
                                toolON = false;
                                i = NUMBER_OF_TOOLS;
                                activeTool = 50;
                                break;
                              }                 // end of toolsSwitch[i].isON
                            activeTool = toolSwitch[i].switchID;
                            gateIndex = toolSwitch[i].mainGate; 
                            toolSwitch[i].isON = true;
                            toolON = true;
                            i = NUMBER_OF_TOOLS;
                            openGate(gateIndex);
                          } else                  // end of checkForVoltageChange
                              {
                          //  either there is no current which has started, or the machine is still running - need to test for which
                                if (toolSwitch[i].isON == true)
                                  {
                                    i=NUMBER_OF_TOOLS;
                                    if (DEBUG == true)
                                      {
                                        writeDebug(String (toolSwitch[i].tool) + "  is still running, dumping out of checking loop", 1);
                                        writeDebug("    The current active tool is ==>  " + String(activeTool), 1);
                                      }         
                                  }   // end of thet toolSwitch[i].isON
                              }   // end of else
                    }  // end of testing active tools.   This is the if statement above which test for active tool
                }                   // end of For loop where we are monitoring the tools
             if(activeTool != 50 )
                {
                  // if activeTool is not = 50, it means there is current flowing through the sensor.   read the gate config map, open and close the appropriate gates
 
                    if (collectorIsOn == false)
                      runGateMap (gateIndex);
  
                } else                          // else of if(activeTool != 50)
                    {                           // This means there was a current change, likely from machine ON to machine OFF
                      if(collectorIsOn == true && toolON == false)
                        {
                          delay(DC_spindown);
                          turnOffDustCollection(); 
                          activeTool = 50; 
                          resetVoltageSwitches();
                        }
                    }       // End of else section
          }                 // end of check for manual overide 
        Blynk.run();   
        timer.run(); 
       
    }   //   end of loop()

    
/***********************************************************************
  *    void   readConfig (SdBaseFile fconfig, char sectDelim, char* delim)
  *    
  *    function to read the initial setup line in the config file.   sets the 
  *    global variables as functioning in the switch statement.
  ************************************************************************/
  void readConfig (char sectDelim, char delim[])
    {
          char          servoString[160] = {'\0'};
          char          memFile[30] = {'\0'};
          char          *token;
          int           index;
          int           bytesRead;
          
   
          bytesRead = fConfig.fgets(servoString, sizeof(servoString)); 
             token = strtok(servoString, delim);   
    
          if (servoString[0] != '#')              //  checking the commented gages
            { 
              for (int index = 0; index < 9; index++)      
              {
                token = strtok(NULL, delim);
                switch (index)
                  {
                    case 0:
                      servoCount = atoi(token);
                      break;
                    case 1:
                      NUMBER_OF_TOOLS = atoi(token);
                      break;
                    case 2:
                      NUMBER_OF_GATES = atoi(token);
                      break;
                    case 3:
                      DC_spindown = atoi(token);
                      break;
                    case 4:
                      dustCollectionRelayPin = atoi(token);
                      break;
                    case 5:
                      manualSwitchPin = atoi(token);
                      break;
                    case 6:
                      mVperAmp = atof(token);
                      break;
                    case 7:
                      debounce = atoi (token);
                      break;
                    case 8:
                      DEBUG = atoi(token);            // last parameter of string, will set the DEBUG variable to send messages to serial monitor
                      break;
                  } //end of case statement
              }   // end of for loop parsing line
            } // end of test whether or not the tool was active/commented out
        if (DEBUG == true)
        {
            writeDebug ("servo count == > " + String(servoCount) + " | NUMBER_OF_TOOLS ==> " + String(NUMBER_OF_TOOLS), 1);
            writeDebug ("NUMBER_OF_GATES ==> " + String (NUMBER_OF_GATES) + " | DC_SpinDown == > " + String(DC_spindown), 1);
            writeDebug ("DC Relay Pin ==> " + String(dustCollectionRelayPin) + " | Manual Switch ==> " + String(manualSwitchPin), 1);
            writeDebug(" mVperAmp ==> " + String(mVperAmp) + " | debounce ==> " + String(debounce) + " | DEBUG ==> " + String(DEBUG), 1);
            // delay(500);
        }
       bytesRead = fConfig.fgets(servoString, sizeof(servoString));
    }
  
  /***********************************************************************
    *    int   readGateConfig (char sectDelim, char* delim)
    *    
    *    funciton to read the configuration of the gates, and remove it from the setup section
    *    many of the variables defined in the setup, can be moved to local variables by having
    *    locals, and settign the structure variables through this function
    ************************************************************************/
  
  int readGateConfig (char sectDelim, char delim[])
    {
        char          gateString[160] = {'\0'};
        char          *token;
        int           index;
        char          tempString[48] = {'\0'};
        int           counter;
        char          checker;
        int           bytesRead;
  
        bytesRead = fConfig.fgets(gateString, sizeof(gateString));
        while (gateString[0] == sectDelim)                          // test to see if this is a comments section
          {
            bytesRead = fConfig.fgets(gateString, sizeof(gateString));
        
          }
  //  Loop through the configuration and get the blast gate configurations
      counter = 0;
  
        while (gateString[0] != sectDelim)
          {                   
            if ( gateString[0] != '#')
              {
                token = strtok(gateString, delim);
                for (index = 0; index < 6; index++)      
                  {
                    token = strtok(NULL, delim);  
                    switch (index)
                      {
                        case 0:
                          blastGate[counter].gateID = atoi(token);
                          break;
                        case 1:
                          blastGate[counter].pwmSlot = atoi(token);
                          break;
                        case 2:
                          strcpy (blastGate[counter].gateName,token);
                          break;
                        case 3:
                          blastGate[counter].openPos = atoi(token);
                          break;
                        case 4:
                          blastGate[counter].closePos = atoi(token);
                          break;
                        case 5:
                            strcpy (tempString,token);
                            stripComma (tempString, blastGate[counter].gateConfig);
                            break; 
                      }  // end of case statement
                    }  // end of the parsing of the line  For loop
                memset (gateString, '\0', sizeof(gateString));
                counter++;
              }   // end of the active switch, test for the commenting of the blast gates
            bytesRead = fConfig.fgets(gateString, sizeof(gateString));      
          }   // end of the section for all the blast gates
  
        return counter;
    }
  
  
  /*****************************************************************************
   *   int readSensorConfig (char sectDelim, char* delim)
   *          used to read the voltage sensor section of the configuration file.   similar to GateConfig
   *          this function will read the next section of the config file and set the 
   *          outlet box structures with the appropriate instance variables. 
   *          
   *          fSensor - the open file pointer
   *          sectDelim -- the marker to known when you are reading comments and hit the end of the section 
   *          delim -- pointer to the variable holding the special character that acts as the token flag in the config line
   *          
   *          returns, number of gates found
   **********************************************************************************/
  int readSensorConfig (char sectDelim, char delim[])
    {
         char          gateString[160] = {'\0'};
        char          *token;
        int           index;
        char          tempString[48] = {'\0'};
        int           counter;
        char          checker;
        int           bytesRead;
              
      //  Loop through the configuration and get the electric switches configuration
        counter = 0;
        bytesRead = fConfig.fgets(gateString, sizeof(gateString));
        
        while (gateString[0] == sectDelim)
          bytesRead = fConfig.fgets(gateString, sizeof(gateString));
        
        while (gateString[0] != sectDelim)
          {  
            index = 0; 
            if ( gateString[0] != '#')
              {
                token = strtok(gateString, delim);
                for (index = 0; index < 7; index++)      // Tokenize the baseline config parameters
                  {
                    token = strtok(NULL, delim);
                    switch (index)
                      {
                        case 0:
                          toolSwitch[counter].switchID = atoi(token);
                          break;
                        case 1:
                          strcpy (toolSwitch[counter].tool,token);
                          break;
                        case 2:
                          toolSwitch[counter].voltSensor = atoi(token);
                          break;
                        case 3:
                          toolSwitch[counter].voltBaseline = atol(token);
                          break; 
                        case 4:
                          toolSwitch[counter].mainGate = atol(token);
                          break; 
                        case 5:
                          toolSwitch[counter].VCC = atof(token);
                          break;
                        case 6:
                          toolSwitch[counter].voltDiff = atof(token);
                          break;
                      }   // end of the case statement          
                  }  // end of parsing the line
              }  // end of testing for an active switch and not commented in config file
            memset (gateString, '\0', sizeof(gateString));
            bytesRead = fConfig.fgets(gateString, sizeof(gateString));
            counter++;
         }   // end of electronic switch section
  
       return counter;
    }
  
  /*****************************************************************************
   *   void startWifiFromConfig (char sectDelim, char delim[], int WifiStart)
   *          used to read the Blynk section of the configuration file.   
   *          The ssid, password and server connection are all defined in this section
   *          and will be used to understand how to connect to the Blynk server
   *          
   *          fSensor - the open file pointer
   *          sectDelim -- the marker to known when you are reading comments and hit the end of the section 
   *          delim -- pointer to the variable holding the special character that acts as the token flag in the config line
   *          
   *          
   **********************************************************************************/
  void startWifiFromConfig (char sectDelim, char delim[], int WifiStart)
     {
         char     gateString[160] = {'\0'};
         int      bytesRead;
         int      index;
         char     *token;
         SdBaseFile   fEncrypt;

        
         //  read the section for the Blynk wifi setup -- move this to a function as well, just need to make sure it works
        if (WifiStart)
            {
              bytesRead = fConfig.fgets(gateString, sizeof(gateString));      
              while (gateString[0] == sectDelim)                          // test to see if this is a comments section
                {
                  bytesRead = fConfig.fgets(gateString, sizeof(gateString));  
                }
              token = strtok(gateString, delim);
              strcpy(cypherKey, token);             // This will be the filename of the wifi config file.
              fConfig.close();
              memset (gateString, '\0', sizeof (gateString));
            }
          if(fEncrypt.open(cypherKey))
            {
                fEncrypt.read(gateString, 96);
                aes128_dec_multiple(cypherKey,gateString, 96);
                index = 0;            
                token = strtok(gateString, delim);
                for (index = 0; index < 7; index++)      // Tokenize the baseline config parameters
                  {
                    token = strtok(NULL, delim);
                    switch (index)
                      {
                        case 0:
                          strcpy(ssid, token);
                          break;
                        case 1:
                          strcpy (pass,token);
                          break;
                        case 2:
                          strcpy (server, token);
                          break;
                        case 3:
                          serverPort = atol(token);
                          break; 
                        case 4:
                          ESP8266_BAUD =  atol(token);
                          break; 
                        case 5:
                          strcpy (BlynkConnection, token);
                          break;
                        case 6:
                          strcpy (auth, token);
                          break;
                        
                      }   // end of the case statement          
                  }  // end of parsing the line
          
            /* ************************************************** 
         *   setting up  the Blynk functionality and connecting to the Blynk server.   There are variables I shoudl be adding to the setup file, to know 
         *   the baud rate, the address of the blynk server, the port # and other parameters necessary for Blynk to function
         ******************************************/
              if (DEBUG == true)
                {
                  writeDebug ("auth == > " + String (auth), 1);
                  writeDebug ("wifi coms speed ==> " + String(ESP8266_BAUD) + "  | Connection ==> " + String(BlynkConnection), 1);
                  writeDebug ("ssid ==> " + String(ssid) + "  |  pass ==> " + String(pass) + "  | server ==> " + String (server) + " | serverPort ==> " + String(serverPort), 1);
                  //delay(500);
                }
                  // Set ESP8266 baud rate
              EspSerial.begin(ESP8266_BAUD);
              if (WifiStart)
                {
                  if ( strcmp(BlynkConnection, "Local") == 0 )
                    {
                      if (DEBUG)
                        writeDebug("trying to connect to the local server",1);
                      Blynk.begin(auth, wifi, ssid, pass, server, serverPort);
                      
                    //  Blynk.begin("UemFhawjdXVMaFOaSpEtM91N9ay1SzAI", wifi, "Everest", "<pass>", "192.168.1.66", <port>);
                    }
                  else if (strcmp(BlynkConnection,"Blynk") == 0)
                    {
                      Blynk.begin(auth, wifi, ssid, pass, "blynk-cloud.com", 80);
                    }
                  
                }  // end of if Wifi start
            } // end of If OPen Ecnrypt
          else
            Serial.println(F("couldn't open the encryption file"));
        Blynk.virtualWrite(V33, ssid);
        Blynk.virtualWrite(V34, pass);
        Blynk.virtualWrite(V35, server);
        Blynk.virtualWrite(V36, serverPort);
        Blynk.virtualWrite(V37, ESP8266_BAUD);
        Blynk.virtualWrite(V38, BlynkConnection);
        Blynk.virtualWrite(V39, auth);
        Blynk.syncVirtual(V33);
        Blynk.syncVirtual(V34);
        Blynk.syncVirtual(V35);
        Blynk.syncVirtual(V36);
        Blynk.syncVirtual(V37);
        Blynk.syncVirtual(V38);

      }

 /**************************************************
  *    void  setBlynkControls()
  *     used to set the lookup dropdown and other controls on the 
  *     blynk app.  That is the manual controller for the gates and 
  *     manually control the dust collector
  ****************************************************/
  void setBlynkControls()
    {
         BlynkParamAllocated clearout(256);    // valiable to clear the existing values from the menu dropdown
         BlynkParamAllocated switches(256);
         BlynkParamAllocated items(256); // list length, in bytes
      
         Blynk.setProperty(V11, "labels", clearout);    // clear all existing values from menu 
         Blynk.setProperty(V2, "labels", clearout);     // clear all existing values from menu
         Blynk.setProperty(V18, "labels", clearout);      // clear all existing values from gate menue
         for (int i = 0; i < NUMBER_OF_TOOLS; i++)
            items.add (toolSwitch[i].tool);
         Blynk.setProperty (V11, "labels", items);      // populate menu with the name of each tool
         Blynk.virtualWrite (V11, 1);
          
         for (int i=0; i< NUMBER_OF_GATES; i++)
            switches.add (blastGate[i].gateName);
         Blynk.setProperty(V2, "labels", switches);   // populate the menu with the name of each outlet
         Blynk.virtualWrite (V2, 3);
         Blynk.setProperty(V18, "labels", switches);
         Blynk.syncVirtual(V2);                      
         if (manualOveride == false)
            Blynk.virtualWrite(V6, false);
         else
            Blynk.virtualWrite(V6,true);
         Blynk.virtualWrite(V3, 0);       
         Blynk.virtualWrite(V4, 0);
         Blynk.virtualWrite(V5, 2);
         Blynk.virtualWrite(V13, 0);
         Blynk.virtualWrite (V12, 1);
         Blynk.syncVirtual(V12);
         Blynk.virtualWrite(V16, 0);
         Blynk.syncVirtual(V16);
         Blynk.virtualWrite(V20,0);
         Blynk.virtualWrite(V21,0);
         Blynk.virtualWrite(V19, 2);
         Blynk.virtualWrite(V30, cypherKey);
         Blynk.syncVirtual(V30);
         Blynk.virtualWrite(V31, 2);
         Blynk.virtualWrite(V33, " ");
         Blynk.virtualWrite(V34, " ");
         Blynk.virtualWrite(V35, " ");
         Blynk.virtualWrite(V36, " ");
         Blynk.virtualWrite(V37, " ");
         Blynk.virtualWrite(V38, " "); 
         
       
        terminal.clear();
        confTerm.clear();
        encryptTerm.clear();
    }
 
 /**********************************************************
  *   void  runGateMap (num activeTool)
  *       sets all the gates open/close for the active tool
  **********************************************************/
  void  runGateMap (int gateIndex)
    {
 
        if (collectorIsOn == false)
          turnOnDustCollection();
        for(int s=0; s<NUMBER_OF_GATES; s++)
          {    
             if (DEBUG == true)
               {
                  writeDebug ("gate map ==> " + String(blastGate[gateIndex].gateConfig),1);
                  writeDebug ("gate # clycling through map ==> " + String(s), 1); 
                  //delay(500);
               }
            if(blastGate[gateIndex].gateConfig[s] == '1')
                openGate(s);                     // if there is a '1' in the s index of the array, open the gate
            else
               closeGate(s, false);                     //   if there is a '0' in the s index of the array, close the gate
          }                         // end of the for loop
    }
 
 /*********************************************************
  *    void  closeAllGates( boolean initialize)
  *       quick function to loop through all the gates and close them
  *******************************************************************/
    void  closeAllGates( boolean  initialize)
      {
        for (uint8_t w=0; w<NUMBER_OF_GATES; w++)
          closeGate(w, initialize);  
       
      }
 
 /*********************************************************
  *   boolean  checkForAmperageChange (int which)
  *       used to check the current sensors on the outlets.  
  *       If current flow is detected, function returns true
  *       if no current, function returns FALSE
  *       This version has removed getVPP, instead it is a single read
  *       of the analog pin
  *       -->set the new baseline.   If the tool is turned off, there should be a delta from the running
  *       current and cause the logic to see a current change and respond true --> there is a current change
  ***********************************************************************/
 boolean checkForVoltageChange(int which)
    {
        float Voltage = 0;
        float VRMS = 0;
        float AmpsRMS = 0;

        Voltage = getVPP (toolSwitch[which].voltSensor);
        VRMS = abs(Voltage - (toolSwitch[which].VCC * 0.5) + .000);              //  set it to 0.000 if you want 0 amps at 0 volts
        AmpsRMS = (VRMS *1000)/mVperAmp;                // Sensitivirty defined by the rojojax model (.xxx)   See config file
        if (DEBUG == true)
          {
              writeDebug ("*****************************\n",1);
              writeDebug ("tool name ==> " +  String(toolSwitch[which].tool), 1);
              writeDebug ("   raw reading from sensor ==> ", 0);
              Serial.println (Voltage);
              writeDebug ("   baseline voltage from init ==> " + String (toolSwitch[which].voltBaseline), 1);
              writeDebug("___________________", 1);
              terminal.print ( toolSwitch[which].tool);
              terminal.print ("  voltage ==> ");
              terminal.println (Voltage);
          }
 /*********************************************
  *   modify this piece to check for appropriate change off the last parameter
  *   of the outlet sensor config line.   Right now, the bandsaw is not drawing enoug
  *   current to keep the collector on -  need to debug this
  *   toolSwitch[counter].ampThreshold
   *************************************************/
       if(abs(Voltage - toolSwitch[which].voltBaseline) > toolSwitch[which].voltDiff)
          {
            if ( toolSwitch[which].isON == true)
              return false;
            else
              return true;
          } else
              {
                //if the tool is on, we have to tell the calling condition we have turned the tool off, so we should return there has been a voltage change
                if (toolSwitch[which].isON == true)
                  return true;
                else 
                  return false; 
              }
    }
    
/***************************************
 *    stripComma
 *        function to remove the commas from the comma delimited list.  
 ******************************************/
    void stripComma (char source[48], char destination[48])
    {
      int    temp = 0;
      int    holder = 0;
      while(source[temp] != '\0')
        if (source[temp] != ',')
          {
            destination[holder] = source[temp];
            holder++;
            temp++;
          }
         else
           temp++;
      for (temp = 0; temp < sizeof(source); temp++)
        source[temp] = '\0'; 
    }
    
 /****************************************************   
  *    turnOnDustCollection
  *       
  ********************************************/
    void turnOnDustCollection(){
      if (DEBUG == true)
        writeDebug ("turnOnDustCollection", 1);
      digitalWrite(dustCollectionRelayPin,1);
      collectorIsOn = true;
      Blynk.virtualWrite(V1, 1);
      if (manualOveride == true)
        {
          terminal.println( "turning dust collector on");
          terminal.flush();
        }
    }

/*************************************************
 * void turnOffDustCollection ()
 ************************************/
    void turnOffDustCollection(){
      if (DEBUG == true)
        {
          writeDebug("turnOffDustCollection", 1);
          
        }
        
      digitalWrite(dustCollectionRelayPin,0);
      collectorIsOn = false;
      Blynk.virtualWrite(V1, false);
      if (manualOveride == true)
        {
          terminal.println("turning dust collector off");
          terminal.flush();
        }
    }

/**********************************************
 * float getVPP (int sensor)
 *      used to be used to flatten the reading on the sesor
 *      no longer implemented in this code.   The function
 *      samples the analog pin a number of times, and returns
 *      the average value of the readings
 *********************************************************/
 float getVPP(int sensor)
  {
      float result;
      
      int readValue;             //value read from the sensor
      int maxValue = 0;          // store max value here
      int minValue = 1024;          // store min value here
      
       uint32_t start_time = millis();
       while((millis()-start_time) < 300) //sample for .3 Sec
       {
           readValue = analogRead(sensor);
           // see if you have a new maxValue
           if (readValue > maxValue) 
           {
               /*record the maximum sensor value*/
               maxValue = readValue;
           }
           if (readValue < minValue) 
           {
               /*record the maximum sensor value*/
               minValue = readValue;
           }
       }
       
       // Subtract min from max
       result = maxValue - minValue;
       return result;
  }
        
 /****************************************************
  * void closeGate ( uint8_t num)
  *     closes the gate passed in num.   The gate will speed to just 
  *     before the bump stop and slows down to close the last 
  *     little bit
  **********************************************************/
    void closeGate(uint8_t num, boolean initialize)
    {
      if (DEBUG == true)   
        {
          Serial.print (F("closeGate " ));
          Serial.println(blastGate[num].pwmSlot);
         // delay (50);
        }
        
      if (blastGate[num].openClose == HIGH || initialize == true)
        {
          if (manualOveride == true)
             {
                terminal.print("    closing gate "); 
                terminal.println(blastGate[num].pwmSlot);
                terminal.flush();
             }
          pwm.setPWM(blastGate[num].pwmSlot, 0, blastGate[num].closePos - 20);
          delay(100);   
  //        delay(20);    the above line is a test for a longer delay and quicker response on the gates
          for (int i=blastGate[num].closePos - 20; i <= blastGate[num].closePos; i +=1)
              pwm.setPWM (blastGate[num].pwmSlot, 0 ,i);
          blastGate[num].openClose = LOW;
        }
      
    }

/****************************************************
  * void openeGate ( uint8_t num)
  *     opens the gate passed in num.   The gate will speed to just 
  *     before the bump stop and slows down to open the last 
  *     little bit
  **********************************************************/
    
    void openGate(int num)
      {
        int c;        
        if (blastGate[num].openClose == LOW)
          {
            if (manualOveride == true)
               {
                  terminal.print("    opening gate "); 
                  terminal.println(blastGate[num].pwmSlot);
                  terminal.flush();
               }
            pwm.setPWM(blastGate[num].pwmSlot, 0, (blastGate[num].openPos - 20));
              delay(100);  
     //       delay(20);  the above line is a test for a longer delay and quicker response on the gates
            for (c= (blastGate[num].openPos - 20); c <= blastGate[num].openPos; c+=1)          
                pwm.setPWM (blastGate[num].pwmSlot, 0, c);
          }
          blastGate[num].openClose = HIGH;
   }

 /**********************
    My easy way to write to the Serial debugger screen
 **********************/
        
    void writeDebug (String toWrite, byte LF)
    {

      if (LF)
      {
        Serial.println(toWrite);
      }
      else
      {
        Serial.print(toWrite);
      }
    
    }

 /*********************************************************
  *   void   resetVoltageSwitches ()
  *       used to recalibrate the votage switches to a baseline 
  *       so we can understand what flatline amperage we are calculating
  *       a difference from
  *       
  *********************************************************/
void  resetVoltageSwitches()
  {
  
    float     reading;
    
    for(int i=0;i<NUMBER_OF_TOOLS;i++)       // record the baseline amperage for each voltage sensor
            toolSwitch[i].voltBaseline = getVPP(toolSwitch[i].voltSensor);
  }
 
