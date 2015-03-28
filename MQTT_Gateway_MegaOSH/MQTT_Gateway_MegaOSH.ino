/*
    MQTT gateway for OpenHAB

    November 14 2014    
    LM35 OK
    MQTT Gateway to OpenHAB OK
    Message from broker OK
    
    Jan 12 2015
    Tempo sans delay
    Analyse des message recu en parti
    Creation des fonctions sendTemp et initMQTT
    
    Jan 13 2015
    Implement rf24 lib
    envoi marche arret au node01l
    S
    
    Jan 18 2015
    Passage du nrf24 en softSPI, PB SPI sur le shield ethernet
    
    Jan 22 2015 deboggage reception message MQTT
    Creation de la trame RF myPacket
    
    Fev 10 2015
    Adaptation pour TeleInfoNode
    
    Fev 12 2015
    Reception des donnée teleinfo, compteur, prix d'hier, depuis 6h, etc
    
    Mar 24 2015
    Ajout ecriure des logs dans la carte SD en cas de plantage

    E.PORTE    
*/
 
// Bibliotheque ethernet shield + mqtt

#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <PubSubClient.h>

#include "DigitalIO.h"

//#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

// WATCHDOG
#include <avr/wdt.h>

const byte SENDER=0x00;


/* Conf RF24_config.h
      const uint8_t SOFT_SPI_MISO_PIN = 3; 
      const uint8_t SOFT_SPI_MOSI_PIN = 7; 
      const uint8_t SOFT_SPI_SCK_PIN = 2; 
*/


// Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins ce & cs 
RF24 radio(8,9);

//byte addresses[][6] = {"0Node","1Node","9Node"};
uint8_t myAddress[] = { SENDER,'d','o','m','O' };
uint8_t toAddress[] = { 0xFF,'d','o','m','O' };


// Déclaration variables
// pin du lm35
byte temp_pin = 0;
// Update these with values suitable for your network.
byte mac[]    = {  0x90, 0xA2, 0xDA, 0x0D, 0x01, 0xBF };
// Home network
byte server[] = { 192, 168, 1, 253 };
byte ip[]     = { 192, 168, 1, 6 };
// Varian Laptop
//byte server[] = { 172, 20, 20, 226 };
//byte ip[]     = { 172, 20, 20, 100 };

// déclaration Fontions
// reception des message
void callback(char* topic, byte* payload, unsigned int length);
// Memoire dispo 
int freeRam ();
// Initialisation de la connexion au broker et subscribe a node/
void initMQTT(void);
// Envoi la temperature du node au broker
void sendTemp(void);

// Reseau et MQTT
EthernetClient ethClient;
PubSubClient MQTT_client(server, 1883, callback, ethClient);

File myFile;

// Variable pour la tempo
long previousMillis = 0;
long interval = 60000; //toute les minutes

long debug=0;

// Paquet RF
struct rfPacket {
  byte sender; // numero du node
  byte type; // type de capteur 0=temp, 1=swit, 2=ping, ...
  byte sensor; // numero du capteur
  byte dataB;
  float dataF;
};

int ramfree = 0;

byte noSD = 1;


// the setup routine runs once when you press reset:
void setup() {

  Serial.begin(57600);
  // WATCHDOG
  wdt_disable();
  Serial.println("Desactivation Watchdog.");
  
  // init rf24
  printf_begin();
  printf("\n\rDemarrage RF24 sur MQTT Gateway\n\r");
  // Setup and configure rf radio
  //digitalWrite(10, HIGH);
  radio.begin();                          // Start up the radio
  // CONFIG du Module NRF24, a mettre sur tout les modules :
  radio.setAutoAck(1);                    // Ensure autoACK is enabled
  radio.setRetries(15,15);                // Max delay between retries & number of retries  
  //radio.setPALevel(RF24_PA_MAX);          // Puissance max
  radio.setChannel(123);                  // Canal numero 123
  //radio.setPayloadSize(8);                // Payload de 8bytes seulement
  //radio.setDataRate(RF24_250KBPS);        // Debit à 250kbps
  // Fin config
  radio.openReadingPipe(1,myAddress);
  radio.startListening();                 // Start listening
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging

  Serial.print("Initializing SD card...");
  pinMode(53, OUTPUT);

  if (!SD.begin(4)) {
    Serial.println("initialization failed!");
    //return;
  }
  else {
    Serial.println("initialization done.");
    noSD = 0;
  }

  // RAM dispo
  Serial.print("Ram dispo = ");
  Serial.println(freeRam());
  
  Ethernet.begin(mac, ip);
  Serial.print("5...");
  delay(500); // 3 seconde le temps de lancer le terminal serie
  Serial.print("4...");
  delay(500); // 3 seconde le temps de lancer le terminal serie
  Serial.print("3...");
  delay(500); // 3 seconde le temps de lancer le terminal serie
  Serial.print("2...");
  delay(500); // 3 seconde le temps de lancer le terminal serie
  Serial.println("1...");
  delay(500); // 3 seconde le temps de lancer le terminal serie
  

}

