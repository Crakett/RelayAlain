/*
   1MB flash sizee

   esp8266-01 connections
   gpio  0 - button
   gpio  4 - relay
   gpio  3 - green led - active high

*/

// sur le ESP01
#define   GPIO0        0
#define   GPIO2        2
#define   GPIO3        3
#define   GPIO4        4


#define   ESP_BUTTON   GPIO0
#define   ESP_LED      GPIO3

#define   ESP_RELAY    GPIO4
//if this is false, led is used to signal startup state, then always on
//if it s true, it is used to signal startup state, then mirrors relay state
//S20 Smart Socket works better with it false
#define ESP_LED_RELAY_STATE      false

#define HOSTNAME "RelayAlain"

//comment out to completly disable respective technology
#define INCLUDE_BLYNK_SUPPORT


/********************************************
   Should not need to edit below this line *
 * *****************************************/
#include <ESP8266WiFi.h>

#ifdef INCLUDE_BLYNK_SUPPORT
#define BLYNK_PRINT Serial       // Comment this out to disable prints and save space
#include <BlynkSimpleEsp8266.h>

static bool BLYNK_ENABLED = true;
#endif


#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>

#define EEPROM_SALT 12669
typedef struct {
  char  bootState[4]      = "off";
  char  blynkToken[33]    = "";
  char  blynkServer[33]   = "blynk-cloud.com";
  char  blynkPort[6]      = "8442";
  int   salt              = EEPROM_SALT;
} WMSettings;

WMSettings settings;

#include <ArduinoOTA.h>


//for LED status
#include <Ticker.h>
Ticker ticker;


const int CMD_WAIT = 0;
const int CMD_BUTTON_CHANGE = 1;
const int CMD_BUTTON_VALID = 2;

int cmd = CMD_WAIT;
//int relayState = HIGH;

// button state
int buttonState = HIGH;
int currentState = HIGH;

static long startPress = 0;
static long timeAntiRebond = 0;

void tick()
{
  //toggle state
  int state = digitalRead(ESP_LED);  // get the current state of GPIO1 pin
  digitalWrite(ESP_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.8, tick);
}

#ifdef INCLUDE_BLYNK_SUPPORT

void updateBlynk(int state) {
  Blynk.virtualWrite(4, state == HIGH ? 255 : 0);
  Serial.print("updateBlynk="); Serial.println(state);
}

#endif

void setState(int state) {
  //relay
  digitalWrite(ESP_RELAY, state);

  //led
  if (ESP_LED_RELAY_STATE) {
    digitalWrite(ESP_LED, state); // led is active high
  }

#ifdef INCLUDE_BLYNK_SUPPORT
  updateBlynk(state);
#endif

}

void turnOn() {
  int relayState = HIGH;
  setState(relayState);
}

void turnOff() {
  int relayState = LOW;
  setState(relayState);
}

void toggleState() {
  if(cmd==CMD_WAIT)  cmd = CMD_BUTTON_CHANGE;
}

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void toggle() {
  Serial.println("toggle state");
  Serial.println(digitalRead(ESP_RELAY));
  int relayState = digitalRead(ESP_RELAY) == HIGH ? LOW : HIGH;
  setState(relayState);
}

void restart() {
  //TODO turn off relays before restarting
  turnOff();
  delay(2000);
  ESP.restart();
  delay(1000);
}

void reset() {
  //reset settings to defaults
  //TODO turn off relays before restarting
  turnOff();
  delay(2000);
  /*
    WMSettings defaults;
    settings = defaults;
    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  */
  //reset wifi credentials
  WiFi.disconnect();
  delay(1000);
  ESP.restart();
  delay(1000);
}

#ifdef INCLUDE_BLYNK_SUPPORT
/**********
 * VPIN % 5
 * 0 off
 * 1 on
 * 2 toggle
 * 3 value
 * 4 led
 ***********/

BLYNK_WRITE_DEFAULT() {
  int pin = request.pin;  // le chiffre derriere le V du bouton dans l'APP blynk
  int action = pin % 5;
  int a = param.asInt();  // à l'appui du bouton, la valeur de droite (high) et à la relache la valeur de gauche (low)

  Serial.print("request.pin="); Serial.print(pin); Serial.print("   param.asInt()="); Serial.println(a);
  
  if (a != 0) {
    switch(action) {
      case 0:
        turnOff();
        break;
      case 1:
        turnOn();
        break;
      case 2:
        toggle();
        break;
      default:
        //Serial.print("unknown action :");
        //Serial.print("pin="); Serial.print(pin);
        //Serial.print("  action="); Serial.println(action);
        break;
    }
  }
}

BLYNK_READ_DEFAULT() {
  // Generate random response
  int pin = request.pin;
  int action = pin % 5;
  Blynk.virtualWrite(pin, digitalRead(ESP_RELAY));

}


BLYNK_WRITE(25) {
  int a = param.asInt();
  Serial.print("BLYNK_WRITE(25)   param.asInt()="); Serial.println(a);
  switch(a) {
      case 0:
        turnOff();
        break;
      case 1:
        turnOn();
        break;
    }
}

