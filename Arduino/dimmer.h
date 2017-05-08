/*
 Interrupt supported on all pins except GPIO16
 Interrupt Modes :RISING,FALLING,CHANGE 
 
 Pins:
 GPIO 5 : Driving the MOSFET/TRIAC Channel(0)
 GPIO 4 : Driving the MOSFET/TRIAC Channel(1)
 Further channels can be added as required..
 
 GPIO 12 :  Zero Crossing Interrupt 

 GPIO 0 GPIO 2 Boot Mode
 H      H       FLASH
 L      H       Program via UART(Rx,Tx)
*/ 

#define IS_DEBUG
#ifdef  IS_DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#endif

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Arduino.h>
#include <WebSocketsServer.h>
#include <Hash.h>

WebSocketsServer webSocket = WebSocketsServer(81);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

///////////////////////////////////////////////////////////////////DEVICE CONFIG////////////////////////////////////////////////////////////////////

#define NUM_CHANNELS 2
#define ZERO_CROSSING_INT_PIN 12
#define DELTA 4                //(t_zero_crossing - t_interrupt)/STEP_TIME
#define STEP_TIME 78          //for 128 lvls (in uS) (65 for 50 Hz)

//128 lvl brightness control 
int Dimming_Lvl[NUM_CHANNELS] = {0,0}; //(0-127) 

int Drive_Pin[NUM_CHANNELS] = {5,4};
int State[NUM_CHANNELS] = {0,0};

//Wifi Access Point Settings
String ssid = "-----------";
String password = "-----------";

volatile boolean isHandled[NUM_CHANNELS] = {0,0};
volatile int Lvl_Counter[NUM_CHANNELS] = {0,0};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned long lastConnectivityCheckTime = 0;
unsigned long lastTimeHost = 0;
unsigned long currentChangeTime = 0;
unsigned long lastChangeTime = 0;
boolean isSynced = 0;

int NumActiveChannels = 0;
volatile boolean zero_cross = 0;
volatile int NumHandled = 0;

//Wifi AP Settings
/* Set these to your desired credentials. */
const char *APssid = "Dimmer001";
const char *APpassword = "MagpieRobin";

const char* host = "Dimmer0";
/**
///////////////////////////////////////////////////////////////////////////// Update the state of a channel /////////////////////////////////////////////////////////////////////////////
**/
void Update_State(int ON_OFF,int Channel_Number)
{ 
  if(State[Channel_Number] == 0 && ON_OFF == 1)
  {
    NumActiveChannels++;
  }
  else if(State[Channel_Number] == 1 && ON_OFF == 0)
  {
    NumActiveChannels--;
  }
  State[Channel_Number] = ON_OFF;
}


// WebSocket Events
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        lastChangeTime = millis();
        isSynced = 0;
        DEBUG_PRINTLN(ip);
      }
      break;
      
    case WStype_TEXT:
      {
        String text = String((char *) &payload[0]);
        lastChangeTime = millis();
        isSynced = 0;
              
        if(text.startsWith("a"))
        {
          String aVal=(text.substring(text.indexOf("a")+1,text.length())); 
          int Lvl_0 = aVal.toInt();
          Dimming_Lvl[0] = Lvl_0;
          DEBUG_PRINTLN(Lvl_0);
        }
  
        if(text.startsWith("b"))
        {
          String bVal=(text.substring(text.indexOf("b")+1,text.length())); 
          int Lvl_1 = bVal.toInt();
          Dimming_Lvl[1] = Lvl_1;
          DEBUG_PRINTLN(Lvl_1);
        }

        if(text=="ON_0")
        {
          Update_State(1,0);
          DEBUG_PRINTLN("Channel 0 ON!!");         
        }
          
        if(text=="OFF_0")
        {
          Update_State(0,0);
          digitalWrite(Drive_Pin[0], LOW);
          DEBUG_PRINTLN("Channel 0 OFF!!");
        }
         
        if(text=="ON_1")
        {
          Update_State(1,1);
          DEBUG_PRINTLN("Channel 1 ON!!");        
        }
          
        if(text=="OFF_1")
        {
          Update_State(0,1);
          digitalWrite(Drive_Pin[1], LOW);
          DEBUG_PRINTLN("Channel 1 OFF!!");
        }    
      }
      //webSocket.sendTXT(num, payload, length);
      //webSocket.broadcastTXT(payload, length);
      break;

    case WStype_BIN:
      hexdump(payload, length);
      //webSocket.sendBIN(num, payload, length);
      break;
  }
}

  
// WebSocket Connection
void WebSocketConnect() 
{
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

// MDNS 
void MDNSConnect() 
{
  if (!MDNS.begin(host)) 
  {
   DEBUG_PRINTLN("Error setting up MDNS responder!");
    while (1) 
    {
      delay(1000);
    }
  }
  DEBUG_PRINTLN("mDNS responder started");
  MDNS.addService("ws", "tcp", 81);
  MDNS.addService("http", "tcp", 80);
}

// HTTP updater connection
void HTTPUpdateConnect() 
{
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  DEBUG_PRINTLN("HTTPUpdateServer ready! Open http://");
  DEBUG_PRINT(host);
  DEBUG_PRINTLN(".local/update in your browser\n");
}


/**
///////////////////////////////////////////////////////////////////////////////// Timer Interrupt ISR ////////////////////////////////////////////////////////////////////////////////////
**/
void dimTimerISR()
{
  if(zero_cross == 1)                     
  {
    for(int i = 0; i < NUM_CHANNELS; i++) 
    {
      if(State[i] == 1)
      {
        if(Lvl_Counter[i] > Dimming_Lvl[i] + DELTA)       
        { 
          digitalWrite(Drive_Pin[i], LOW);     
          Lvl_Counter[i] = 0;  
          isHandled[i] = 1; 
          
          NumHandled++;
          if(NumHandled == NumActiveChannels)
          {    
            zero_cross = 0;     
          }
        } 
        else if(isHandled[i] == 0)
        {
          Lvl_Counter[i]++;                     
        }          
     }
   }
  }
}

/**
///////////////////////////////////////////////////////////////////////////////// Zero Crossing ISR ////////////////////////////////////////////////////////////////////////////////////
**/
void Zero_Crossing_Int()
{
  if(NumActiveChannels > 0)
  {
    NumHandled = 0;
    
    for(int i=0; i<NUM_CHANNELS; i++)
    {
      isHandled[i] = 0; 
      if(State[i] == 1)
      {
        digitalWrite(Drive_Pin[i], HIGH);
      }
    }  
    zero_cross = 1; 
  }
}

/**
//////////////////////////////////////////////////////////////////////////////////// TIMER SETTINGS ///////////////////////////////////////////////////////////////////////////////////// 
**/
void ICACHE_FLASH_ATTR timer_init(void)
{
  //FRC1_SOURCE/NMI_SOURCE
  hw_timer_init(FRC1_SOURCE, 1);
  hw_timer_set_func(dimTimerISR);
  hw_timer_arm(STEP_TIME);
}

/**
////////////////////////////////////////////////////////////////////////Connect the module to a WiFi access point////////////////////////////////////////////////////////////////////////
**/
int connectToWiFi() 
{
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int i=0;
  while (WiFi.status() != WL_CONNECTED) 
  {
    if (i == 30) 
    {
      return -1;
    }
    delay(1000);
    DEBUG_PRINT(".");
    i++;
  } 
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Connected to ");
  DEBUG_PRINTLN(ssid);
  DEBUG_PRINTLN("IP address: ");
  DEBUG_PRINTLN(WiFi.localIP());
  
  return 0;
}
