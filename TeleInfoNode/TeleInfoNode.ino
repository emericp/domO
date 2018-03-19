/*

TeleInfoNode

On récupère tout les message envoyé par le compteur,
on les parse et vérifie, checksum,
on envoi la consomation instantané au node0,
toute les "tempo", on envoi les autres infos :
- La journée de demain si on l'a
- Tout les matin à 6h (changement de couleur), on envoi le resumé des KWH consommée, et eventuellement le prix
- On envoi a chaque changement de couleur, heure creuses pleines le compteur
C'est tout pour le node00 et openHAB
Si on branche un ecran on affiche qq infos :
- Puissance inst, mode actuel, demain. Qq chose de simple !

04/02/2015 v0.1
-Recupere chaque message et les stock dans une structure de String.
-Checksum controlé.


.
Sketch repris de domotique-info.fr, modifié pour faire un node domO

Hardware needed :
  + 1 x Arduino
  + 1 x Opto Coupler : SFH620A
  + 1 x LED
  + 1 x 1 k? resistor
  + 1 x 4,7 k? resistor

  
ADCO 039801280547 F
OPTARIF BBR+ V
ISOUSC 45 ?
BBRHCJB 037761543 A
BBRHPJB 043140016 =
BBRHCJW 006181665 S
BBRHPJW 006169316 _
BBRHCJR 002282520 B
BBRHPJR 002950277 Z
PTEC HCJR S
DEMAIN ROUG +
IINST 013 [
IMAX 033 E
HHPHC Y D
MOTDETAT 000000 B

*/

// ECRAN
#include "SPI.h"
#include "Adafruit_GFX.h"
#include <Adafruit_ST7735.h> 
#define TFT_CS     10
#define TFT_RST    6  // you can also connect this to the Arduino reset
                      // in which case, set this #define pin to 0!
#define TFT_DC     5
// ----

#include <SoftwareSerial.h>
#include <EEPROM.h>

// RF24
// Numéro du node
#define SENDER 0x02
#include <SPI.h>
#include "RF24.h"
#include "printf.h"
// Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins ce & cs 
RF24 radio(8,9);
                     // source, destination
//byte addresses[][6] = {"2Node","0Node"};
uint8_t myAddress[] = { SENDER,'d','o','m','O' };
uint8_t toAddress[] = { 0xFF,'d','o','m','O' };
// Paquet RF
struct rfPacket {
  byte sender; // numero du node
  byte type; // type de capteur 0=temp, 1=swit, 2=ping, ...
  byte sensor; // numero du capteur
  byte dataB;
  float dataF;
};



// Caractère de début de trame
#define startFrame 0x02
// Caractère de fin de trame
#define endFrame 0x03

// On crée une instance de SoftwareSerial
SoftwareSerial* cptSerial;

struct infoStruct {
  String etiquette;
  String champ;
}infoGroup;
struct bbrhStruct {
  unsigned long cjb;
  unsigned long pjb;
  unsigned long cjw;
  unsigned long pjw;
  unsigned long cjr;
  unsigned long pjr;
  /*unsigned long cjbOld;
  unsigned long pjbOld;
  unsigned long cjwOld;
  unsigned long pjwOld;
  unsigned long cjrOld;
  unsigned long pjrOld;*/
}bbrh;
String tarifActuel = "XXXX";
String demain = "----";

// Prix
const float euroHCJB=0.0903;
const float euroHPJB=0.1075;
const float euroHCJW=0.1255;
const float euroHPJW=0.1491;
const float euroHCJR=0.2270;
const float euroHPJR=0.5894;

const int cjbOld=0;
const int pjbOld=10;
const int cjwOld=20;
const int pjwOld=30;
const int cjrOld=40;
const int pjrOld=50;

long resultKWh = 0;
float resultKWhEuro = 0;


int interval=30000;
unsigned long previousMillis=0;
int oldPower=0;
byte sendChoice=0;

int getTrame(void);
void sendRetry(byte destination, byte type, byte sensor, byte dataB, float dataF, byte retry);
int sendMyPacket(byte destination, byte type, byte sensor, byte dataB, float dataF);

// ECRAN
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);
// -----

