/*
 WeatherNode
 
 E. PORTE
 
 V 0.1
 Remonte Presssion et température
 Remonte Lux
 Remonte Température et humidité


*/
#include <SPI.h>
#include <Wire.h>
#include "RF24.h"
#include <SFE_BMP180.h>
#include <BH1750.h>
//#include <dht.h>
#include "LowPower.h"
#include <OneWire.h> // Inclusion de la librairie OneWire
 
#define DS18B20 0x28     // Adresse 1-Wire du DS18B20
#define BROCHE_ONEWIRE 2 // Broche utilisée pour le bus 1-Wire
 
OneWire ds(BROCHE_ONEWIRE); // Création de l'objet OneWire ds

const byte debug = 0;

// NRF24
const byte SENDER=0x03; // Le node météo
RF24 radio(8,9);
uint8_t myAddress[] = { SENDER,'d','o','m','O' };
uint8_t toAddress[] = { 0xFF,'d','o','m','O' };


// You will need to create an SFE_BMP180 object, here called "pressure":

SFE_BMP180 pressure;
BH1750 lightMeter;

//dht DHT;

//#define DHT22_PIN 2

struct
{
    uint32_t total;
    uint32_t ok;
    uint32_t crc_error;
    uint32_t time_out;
    uint32_t connect;
    uint32_t ack_l;
    uint32_t ack_h;
    uint32_t unknown;
} stat = { 0,0,0,0,0,0,0,0};

struct rfPacket {
  byte sender; // numero du node
  byte type; // type de capteur 0=temp, 1=swit, 2=ping, ...
  byte sensor; // numero du capteur
  byte dataB;
  float dataF;
};

void sendRetry(byte destination, byte type, byte sensor, byte dataB, float dataF, byte retry);
int sendMyPacket(byte destination, byte type, byte sensor, byte dataB, float dataF);

void setup()
{
  Serial.begin(57600);
  Serial.println("REBOOT");

  pinMode(3, OUTPUT);

  // Initialize the sensor (it is important to get calibration values stored on the device).

  if (pressure.begin())
    Serial.println("BMP180 init success");
  else
  {
    // Oops, something went wrong, this is usually a connection problem,
    // see the comments at the top of this sketch for the proper connections.

    Serial.println("BMP180 init fail\n\n");
    //hile(1); // Pause forever.
  }
  lightMeter.begin();

  
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
  radio.startListening();                 // Start listening ON A PAS BESOIN D'ECOUTER POUR LE MOMENT
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging
  //radio.openWritingPipe(addresses[1]);
  //radio.openReadingPipe(1,addresses[0]);
  radio.stopListening(); 
  
  //delay(2000);
}