//restart - button
BLYNK_WRITE(30) {
  int a = param.asInt();
  if (a != 0) {
    restart();
  }
}

//reset - button
BLYNK_WRITE(31) {
  int a = param.asInt();
  if (a != 0) {
    reset();
  }
}

#endif

void setup()
{
  Serial.begin(115200);

  //setup relay
  //TODO multiple relays
  digitalWrite(ESP_RELAY, LOW);
  pinMode(ESP_RELAY, OUTPUT);

  //set led pin as output
  pinMode(ESP_LED, OUTPUT);
  // start ticker with 0.1 because we start in AP mode and try to connect
  ticker.attach(0.1, tick);

  const char *hostname = HOSTNAME;

  WiFiManager wifiManager;
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //timeout - this will quit WiFiManager if it's not configured in 3 minutes, causing a restart
  wifiManager.setConfigPortalTimeout(180);

  //custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.salt != EEPROM_SALT) {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    WMSettings defaults;
    settings = defaults;
  }


  WiFiManagerParameter custom_boot_state("boot-state", "on/off on boot", settings.bootState, 33);
  wifiManager.addParameter(&custom_boot_state);


  Serial.println(settings.bootState);

#ifdef INCLUDE_BLYNK_SUPPORT
  Serial.println(settings.blynkToken);
  Serial.println(settings.blynkServer);
  Serial.println(settings.blynkPort);

  WiFiManagerParameter custom_blynk_text("<br/>Blynk config. <br/> No token to disable.<br/>");
  wifiManager.addParameter(&custom_blynk_text);

  WiFiManagerParameter custom_blynk_token("blynk-token", "blynk token", settings.blynkToken, 33);
  wifiManager.addParameter(&custom_blynk_token);

  WiFiManagerParameter custom_blynk_server("blynk-server", "blynk server", settings.blynkServer, 33);
  wifiManager.addParameter(&custom_blynk_server);

  WiFiManagerParameter custom_blynk_port("blynk-port", "port", settings.blynkPort, 6);
  wifiManager.addParameter(&custom_blynk_port);
#endif


  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(hostname)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  //Serial.println(custom_blynk_token.getValue());
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("Saving config");

    strcpy(settings.bootState, custom_boot_state.getValue());

#ifdef INCLUDE_BLYNK_SUPPORT
    strcpy(settings.blynkToken, custom_blynk_token.getValue());
    strcpy(settings.blynkServer, custom_blynk_server.getValue());
    strcpy(settings.blynkPort, custom_blynk_port.getValue());
#endif

    Serial.println(settings.bootState);
    Serial.println(settings.blynkToken);
    Serial.println(settings.blynkServer);
    Serial.println(settings.blynkPort);

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

#ifdef INCLUDE_BLYNK_SUPPORT
  //config blynk
  if (strlen(settings.blynkToken) == 0) {
    BLYNK_ENABLED = false;
  }
  if (BLYNK_ENABLED) {
    Blynk.config(settings.blynkToken, settings.blynkServer, atoi(settings.blynkPort));
  }
#endif


  //OTA
  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.begin();

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();

  //setup button
  pinMode(ESP_BUTTON, INPUT_PULLUP);
  attachInterrupt(ESP_BUTTON, toggleState, CHANGE);



   //TODO this should move to last state maybe
   //TODO multi channel support
  if (strcmp(settings.bootState, "on") == 0) {
    turnOn();
  } else {
    turnOff();
  }

  //setup led
  if (!ESP_LED_RELAY_STATE) {
    digitalWrite(ESP_LED, HIGH);
  }

  Serial.println("done setup");
}


void loop()
{

  //ota loop
  ArduinoOTA.handle();

#ifdef INCLUDE_BLYNK_SUPPORT
  //blynk connect and run loop
  if (BLYNK_ENABLED) {
    Blynk.run();
  }
#endif

  buttonChange();

}


void buttonChange()  {

  switch (cmd) {
    case CMD_WAIT:                // attent un appui
      break;
      
    case CMD_BUTTON_CHANGE:       // si appui ou relache du bouton
      cmd = CMD_BUTTON_VALID;
      timeAntiRebond = millis();
      break;

   case CMD_BUTTON_VALID:
      if(abs(millis() - timeAntiRebond) > 30) {
        currentState = digitalRead(ESP_BUTTON);
        Serial.print("BOUTON LECTURE="); Serial.println(currentState);
        if (currentState != buttonState) {                       // l'état à changer ?
          if (currentState == HIGH) {                            // on agit sur le relaché du bouton
            long duration = millis() - startPress;
            Serial.print("   DUREE="); Serial.println(duration);
            if (duration < 5000) {
              //Serial.println("short press - toggle relay");
              toggle();
            } else if (duration >= 5000 && duration < 20000) {
              Serial.println("medium press - reset");
              restart();
            } else if (duration > 20000) {
              Serial.println("long press - reset settings");
              reset();
            }
          } else {
            startPress = millis();   // on appui, on mémorise le temps
          }
          buttonState = currentState;
        }
        cmd = CMD_WAIT;
      }


    

      break;
  }


}