// Fonction d'initialisation de la carte Arduino, appelée
// 1 fois à la mise sous-tension ou après un reset
void setup()
{
  // On initialise le port utilisé par le "moniteur série".
  // Attention de régler la meme vitesse dans sa fenêtre
  Serial.begin(57600);
  Serial.println(F("setup RF24..."));
  
  radio.begin();                          // Start up the radio
  // CONFIG du Module NRF24, a mettre sur tout les modules :
  radio.setAutoAck(1);                    // Ensure autoACK is enabled
  radio.setRetries(15,15);                // Max delay between retries & number of retries  
  //radio.setPALevel(RF24_PA_MAX);          // Puissance max
  radio.setChannel(123);                  // Canal numero 123
  radio.setPayloadSize(8);                // Payload de 8bytes seulement
  radio.setDataRate(RF24_250KBPS);        // Debit à 250kbps
  // Fin config
  radio.startListening();                 // Start listening
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging
  //radio.openWritingPipe(addresses[1]);
  radio.openReadingPipe(1,myAddress);
  Serial.println(F("setup SoftSerial..."));
  // On définit les PINs utilisées par SoftwareSerial, 
  // 8 en réception, 9 en émission (en fait nous ne 
  // ferons pas d'émission)
  cptSerial = new SoftwareSerial(7, 6);
  // On initialise le port avec le compteur EDF à 1200 bauds :
  //  vitesse de la Télé-Information d'après la doc EDF
  cptSerial->begin(1200);
  Serial.println(F("setup complete"));
 
  // ECRAN
  pinMode(3, OUTPUT);
  analogWrite(3, 200);
  tft.initR(INITR_BLACKTAB);   // initialize a ST7735S chip, black tab
  tft.fillScreen(ST7735_BLACK);
  tft.setRotation(1);
  tft.setTextSize(3);
  //tft.setCursor(17, 45);
  //tft.setTextColor(ST7735_WHITE,ST7735_BLACK);
  //tft.print("PLEINES");
  // -----
  
}