void loop()
{
  char status;
  double T,P;

  uint16_t lux = lightMeter.readLightLevel();
  sendRetry( 0x00, 10, 0, 0, lux, 3);
  //sendMyPacket( 0x00, 10, 0, 0, lux);
  Serial.print("Light: ");
  Serial.print(lux);
  Serial.println(" lux");

  /*int chk = DHT.read22(DHT22_PIN);
  switch (chk)
    {
    case DHTLIB_OK:
        stat.ok++;
        Serial.print("OK,\t");
        break;
    case DHTLIB_ERROR_CHECKSUM:
        stat.crc_error++;
        Serial.print("Checksum error,\t");
        break;
    case DHTLIB_ERROR_TIMEOUT:
        stat.time_out++;
        Serial.print("Time out error,\t");
        break;
    case DHTLIB_ERROR_CONNECT:
        stat.connect++;
        Serial.print("Connect error,\t");
        break;
    case DHTLIB_ERROR_ACK_L:
        stat.ack_l++;
        Serial.print("Ack Low error,\t");
        break;
    case DHTLIB_ERROR_ACK_H:
        stat.ack_h++;
        Serial.print("Ack High error,\t");
        break;
    default:
        stat.unknown++;
        Serial.print("Unknown error,\t");
        break;
    }
  sendMyPacket( 0x00, 9, 0, 0, DHT.humidity);
  Serial.print(DHT.humidity, 1);
  Serial.print(",\t");
  Serial.print(DHT.temperature, 1);
  Serial.println(",\t");
  sendMyPacket( 0x00, 1, 0, 0, DHT.temperature);*/
  
  // Temp sparkfun i2c
  byte _status;
  unsigned int H_dat, T_dat;
  float temp=-1, hygro=-1;
  _status = fetch_humidity_temperature(&H_dat, &T_dat);
  hygro = (float) H_dat / 16382 * 100;
  //temp = (float) T_dat / 16382 * 165 - 40.0;
  sendRetry( 0x00, 9, 0, 0, hygro, 3);
  //sendMyPacket( 0x00, 9, 0, 0, hygro);
  Serial.print(hygro, 1);
  Serial.print(",\t");
  Serial.print(temp, 1);
  Serial.println(",\t");
  //delay(20);
  //sendRetry( 0x00, 1, 0, 0, temp, 3);
  //sendMyPacket( 0x00, 1, 0, 0, temp);
  if(getTemperature(&temp)) {
    getTemperature(&temp);
    delay(50);
    sendRetry( 0x00, 1, 0, 0, temp, 3);
  }
  
  
  
  status = pressure.startTemperature();
  if (status != 0)
  {
    delay(status);

    status = pressure.getTemperature(T);
    if (status != 0)
    {
      sendRetry( 0x00, 1, 1, 0, T, 3);
      //sendMyPacket( 0x00, 1, 1, 0, T);
      // Print out the measurement:
        Serial.print("BMP180: ");
        Serial.print(T,2);
        Serial.print(" C, ");
      
      status = pressure.startPressure(3);
      if (status != 0)
      {
        delay(status);

        status = pressure.getPressure(P,T);
        if (status != 0)
        {
          //delay(10);
          sendRetry( 0x00, 11, 0, 0, P, 3);
          //sendMyPacket( 0x00, 11, 0, 0, P);
          // Print out the measurement:
            Serial.print(P,2);
            Serial.println(" mb, ");
        }
        else Serial.println("error retrieving pressure measurement\n");
      }
      else Serial.println("error starting pressure measurement\n");
    }
    else Serial.println("error retrieving temperature measurement\n");
  }
  else Serial.println("error starting temperature measurement\n");
  
  //delay(10000);
  delay(50);
  radio.powerDown();
  delay(50);
  //delay(5000);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  //LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  delay(50);
  radio.powerUp();
  delay(50);

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
  //radio.stopListening(); ////////////// On a pas besoin d'ecouter
  //Serial.println(F("Now sending."));
  if (!radio.write( &myPacket, sizeof(myPacket) )){  Serial.println(F("Failed send myPacket.")); return 0; }
  //radio.startListening();//////////////// On a pas besoin d'ecouter
  return 1;
}

//Recupoeration temerature
byte fetch_humidity_temperature(unsigned int *p_H_dat, unsigned int *p_T_dat)
{
      byte address, Hum_H, Hum_L, Temp_H, Temp_L, _status;
      unsigned int H_dat, T_dat;
      address = 0x27;;
      Wire.beginTransmission(address); 
      Wire.endTransmission();
      delay(100);
      
      Wire.requestFrom((int)address, (int) 4);
      Hum_H = Wire.read();
      Hum_L = Wire.read();
      Temp_H = Wire.read();
      Temp_L = Wire.read();
      Wire.endTransmission();
      
      _status = (Hum_H >> 6) & 0x03;
      Hum_H = Hum_H & 0x3f;
      H_dat = (((unsigned int)Hum_H) << 8) | Hum_L;
      T_dat = (((unsigned int)Temp_H) << 8) | Temp_L;
      T_dat = T_dat / 4;
      *p_H_dat = H_dat;
      *p_T_dat = T_dat;
      return(_status);
}

// Fonction récupérant la température depuis le DS18B20
// Retourne true si tout va bien, ou false en cas d'erreur
boolean getTemperature(float *temp){
  byte data[9], addr[8];
  // data : Données lues depuis le scratchpad
  // addr : adresse du module 1-Wire détecté
 
  if (!ds.search(addr)) { // Recherche un module 1-Wire
    ds.reset_search();    // Réinitialise la recherche de module
    return false;         // Retourne une erreur
  }
   
  if (OneWire::crc8(addr, 7) != addr[7]) // Vérifie que l'adresse a été correctement reçue
    return false;                        // Si le message est corrompu on retourne une erreur
 
  if (addr[0] != DS18B20) // Vérifie qu'il s'agit bien d'un DS18B20
    return false;         // Si ce n'est pas le cas on retourne une erreur
 
  ds.reset();             // On reset le bus 1-Wire
  ds.select(addr);        // On sélectionne le DS18B20
   
  ds.write(0x44, 1);      // On lance une prise de mesure de température
  delay(800);             // Et on attend la fin de la mesure
   
  ds.reset();             // On reset le bus 1-Wire
  ds.select(addr);        // On sélectionne le DS18B20
  ds.write(0xBE);         // On envoie une demande de lecture du scratchpad
 
  for (byte i = 0; i < 9; i++) // On lit le scratchpad
    data[i] = ds.read();       // Et on stock les octets reçus
   
  // Calcul de la température en degré Celsius
  *temp = ((data[1] << 8) | data[0]) * 0.0625;
   
  // Pas d'erreur
  return true;
}