// the loop routine runs over and over again forever:
void loop() {
  char buff_message[20];
  String buff_S;
  rfPacket myPacket;
  //====== Ici boucle infini sans tempo
  // On se connecte si on ne l'est pas....
  initMQTT();
  // WATCHDOG
  wdt_reset();
  // ...
  // ...
  // ...
  // On check l'arrive de message RF
  
  if( radio.available()){
    while (radio.available()) {                                   // While there is data ready
      radio.read( &myPacket, sizeof(myPacket) );             // Get the payload
    }
    // DEBUG
    Serial.print("Sender:");
    Serial.print(myPacket.sender);
    Serial.print(" Type:");
    Serial.print(myPacket.type);
    Serial.print(" Sensor:");
    Serial.print(myPacket.sensor);
    Serial.print(" dataB:");
    Serial.print(myPacket.dataB);
    Serial.print(" dataF:");
    Serial.print(myPacket.dataF);
    Serial.print(" dataF(ulong):");
    Serial.println((unsigned long)myPacket.dataF);
    
    if (noSD != 1) {
      myFile = SD.open("mqlog.txt", FILE_WRITE);
      if (myFile) {
        myFile.print("Sender:");
        myFile.print(myPacket.sender);
        myFile.print(" Type:");
        myFile.print(myPacket.type);
        myFile.print(" Sensor:");
        myFile.print(myPacket.sensor);
        myFile.print(" dataB:");
        myFile.print(myPacket.dataB);
        myFile.print(" dataF:");
        myFile.print(myPacket.dataF);
        myFile.print(" dataF(ulong):");
        myFile.println((unsigned long)myPacket.dataF);
        myFile.close();
      }
    }
    
    //=============================================================
    if (myPacket.sender == 1) { // si on recoit du node 01 (heatControl)
      if (myPacket.type == 1) { // Temperature
        // On envoi le message MQTT
          // ATTENTION 6 caracteres seulement
        if(MQTT_client.connected()) {
          //dtostrf(myPacket.dataF, 4, 1, buff_message); // float to string
          buff_S = String((float)myPacket.dataF);
          buff_S.toCharArray(buff_message,20); 
          MQTT_client.publish("node/01/temp/00", buff_message);
        }
      }
      if (myPacket.type == 4) { // update de switch
        // On envoi le message MQTT
        if(MQTT_client.connected()) {
          if (myPacket.dataB == 1) MQTT_client.publish("node/01/swst/00", "ON");
          if (myPacket.dataB == 0) MQTT_client.publish("node/01/swst/00", "OFF");  
        }
      }
    }
    //=============================================================
    if (myPacket.sender == 9) { // Ici on a a faire a PingMachineNRF24 pour diagnostique RF
      
      Serial.println("Ping recu de node 09");
      sendMyPacket( 0x09, 3, 0, 0, myPacket.dataF);
      
      //radio.openWritingPipe(addresses[2]);
      //radio.stopListening();       // First, stop listening so we can talk   
      //if (!radio.write( &myPacket, sizeof(myPacket) )){  printf("failed.\n\r");  }
      //else printf("ACK OK.\n\r");
      //radio.startListening();                                       // Now, resume listening so we catch the next packets.     
      //radio.openWritingPipe(addresses[1]);
    }
    //=============================================================
    if (myPacket.sender == 2) { // Ici on a a faire a TeleInfoNode
      if (myPacket.type == 6) {
        //Serial.print("TeleInfoNode Puissance :");
        //Serial.print((int)myPacket.dataF);
        //Serial.println("Watts");
        // On envoi le message MQTT
        if(MQTT_client.connected()) {
          char buff_message[20] ; // ATTENTION 6 caracteres seulement
          itoa((int)myPacket.dataF, buff_message, 10);
          MQTT_client.publish("node/02/pins/00", buff_message);
        }
        else Serial.println("MQTT Deconnected.");
      }
      // Modif edf
      if (myPacket.type == 7) {
        // compteurs
        if (myPacket.sensor == 1) {
          if(MQTT_client.connected()) {
            buff_S = String((unsigned long)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/02/bbrh/01", buff_message);
          }
        }
        if (myPacket.sensor == 2) {
          if(MQTT_client.connected()) {
            buff_S = String((unsigned long)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/02/bbrh/02", buff_message);
          }
        }
        if (myPacket.sensor == 3) {
          if(MQTT_client.connected()) {
            buff_S = String((unsigned long)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/02/bbrh/03", buff_message);
          }
        }
        if (myPacket.sensor == 4) {
          if(MQTT_client.connected()) {
            buff_S = String((unsigned long)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/02/bbrh/04", buff_message);
          }
        }
        if (myPacket.sensor == 5) {
          if(MQTT_client.connected()) {
            buff_S = String((unsigned long)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/02/bbrh/05", buff_message);
          }
        }
        if (myPacket.sensor == 6) {
          if(MQTT_client.connected()) {
            buff_S = String((unsigned long)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/02/bbrh/06", buff_message);
          }
        }
        if (myPacket.sensor == 7) {
            // ATTENTION 6 caracteres seulement
          if(MQTT_client.connected()) {
            itoa((unsigned long)myPacket.dataF, buff_message, 10); // int to string last24HkWh
            MQTT_client.publish("node/02/bbrh/07", buff_message);
          }
        }
        if (myPacket.sensor == 8) {
            // ATTENTION 6 caracteres seulement
          if(MQTT_client.connected()) {
            //dtostrf(myPacket.dataF, 4, 2, buff_message); // float to string, last24HkWh
            buff_S = String((float)myPacket.dataF);
            buff_S.toCharArray(buff_message,20); 
            MQTT_client.publish("node/02/bbrh/08", buff_message);
          }
        }
        if (myPacket.sensor == 9) {
            // ATTENTION 6 caracteres seulement
          if(MQTT_client.connected()) {
            itoa((unsigned long)myPacket.dataF, buff_message, 10); // int to string last24HkWh
            MQTT_client.publish("node/02/bbrh/09", buff_message);
          }
        }
        if (myPacket.sensor == 10) {
            // ATTENTION 6 caracteres seulement
          if(MQTT_client.connected()) {
            //dtostrf(myPacket.dataF, 4, 2, buff_message); // float to string, last24HkWh
            buff_S = String((float)myPacket.dataF);
            buff_S.toCharArray(buff_message,20); 
            MQTT_client.publish("node/02/bbrh/10", buff_message);
          }
        
        }
      }
      
      if (myPacket.type == 8) {
        if (myPacket.sensor == 0) { // tarif du jour
          switch (myPacket.dataB) {
            case 1:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/00", "BLEU-CREUSES");
              break;
            case 2:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/00", "BLANC-CREUSES");
              break;
            case 3:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/00", "ROUGE-CREUSES");
              break;
            case 4:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/00", "BLEU-PLEINES");
              break;
            case 5:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/00", "BLANC-PLEINES");
              break;
            case 6:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/00", "ROUGE-PLEINES");              
              break;
            //default:
          }
        }
        if (myPacket.sensor == 1) { // tarif de demain
          switch (myPacket.dataB) {
            case 1:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/01", "BLEU");
              break;
            case 2:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/01", "BLANC");
              break;
            case 3:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/01", "ROUGE");
              break;
            case 0:
              if(MQTT_client.connected()) MQTT_client.publish("node/02/ptec/01", "----");
              break;
            //default:
          }    
        }
      }
    }
    //=============================================================
    if (myPacket.sender == 3) { // Station météo
      if (myPacket.type == 1) {
        if (myPacket.sensor == 0) { // Temp externe
          if(MQTT_client.connected()) {
            buff_S = String((float)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/03/temp/00", buff_message);
          }
        }
        if (myPacket.sensor == 1) {// Temp interne
        if(MQTT_client.connected()) {
            buff_S = String((float)myPacket.dataF);
            buff_S.toCharArray(buff_message,20);
            MQTT_client.publish("node/03/temp/01", buff_message);
          }
        }
      }
      if (myPacket.type == 9) {
        // Humidité
        if(MQTT_client.connected()) {
          buff_S = String((float)myPacket.dataF);
          buff_S.toCharArray(buff_message,20);
          MQTT_client.publish("node/03/humi/00", buff_message);
        }
      }
      if (myPacket.type == 10) {
        // Lumens
        if(MQTT_client.connected()) {
          buff_S = String((unsigned int)myPacket.dataF);
          buff_S.toCharArray(buff_message,20);
          
          //itoa((unsigned long)myPacket.dataF, buff_message, 10);
          MQTT_client.publish("node/03/lumi/00", buff_message);
        }
      }
      if (myPacket.type == 11) {
        // Pression
        if(MQTT_client.connected()) {
          buff_S = String((float)myPacket.dataF);
          buff_S.toCharArray(buff_message,20); 
          MQTT_client.publish("node/03/pres/00", buff_message);
        }
      }    
    }
    //=============================================================
    //=============================================================    
  }
  
  
  
  // receptionne les messages MQTT et maintiens la connection au serveur
  MQTT_client.loop();

  //======= Ici routine periodique = 10 secondes
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    // Code...
    sendTemp();
    debug++;
  
    ramfree = freeRam();
    Serial.print(ramfree);
    Serial.print(" octets dispo. Boucle no. ");
    Serial.println(debug);
    
    if (noSD != 1) {
      myFile = SD.open("mqlog.txt", FILE_WRITE);
      if (myFile) {
        myFile.print("Ram libre : ");
        myFile.print(ramfree);
        myFile.print(" octets, Boucle no. ");
        myFile.println(debug);
        myFile.close();
      }
    }
    
    // Fin tempo
  }
  
}


// Réception des message MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  // Affiche le message contenu dans le payload, attention pointeur !
  // Recoit seulement si on a subscribe au topic, cf MQTT_client.subscribe
  // Topic type node/xx/truc/yy

  char buff_message[length+1]; // 'length' caracteres max pour le message
  String topicToParse = topic;
  byte i;
  for(i=0;i<length;i++) {
    buff_message[i] = char(payload[i]);
  }
  buff_message[i] = 0; // buff message est une string
  
  // On analyse le topic
  if (topicToParse.substring(0,4) != "node") return; // Si le topic ne commence pas par "node" on arrete la...
  //Serial.println("On a un topic NODE...");
  if (topicToParse.substring(5,7) == "00") return; // un message du node 00, pas besoin de le traiter

  if (topicToParse.substring(5,7) == "01") { //On traite les requete pour node01
    //Serial.println("Message bon pour analyse...");
    if (topicToParse.substring(8,12) == "swit") { // Action d'un switch, ON OFF, on envoi au node concerne via nrf24l01.
      //Serial.print("Action du switch, node ");
      //Serial.print(topicToParse.substring(5,7));
      //Serial.print(" position ");
      
      if (buff_message[0] == '0') { 
        sendMyPacket( 0x01, 2, 0, 0, 0);
      }
      else if (buff_message[0] == '1') {
        //while(1)
        //;
        sendMyPacket( 0x01, 2, 0, 1, 0);
      }
      else return;
    }
    if (topicToParse.substring(8,12) == "sett") {
      sendMyPacket( 0x01, 5, 0, atoi(buff_message), 0);
    }
  }
  // ...
  // ...
  
}

// Initialisation de la connexion au broker et subscribe a node/
void initMQTT(void) {
  // Se connecte au broker si on n'est pas connecte :
  if (!MQTT_client.connected())
  {
      // clientID, username, MD5 encoded password
      // MQTT_client.connect("arduino-mqtt", "john@m2m.io", "00000000000000000000000000000");
      // Basic authentification
      byte i=0;
      while (!MQTT_client.connected() && i < 5) // tant qu'on est pas connecté et qu'on a fait 3 tentative de connexion
      {
        i++;
        Serial.print("Connection, tentative ");
        Serial.print(i);
        Serial.print(" result : ");
        Serial.println(MQTT_client.connect("ArduinoGateway"));
        
        if (noSD != 1) {
          myFile = SD.open("mqlog.txt", FILE_WRITE);
          if (myFile) {
            myFile.println("Connection au broker MQTT...");
            myFile.close();
          }
        }
        
        delay(4000);
      }
      if(MQTT_client.connected()) {
        delay(2000);
        Serial.println("Envoi bonjour.");
        MQTT_client.publish("node/00", "bonjour");
        delay(2000);
        Serial.println("Subscribe node#");
        MQTT_client.subscribe("node/#");
        Serial.println("Fin connexion au broker MQTT.");
        
        if (noSD != 1) {
          myFile = SD.open("mqlog.txt", FILE_WRITE);
          if (myFile) {
            myFile.println("Connection OK sur raspMosquitto.");
            myFile.close();
          }
        }
        // WATCHDOG
        wdt_enable(WDTO_8S);
        Serial.println("Activation Watchdog: 8Sec.");
      }
      else {
        if (noSD != 1) {
          myFile = SD.open("mqlog.txt", FILE_WRITE);
          if (myFile) {
            myFile.println("Echec de connection au broker.");
            myFile.close();
          }
        }
      }
  }
}

void sendTemp(void) {
  // Buffer pour chaine de caractere
  char buff_message[10]; // ATTENTION 6 caracteres seulement
  // Lit la temperature
  float temp = analogRead(temp_pin);
  temp = temp * 0.48828125;
  // envoi la temperature au broker :
  if(MQTT_client.connected()) {
    dtostrf(temp, 4, 1, buff_message); // float to string
    MQTT_client.publish("node/00/temp/00", buff_message);
    Serial.print("Envoi MQTT node/00/temp/00 : ");
    Serial.println(buff_message);
  }
  else {
    Serial.println("Data MQTT not sent.");
  }
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

// RAM dispo
int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