// Boucle principale, appelée en permanence une fois le 
// setup() terminé
void loop()
{
  // Boucle infini sans tempo

  int result = getTrame();
  int tmp;
  unsigned long tmplong;
  float tmpfloat;
  
  if (result == 1) {
    if (infoGroup.etiquette == "IINST") { // si le message est courannt instantane :
      //Serial.print("Puissance instantanee.");
      //Serial.print(infoGroup.champ.toInt()*230);
      //Serial.println("W");
      // On envoi au node00 si la puissance à changee...
      tmp = infoGroup.champ.toInt();
      if (tmp != oldPower) { // Si la puissance à changée, on envoi la mise a jour au node0
        Serial.println("Changement de puissance, envoi au node00");
        sendMyPacket( 0x00, 6, 0, 0, tmp*230);
        oldPower = tmp;
        // ON ECRIT SUR L ECRAN:
        tft.setCursor(15, 5);
        if ((tarifActuel=="HPJB") || (tarifActuel=="HCJB")) tft.setTextColor(ST7735_BLUE,ST7735_BLACK);
        if ((tarifActuel=="HPJW") || (tarifActuel=="HCJW")) tft.setTextColor(ST7735_WHITE,ST7735_BLACK);
        if ((tarifActuel=="HPJR") || (tarifActuel=="HCJR")) tft.setTextColor(ST7735_RED,ST7735_BLACK);
        tft.setTextSize(4);
        if (tmp*230 < 1000) tft.print(" ");
        tft.print(tmp*230);
        tft.setTextSize(4);
        tft.setCursor(125, 2);
        tft.print("W");
        //
      }
    }
    if (infoGroup.etiquette == "PTEC") { // Si le message est le tarif du jour
      // On compare avec le tarif actuel, si c'est pas le même, on envoie la mise à  jour au node 0
      if (tarifActuel != infoGroup.champ) { // On vient de changer de tarif
        Serial.println("On change de tarif.");
        // On compte le nombre de kilowattheures des derniere 24h, HP + HC = kWh, HP*€ + HC*€ = Prix des 24H
        // Si on passe en heure pleine, alors on fait les comptes :
        if ((infoGroup.champ == "HPJR") || (infoGroup.champ == "HPJW") || (infoGroup.champ == "HPJB")) {
          if (tarifActuel == "HCJB") { // On regarde le tarif d'avant
            tmplong = (bbrh.cjb - EEPROMReadlong(cjbOld)) + (bbrh.pjb - EEPROMReadlong(pjbOld)); // On a les kWh total
            resultKWh = sendMyPacket( 0x00, 7, 7, 0, tmplong); // On envoi les last24kWh
            if (resultKWh == 0) resultKWh = tmplong;
            else resultKWh = 0;
            delay(200);
            tmpfloat = (float)(bbrh.cjb - EEPROMReadlong(cjbOld))*euroHCJB + (float)(bbrh.pjb - EEPROMReadlong(pjbOld))*euroHPJB;
            resultKWhEuro = sendMyPacket( 0x00, 7, 8, 0, tmpfloat); // On envoi les last24kWh en euros
            if (resultKWhEuro == 0) resultKWhEuro = tmpfloat;
            else resultKWhEuro = 0;
          }
          if (tarifActuel == "HCJW") { // On regarde le tarif d'avant
            tmplong = (bbrh.cjw - EEPROMReadlong(cjwOld)) + (bbrh.pjw - EEPROMReadlong(pjwOld));
            resultKWh = sendMyPacket( 0x00, 7, 7, 0, tmplong);
            if (resultKWh == 0) resultKWh = tmplong;
            else resultKWh = 0;
            delay(200);
            tmpfloat = (float)(bbrh.cjw - EEPROMReadlong(cjwOld))*euroHCJW + (float)(bbrh.pjw - EEPROMReadlong(pjwOld))*euroHPJW;
            resultKWhEuro = sendMyPacket( 0x00, 7, 8, 0, tmpfloat); // On envoi les last24kWh en euros
            if (resultKWhEuro == 0) resultKWhEuro = tmpfloat;
            else resultKWhEuro = 0;
          }
          if (tarifActuel == "HCJR") { // On regarde le tarif d'avant
            tmplong = (bbrh.cjr - EEPROMReadlong(cjrOld)) + (bbrh.pjr - EEPROMReadlong(pjrOld));
            resultKWh = sendMyPacket( 0x00, 7, 7, 0, tmplong);
            if (resultKWh == 0) resultKWh = tmplong;
            else resultKWh = 0;
            delay(200);
            tmpfloat = (float)(bbrh.cjr - EEPROMReadlong(cjrOld))*euroHCJR+ (float)(bbrh.pjr - EEPROMReadlong(pjrOld))*euroHPJR;
            resultKWhEuro = sendMyPacket( 0x00, 7, 8, 0, tmpfloat); // On envoi les last24kWh en euros
            if (resultKWhEuro == 0) resultKWhEuro = tmpfloat;
            else resultKWhEuro = 0;
          }
        }
      
        tmp = 0;
        if (infoGroup.champ == "HCJB") {
          if (tarifActuel != "XXXX") {
            sendMyPacket( 0x00, 7, 1, 0, bbrh.cjb);
           }
          //bbrh.cjbOld = bbrh.cjb; // On enregistre le debut du compteur, pour faire les compte à la fin...
          // dans l'eeprom, seulement si l'etat anterieur etait connu... pas XXXX
          tmp = sendMyPacket( 0x00, 8, 0, 1, 0);
          
        }
        if (infoGroup.champ == "HCJW") {
          if (tarifActuel != "XXXX") { 
            sendMyPacket( 0x00, 7, 2, 0, bbrh.cjw);
          }
          //bbrh.cjwOld = bbrh.cjw;
          tmp = sendMyPacket( 0x00, 8, 0, 2, 0);
        }
        if (infoGroup.champ == "HCJR") {
          if (tarifActuel != "XXXX") {
            sendMyPacket( 0x00, 7, 3, 0, bbrh.cjr);
          }
          //bbrh.cjrOld = bbrh.cjr;
          tmp = sendMyPacket( 0x00, 8, 0, 3, 0);
        }
        if (infoGroup.champ == "HPJB") {
          if (tarifActuel != "XXXX") { 
            EEPROMWritelong(pjbOld, bbrh.pjb);
            EEPROMWritelong(cjbOld, bbrh.cjb);
            sendMyPacket( 0x00, 7, 4, 0, bbrh.pjb);
          }
          //bbrh.pjbOld = bbrh.pjb;                   
          tmp = sendMyPacket( 0x00, 8, 0, 4, 0);
        }
        if (infoGroup.champ == "HPJW") {
          if (tarifActuel != "XXXX") {
            EEPROMWritelong(pjwOld, bbrh.pjw);
            EEPROMWritelong(cjwOld, bbrh.cjw);
            sendMyPacket( 0x00, 7, 5, 0, bbrh.pjw);
          }
          //bbrh.pjwOld = bbrh.pjw;
          tmp = sendMyPacket( 0x00, 8, 0, 5, 0);
        }
        if (infoGroup.champ == "HPJR") {
          if (tarifActuel != "XXXX") {
            EEPROMWritelong(pjrOld, bbrh.pjr);
            EEPROMWritelong(cjrOld, bbrh.cjr);
            sendMyPacket( 0x00, 7, 6, 0, bbrh.pjr);
          }
          //bbrh.pjrOld = bbrh.pjr;
          tmp = sendMyPacket( 0x00, 8, 0, 6, 0);
        }
      
        // On met a jour le tarif actuel si on a bien envoyé le paquet, sinon on change pas et on retente plus tard
        //if (tmp == 1) 
        tarifActuel = infoGroup.champ;
      }
    }
    // Mise à jour des compteur en permanence :
    if (infoGroup.etiquette == "BBRHCJB") bbrh.cjb = infoGroup.champ.toInt()/1000;
    if (infoGroup.etiquette == "BBRHPJB") bbrh.pjb = infoGroup.champ.toInt()/1000;
    if (infoGroup.etiquette == "BBRHCJW") bbrh.cjw = infoGroup.champ.toInt()/1000;
    if (infoGroup.etiquette == "BBRHPJW") bbrh.pjw = infoGroup.champ.toInt()/1000;
    if (infoGroup.etiquette == "BBRHCJR") bbrh.cjr = infoGroup.champ.toInt()/1000;
    if (infoGroup.etiquette == "BBRHPJR") bbrh.pjr = infoGroup.champ.toInt()/1000;
    // ..... Tarif du lendemain ???
    if (infoGroup.etiquette == "DEMAIN") {
      if ((infoGroup.champ != "----") && (demain == "----")) { // On a l'info et variable demain est inconnue
        // On envoi la couleur de demain
        if (infoGroup.champ == "BLEU") sendMyPacket( 0x00, 8, 1, 1, 0);
        if (infoGroup.champ == "BLAN") sendMyPacket( 0x00, 8, 1, 2, 0);
        if (infoGroup.champ == "ROUG") sendMyPacket( 0x00, 8, 1, 3, 0);
        demain = infoGroup.champ;
      }
      if ((infoGroup.champ == "----") && (demain != "----")) {
        demain = "----"; // Si on ne connait plus demain Et que que demain est valide, on dévalide.
        sendMyPacket( 0x00, 8, 1, 0, 0);
      }
    }
  }

  // Boucle infini avec tempo 5min
  unsigned long currentMillis = millis();
  if ((unsigned long)(currentMillis - previousMillis) >= interval) {
    // On envoi les infos au node toute les x minutes.
    Serial.println("Envoi derniers kWh et euros.");
          if ((tarifActuel == "HCJB") || (tarifActuel == "HPJB")) { // On regarde le tarif
            tmplong = (bbrh.cjb - EEPROMReadlong(cjbOld)) + (bbrh.pjb - EEPROMReadlong(pjbOld)); // On a les kWh total
            sendMyPacket( 0x00, 7, 9, 0, tmplong); // On envoi les last24kWh
            delay(100);
            tmpfloat = (float)(bbrh.cjb - EEPROMReadlong(cjbOld))*euroHCJB + (float)(bbrh.pjb - EEPROMReadlong(pjbOld))*euroHPJB;
            sendMyPacket( 0x00, 7, 10, 0, tmpfloat); // On envoi les last24kWh en euros
            printEuro(tmpfloat);
          }
          if ((tarifActuel == "HCJW") || (tarifActuel == "HPJW")) { // On regarde le tarif d'avant
            tmplong = (bbrh.cjw - EEPROMReadlong(cjwOld)) + (bbrh.pjw - EEPROMReadlong(pjwOld));
            sendMyPacket( 0x00, 7, 9, 0, tmplong);
            delay(100);
            tmpfloat = (float)(bbrh.cjw - EEPROMReadlong(cjwOld))*euroHCJW + (float)(bbrh.pjw - EEPROMReadlong(pjwOld))*euroHPJW;
            sendMyPacket( 0x00, 7, 10, 0, tmpfloat); // On envoi les last24kWh en euros
            printEuro(tmpfloat);
          }
          if ((tarifActuel == "HCJR") || (tarifActuel == "HPJR")) { // On regarde le tarif d'avant
            tmplong = (bbrh.cjr - EEPROMReadlong(cjrOld)) + (bbrh.pjr - EEPROMReadlong(pjrOld));
            sendMyPacket( 0x00, 7, 9, 0, tmplong);
            delay(100);
            tmpfloat = (float)(bbrh.cjr - EEPROMReadlong(cjrOld))*euroHCJR+ (float)(bbrh.pjr - EEPROMReadlong(pjrOld))*euroHPJR;
            sendMyPacket( 0x00, 7, 10, 0, tmpfloat); // On envoi les last24kWh en euros
            printEuro(tmpfloat);
          }
          
          sendChoice++;
          switch (sendChoice) {
            case 1:
              sendMyPacket( 0x00, 7, 1, 0, bbrh.cjb);
            break;
            case 2:
              sendMyPacket( 0x00, 7, 2, 0, bbrh.cjw);
            break;
            case 3:
              sendMyPacket( 0x00, 7, 3, 0, bbrh.cjr);
            break;
            case 4:
              sendMyPacket( 0x00, 7, 4, 0, bbrh.pjb);
            break;
            case 5:
              sendMyPacket( 0x00, 7, 5, 0, bbrh.pjw);
            break;
            case 6:
              sendMyPacket( 0x00, 7, 6, 0, bbrh.pjr);
            break;
            default:
            sendChoice = 0;
          }
          
          if (resultKWh != 0) {
            delay(100);
            if (sendMyPacket( 0x00, 7, 7, 1, resultKWh) == 1) resultKWh = 0;
          }
          if (resultKWhEuro != 0) {
            delay(100);
            if (sendMyPacket( 0x00, 7, 8, 1, resultKWhEuro) == 1) resultKWhEuro = 0;
          }
    
    previousMillis = currentMillis;
   }
}

