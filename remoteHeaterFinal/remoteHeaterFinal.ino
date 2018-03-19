/*
  remoteHeater
  
  Emeric Porte
  
  28/01/2015
  
  Merging remoteHeater (with communication NRF24L01 + remoteHeater core timer (time management)
  
  v1.0

  Green  Button = delay before heat
  Red Button = Time of heat
  
  Red + Green = Reset or change state of heat
  
  Send Temp every x seconds
  
  Receive via NRF24L01 command to ON OFF heat
  
  v1.1
  
  Temperature set regulation via NRF24L01.

 */
 

#include <Adafruit_NeoPixel.h>
#include "RF24.h"

// Numero de node :
const byte SENDER=0x01;


// PIN ATTINY84
#define LM35 A2
#define RELAY 9
#define CE 3
#define CS 7
#define R_BUTTON 1
#define G_BUTTON 0
#define PIN 2
#define NBPXL 4

byte pinRelay = 0;
byte SecheMax = 30;


// Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 
RF24 radio(CE,CS); //pin 6 et pin 10

uint8_t myAddress[] = { SENDER,'d','o','m','O' };
uint8_t toAddress[] = { 0xFF,'d','o','m','O' };

// Variable pour la tempo
long previousMillisRF = 0;
long intervalRF = 60000; //toute les 60 secondes

// Paquet RF
struct rfPacket {
  byte sender;
  byte type;
  byte sensor;
  byte dataB;
  float dataF;
}myPacket;

// Core timer //////////////////////

#include <Adafruit_NeoPixel.h>



#define menuOut 3000 // si par d'activité pendant 3 sec, on valide le choix

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NBPXL, PIN, NEO_GRB + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

byte heatTime = 0;
byte g_t[] = { 100, 35, 0};
byte heatWait = 0;
//byte g_w[] = { 100, 35, 0 };
byte setMode = 0; // 0 = no mode
                  // 1 = set heat time, 15min increment
                  // 2 = set delay before heat, 1h increment
byte stateMode = 0; // 0, on chauffe pas on attend pas la chauffe
                    // 1, on est en départ différé
                    // 2, on chauffe en mode timer
unsigned long previousMillis = 0, interval = 0, previousMillis2 = 0;

                  
void fourLeds(byte r, byte g, byte b);
void relayFeedback(byte relayState);

// Fin core timer ////////////////////

void setup() {

  strip.begin();
  // On met le relay off et led en VERT
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);
  fourLeds(0,10,0); // vert
  //strip.setPixelColor(0,0,255,0);
  strip.show();
  
  pinMode(R_BUTTON, INPUT);
  pinMode(G_BUTTON, INPUT);

  // Setup and configure rf radio
  radio.begin();                          // Start up the radio
  //radio.setAutoAck(1);                    // Ensure autoACK is enabled
  radio.setRetries(15,15);                // Max delay between retries & number of retries
  radio.setChannel(123);  
  radio.setPayloadSize(8);                // Payload de 8bytes seulement
  radio.setDataRate(RF24_250KBPS);        // Debit à 250kbps
  //radio.openWritingPipe(addresses[0]); // envoi à 0node
  radio.openReadingPipe(1,myAddress); // recois sur 1Node
  
  radio.startListening();                 // Start listening
  
  delay(100);
  relayFeedback(0); // Je dis a openhab que le relay est OFF
}

