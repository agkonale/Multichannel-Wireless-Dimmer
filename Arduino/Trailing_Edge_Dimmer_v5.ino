#include "hw_timer.h"
#include "dimmer.h"

void setup() 
{
  Serial.begin(115200);
  
  pinMode(ZERO_CROSSING_INT_PIN,INPUT);
  
  for(int i=0; i<NUM_CHANNELS; i++)
  {
    pinMode(Drive_Pin[i],OUTPUT);
    digitalWrite(Drive_Pin[i],LOW);
  }
   
  connectToWiFi();
  
  WebSocketConnect();
  MDNSConnect();
  HTTPUpdateConnect();

  
  noInterrupts();
  
  timer_init();
  attachInterrupt(ZERO_CROSSING_INT_PIN,Zero_Crossing_Int,RISING); 
  
  interrupts();
}


void loop() 
{

  if(millis() - lastConnectivityCheckTime > 1000)
  {
    if(WiFi.status() != WL_CONNECTED) 
    {
      connectToWiFi();
      WebSocketConnect();
      MDNSConnect();
    }  
    lastConnectivityCheckTime = millis();
  }
  
  else 
  {
    webSocket.loop();
    //yield();
    
    //OTA
    if (millis() - lastTimeHost > 10) 
    {
      httpServer.handleClient();
      lastTimeHost = millis();
    }

    //Update Connected Clients
    currentChangeTime = millis();
    if(currentChangeTime - lastChangeTime> 300 && isSynced == 0) 
    {
      String websocketStatusMessage = "A" + String(Dimming_Lvl[0]) + ",B" + String(Dimming_Lvl[1]) + ",X" + String(State[0]) + ",Y" + String(State[1]);
      webSocket.broadcastTXT(websocketStatusMessage); // Tell all connected clients current state of the channels
      isSynced = 1;
    }
  }
}