void sendRetry(byte destination, byte type, byte sensor, byte dataB, float dataF, byte retry)
{
  int i=0;
  while(sendMyPacket(destination,type,sensor,dataB,dataF)==0)
  {
    i++;
    if (i>= retry) return;
  }
}

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
  toAddress[0] = destination;
  radio.openWritingPipe(toAddress);
  // On envoi...
  radio.stopListening(); // First, stop listening so we can talk.
  Serial.println(F("Now sending."));
  if (!radio.write( &myPacket, sizeof(myPacket) )){  Serial.println(F("RF24 Send Failed.")); return 0; }
  radio.startListening();
  return 1;
}

int getTrame(void)
{
  char charIn;
  int i, checksum=0;
  unsigned int timeout;
  if (cptSerial->available()) { // Si on a des caractere en attente...
    if ((cptSerial->read() & 0x7F) != 0x0A) return 0; // Si le caractere n'est pas un debut de ligne on zappe
    char trameBuffer[30];
    // La, on recupere toute la chaine de caractere jusqu'a la fin : carriage return 0x0D
    for(i=0;i<30;i++) {
      timeout = 0;
      while (!cptSerial->available() && (timeout < 20)) { timeout++; delay(1); } // On attend le caractere suivant
      charIn = cptSerial->read() & 0x7F;
      if (charIn == 0x0D) { trameBuffer[i] = 0; break; }
      else { trameBuffer[i] = charIn; checksum+=charIn; }
    }
    if (i == 29) return 0; // On est arrivé a 29, donc la trame n'est pas bonne
    // On fait le checksum, on a la somme des code ascii,
    // On reture un espace 0x20 et le caractere de checksum
    checksum-=0x20;
    checksum-=trameBuffer[i-1];
    checksum = checksum & 0x3F;
    checksum+=0x20;
    if (checksum == trameBuffer[i-1]) {
      String myInfo = trameBuffer;
      infoGroup.etiquette = myInfo.substring(0,myInfo.indexOf(' '));
      infoGroup.champ = myInfo.substring(myInfo.indexOf(' ')+1,myInfo.indexOf(' ',myInfo.indexOf(' ')+1));
      return 1; // Trame ok. 
    }
  }
  else return 0; // trame Pas de caractere dispo dans le serial
}