void loop(void){
 /////////// CORE TIMER ///////////////////
 
 

  //int incomingByte = 0;
  byte updateled = 0;
  byte division, modulo;
  byte couleur; //indice de la couleur dans le array de byte

  int redButton = digitalRead(R_BUTTON);
  int greenButton = digitalRead(G_BUTTON);
  
  // Check button state and choose menu
  if (redButton == HIGH) {
    if (setMode == 2) heatWait = 0; // On raz.
    else if ((setMode == 1) && (heatTime < 180)) heatTime += 15;
    else if (setMode == 0) { setMode = 1; fourLeds(255,0,0); delay(100); }
    updateled = 1;
  }
  if (greenButton == HIGH) {
    if ((setMode == 2) && (heatWait < 12)) heatWait++;
    else if (setMode == 1) heatTime = 0;
    else if (setMode == 0) { setMode = 2; fourLeds(0,0,255); delay(100); }    
    updateled = 1;
  }
  
  // If button pushed, display led            
  if (updateled == 1) {              
    if (setMode == 1) { //set les pixel pour le mode temps de chauffage
      division = heatTime / 15; // On trouve le nombre de quart d'heure
      modulo = division % 4; // On trouve le nombre de quart  d'heure quand on enleve toute les heures complete
      division = division / 4; // On trouve le nombre d'heure complete
      for (byte pixel=0;pixel<=3;pixel++) { // pour chaque pixels...
        if (pixel<modulo) couleur = division + 1; //on ajoute le nombre d'heure + 1 quart d'heure s'il y en a un
        else couleur = division; // sinon la couleur correspond à une/des heure complete
        if (couleur == 0) //si on a 0 heure et 0 quart d'heure pour ce pixel, on affiche rien
          strip.setPixelColor(pixel, 4, 0, 0); //////////////////////
        else if (couleur > 0) // sinon on affiche une couleur determiné dans le array de byte
          strip.setPixelColor(pixel, 100, g_t[couleur-1], 0);
      }
    }
    if (setMode == 2) { //set les pixels pour depart differe du chauffage
      modulo = heatWait % 4;
      division = heatWait / 4;
      for (byte pixel=0;pixel<=3;pixel++) { // pour chaque pixels...
          if (pixel<modulo) couleur = division + 1; //on ajoute le nombre d'heure + 1 heure sup si on depasse 4 
          else couleur = division;
          if (couleur == 0)
            strip.setPixelColor(pixel, 0, 0, 4); /////////////////////////
          else if (couleur > 0) // sinon on affiche une couleur determiné dans le array de byte
            strip.setPixelColor(pixel, g_t[couleur-1], 0, 100);
      }
    }
    previousMillis = millis(); // on capte millis lors du dernier event
     // Serial.println(2048 - freeRam()); // Ram utilisée
    updateled = 0;
    delay(300);
    // Both button pushed : reset or ON OFF heater
    if ((digitalRead(R_BUTTON) == HIGH) && (digitalRead(G_BUTTON) == HIGH)) {
      if (pinRelay == 1) { // Si le relais est ON, on le coupe
        digitalWrite(RELAY, LOW);
        relayFeedback(0);
        fourLeds(0,10,0);
      }
      else if ((pinRelay == 0)) { // Si le relais est OFF, et qu'on est hors menu, on met ON
        digitalWrite(RELAY, HIGH);
        relayFeedback(1);
        fourLeds(10,0,0);
      }
      
      heatWait = 0;
      heatTime = 0;
      redButton = LOW;
      greenButton = LOW;
      setMode = 0;
      stateMode = 0;
      delay(1000);
    }
  }
  
  // Timeout to exit current menu
  if (((unsigned long)(millis() - previousMillis) >= menuOut) && (setMode !=0)) {
    if (setMode == 2) { // on commence à compté depart diferré si on etait dans ce mode
      //interval = (unsigned long)heatWait * 60 * 1000; // interval différé : h * 60sec * 1000 millis INTERVAL EN MINUTES POUR TEST - ATTENTE
      interval = (unsigned long)heatWait * 60 * 60 * 1000; // interval différé : h * 60min * 60sec * 1000 millis
      stateMode = 1; // on dit qu'on est en départ différé
      heatTime = 15; // Par défaut on met 15min de chauffe, pour changer mettre la chauffe après.
      previousMillis2 = millis(); // on capte millis quand le depart differé est parti
      relayFeedback(0);
      fourLeds(10,0,10); // Un peu blanc pendant l'attente chauffe
    }
    else if ((setMode == 1) && (stateMode != 1)) { // on etait dans le mode timer, et on est pas en mode differé, on commence le chauffage.
      //interval = (unsigned long)heatTime * 1000; // INTERVAL EN SECONDE POUR TEST - CHAUFFE
      interval = (unsigned long)(heatTime * 60 * 1000);
      stateMode = 2; // on chauffe.
      previousMillis2 = millis(); // on capte millis quand le chauffage se met en route
      // Pin relais à activer
      relayFeedback(1);
      fourLeds(10,0,0); // moyen blanc pour chauffe tout de suite
    }
    else if ((setMode == 1) && (stateMode == 1)) { // On etait en mode timer et le differe est en cours, donc on met les led violette et on attend
      fourLeds(10,0,10);
    }
    setMode = 0;
  }

  // Delay before heat or time to heat
  if (((unsigned long)(millis() - previousMillis2) >= interval) && (stateMode != 0)) { // Une tempo se termine, qu'est ce qu'on fait ?
    if (stateMode == 2) { // on chauffe, donc...
      stateMode = 0; // on stop tout
       // Pin relais à désactiver.
       relayFeedback(0);
       fourLeds(0,10,0); // on eteind en meme temps que la chauffe // On met en vert quand la chauffe a fini
    }
    if (stateMode == 1) { // on est en départ différé, donc..
      interval = (unsigned long)heatTime * 1000;// On met le temps de chauffe EN SECONDE, POUR TEST - CHAUFFE
      //interval = (unsigned long)heatTime * 60 * 1000;// On met le temps de chauffe
      stateMode = 2; //on chauffe
      previousMillis2 = millis(); // on capte millis quand le chauffage se met en route
      // pin relais a activer
      relayFeedback(1);
      fourLeds(10,0,0); // trés blanc pendant chauffe après une attente
    }
  }

  delay(10);

 ///////// FIN CORE TIMER /////////////////

  // Boucle infini sans tempo
    if( radio.available()){
      //char message;                                       // Variable for the received timestamp
      while (radio.available()) {                                   // While there is data ready
        radio.read( &myPacket, sizeof(myPacket) );             // Get the payload
      }
      if (myPacket.sender == 0) {
        if (myPacket.type == 2) {
          if (myPacket.dataB == 1) // marche
          {
            digitalWrite(RELAY, HIGH);
            pinRelay = 1;
            fourLeds(10,0,0); 
          }
          if (myPacket.dataB == 0) // arret
          {
            digitalWrite(RELAY, LOW);
            pinRelay = 0;
            if (stateMode == 1) fourLeds(10,0,10); 
            else fourLeds(0,10,0); 
          }
        }
        if (myPacket.type == 5) {
          SecheMax = myPacket.dataB;
        }
      }
   }
   
   
   strip.show();

  // Tempo toute les 10 secondes
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisRF > intervalRF) {
    previousMillisRF = currentMillis;
    // On recupere et envoi la temperature
    // Conversion temp reelle
    float tempLM35 = (analogRead(LM35) * 0.48828125);
    if ((tempLM35 > (float)SecheMax+1) && (pinRelay == 1)) {
      digitalWrite(RELAY, LOW);
      strip.setPixelColor(0, 0, 10, 0);
      strip.show();
    }
    if ((tempLM35 < (float)SecheMax-1) && (pinRelay == 1)) {
      digitalWrite(RELAY, HIGH);
      strip.setPixelColor(0, 10, 0, 0);
      strip.show();
    }
    // On envoi en RF ... Comment ?
    sendMyPacket( 0x00, 1, 0, 0, tempLM35); 
  }
}