void printEuro(float euro)
{
  tft.setTextSize(3);
  tft.setCursor(5, 95);
  tft.setTextColor(ST7735_GREEN,ST7735_BLACK);
  tft.print(euro);
  tft.print(" Eur");
  
}

// Manip EEPROM
//This function will write a 4 byte (32bit) long to the eeprom at
//the specified address to adress + 3.
void EEPROMWritelong(int address, long value)
      {
      //Decomposition from a long to 4 bytes by using bitshift.
      //One = Most significant -> Four = Least significant byte
      byte four = (value & 0xFF);
      byte three = ((value >> 8) & 0xFF);
      byte two = ((value >> 16) & 0xFF);
      byte one = ((value >> 24) & 0xFF);

      //Write the 4 bytes into the eeprom memory.
      EEPROM.write(address, four);
      EEPROM.write(address + 1, three);
      EEPROM.write(address + 2, two);
      EEPROM.write(address + 3, one);
      }
      
      
long EEPROMReadlong(int address)
      {
      //Read the 4 bytes from the eeprom memory.
      long four = EEPROM.read(address);
      long three = EEPROM.read(address + 1);
      long two = EEPROM.read(address + 2);
      long one = EEPROM.read(address + 3);

      //Return the recomposed long by using bitshift.
      return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
      }