void fourLeds(byte r, byte g, byte b)
{
  for(int i=0;i<=3;i++)
    strip.setPixelColor(i, r, g, b);
  strip.show();
}

void relayFeedback(byte relayState) {
  pinRelay = relayState;
  if (relayState == 1) digitalWrite(RELAY, HIGH);
  else digitalWrite(RELAY, LOW);
  // On envoi en RF ... Comment ?
  sendMyPacket( 0x00, 4, 0, relayState, 0);
}

// Envoi de packet NRF24
int sendMyPacket(byte destination, byte type, byte sensor, byte dataB, float dataF)
{
  rfPacket myPacket;
  // On prepare...
  myPacket.sender = SENDER;
  myPacket.type = type;
  myPacket.sensor = sensor;
  myPacket.dataB = dataB;
  myPacket.dataF = dataF;
  // On set le writing pipe, le destinataire...
  //addresses[1][0] = destination;
  toAddress[0] = destination;
  radio.openWritingPipe(toAddress);
  // On envoi...
  radio.stopListening(); // First, stop listening so we can talk.
  //Serial.println(F("Now sending."));
  if (!radio.write( &myPacket, sizeof(myPacket) )){  Serial.println(F("Failed send myPacket.")); return 0; }
  radio.startListening();
  return 1;
}
