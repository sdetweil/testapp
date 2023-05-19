

#ifdef ESP32

  #include <Esp.h>

  // use the NIMBLE ble library , instead of builtin if 1
  //#define USE_NIMBLE 0

  #ifdef USE_NIMBLE
    #include <NimBLEDevice.h>
    #define BLERead NIMBLE_PROPERTY::READ 
    #define BLEWrite NIMBLE_PROPERTY::WRITE 
    #define BLENotify NIMBLE_PROPERTY::NOTIFY
  #else
    #include <BLEDevice.h>
    #include <BLEServer.h>
    #include <BLEUtils.h>
    #include <BLEScan.h>
    #include <BLEAdvertisedDevice.h>

    #define BLE_HS_ADV_F_DISC_GEN                   0x02
    #define BLE_HS_ADV_F_BREDR_UNSUP                0x04
    #define BLERead     BLECharacteristic::PROPERTY_READ  
    #define BLEWrite    BLECharacteristic::PROPERTY_WRITE  
    #define BLENotify   BLECharacteristic::PROPERTY_NOTIFY  
  #endif
  BLEScan *pBLEScanner;
#endif


//
// if we are doing over the air updates
//
#if defined(USE_OTA)
  unsigned long  OtaDelay = (1 * 1000);
  unsigned long startOtaTime = 0;
  bool OTARunning=false;
  BLEService *OTAService=null;
  void startOTAService(const char *value);
  #include <ArduinoBleOTA.h>
  #include <BleOtaUploadCallbacks.h>
#endif

// enable serial print debugging if 1
#define DEBUG 1

String SWVersion = STR(SW_VERSION);
String HW_NAME_INFO = STR(HW_NAME);
BleOtaVersion HW_VER = {1, 0, 0};
String SW_NAME = "xxBeacon";
BleOtaVersion SW_VER ;
//
// our UUID parts
//
// our APP prefix
String UUIDPrefix = "00000000";
String UUIDEnd = "-27b9-42f0-82aa-2e951747bbf9";
// construct appid once
String AppUUID  = String(UUIDPrefix + UUIDEnd);
// how many services will we have (occupied(1) and not(0))
#define NUM_SERVICES 2
BLEService* activeService[NUM_SERVICES] = {null, null};
void startAdvertising();
String uuids[NUM_SERVICES] ={"00000001"+UUIDEnd,"00000011"+UUIDEnd};

//
// flags
//
// when advertisng = true
bool advertising = false;
// when tof or OTA service needed
bool Changed = true;
// tof indicates person occupies space
bool status = false;

//
//  used for our BLE device name
//
String DeviceName = "";

//
// used for the config data written thru ble
//
String  fileName = "";

// amount of time to record partner sensor readings
unsigned timeout = 300000; // 5 * 60 * 1000;  // 30 seconds
// last time this timer was reset
unsigned long lastHeard = 0;
unsigned long lastConnected = 0;
unsigned connectTimeout = 15000;

// amount of time to scan, before restarting scn (hangs otherwise)
unsigned scanTimeout = 10 * 1000;
// last time this timer was reset
unsigned long scanCheck = 0;


#ifdef ESP32
  // variables used to make code same between esp32 and other libs
  #define BLEByteCharacteristic BLECharacteristic
  #define BLEStringCharacteristic BLECharacteristic
  BLEServer *pServer = NULL;
  BLEAdvertising *pAdvertising = null;
  bool deviceConnected = false;
  bool oldDeviceConnected = false;
#endif
//
//  our characteristics
//
// turn on/off LED
BLECharacteristic *LEDCharacteristic;
#define LEDChar "9A61"

// turn on /off Relay with powers sensors
BLECharacteristic *RelayCharacteristic;
#define RelayChar  "9A62"

// get this package software version, used to locate new binary if need be
BLECharacteristic *SWVersionCharacteristic;
#define SWVersionChar  "9A63"

// used to trigger swapping in the OTA service (service...)
BLECharacteristic *StartOTACharacteristic;
#define StartOTAChar "9A64"

// a characteristic in case we need to signal that a service was changed
BLECharacteristic *ServiceChangedCharacteristic;
#define ServChangeChar "2A05"

// our config data read(config and navigate apps) /write (config app)
BLECharacteristic *configCharacteristic;
#define ConfigChar "9AFF"

BLEDevice central;
BLEService *setupService(int index);


#if DEBUG
  #define Sprintln(a) Serial.println(a)
  #define Sprint(a) Serial.print(a)
  #define Sprintf(a,...) Serial.printf(a)
  #else
  #define Sprintln(a)
  #define Sprint(a)
  #define Sprintf(a)
#endif

//
//  worker function to create characteristic
// make other code smaller and make it consistent regardless of libe being used
//
BLECharacteristic *
createCharacteristic(String ID, int Properties, int size,
                     void *callbacks,
                     BLEService *service)
{
#ifdef ESP32
  BLECharacteristic *pCharacteristic = service->createCharacteristic(
      ID.c_str(), Properties);

  pCharacteristic->setCallbacks((BLECharacteristicCallbacks *)callbacks);

#else

    BLECharacteristic *pCharacteristic = new BLECharacteristic(ID.c_str(), Properties, size, true);
    pCharacteristic->setEventHandler(Properties, (BLECharacteristicEventHandler)callbacks);
    // add a type??  2902?
    service->addCharacteristic(*pCharacteristic);

#endif
  Sprintln("after Characteristic "+ID+" created");
  return pCharacteristic;
}

//
//  worker function to create the UUID strng from config info and live status status
//
String *
makeUUIDString(int index)
{  
  return uuids[index];
}

//
//
//  worker function to make start advertise consistent in rest of code 
//
void startAdvertising()
{
  Sprintln("starting advertising");
#ifdef ESP32
  #ifdef USE_NIMBLE
    pServer->startAdvertising();
  #else
    BLEDevice::startAdvertising();
  #endif
    Sprintln(activeService[Occupied]->getUUID().toString().c_str());
#else
  BLE.advertise();
  Sprintln(activeService[Occupied]->uuid());
#endif
  Sprint("after starting advertising uuid=");
  advertising = true;
}
//
//
//  worker function to make stop advertise consistent in rest of code
//
void stopAdvertising()
{
#ifdef ESP32
  BLEDevice::stopAdvertising();
#else
  BLE.stopAdvertise();
#endif
  advertising = false;
}

#ifdef ESP32
//
//
// BLE handlers for connect/disconnect 
//
//
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Sprintln("connected");
      lastConnected = millis();
    };

#if !defined(USE_NIMBLE)
    void onDisconnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) {
      BLEAddress remote_addr(param->disconnect.remote_bda);
      lastConnected = 0;
      Sprintln("disconnected");
    }
#endif

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      lastConnected = 0;     
        if (advertising) {
          Sprint("on disconnect start advertising= ");
          if(!OTARunning)
            Sprintln(activeService[Occupied]->getUUID().toString().c_str());
          else
            Sprintln(OTAService->getUUID().toString().c_str());
            #ifdef ESP32
              pServer->getAdvertising()->start();
              BLEDevice::startAdvertising();
            #else
              BLE.advertise();
            #endif
        }
      Sprintln("disconnected");
    }
};

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {

    void onResult(BLEAdvertisedDevice
#ifdef USE_NIMBLE
                  *
#endif
                  peripheral) {

      for (int s = 0; s < peripheral
#ifdef USE_NIMBLE
           ->
#else
           .
#endif
           getServiceUUIDCount(); s++) {
        const char* dev_uuid = peripheral
#ifdef USE_NIMBLE
                               ->
#else
                               .
#endif
                               getServiceUUID(s).toString().c_str();
        Sprintf(("\tAdvertised service: %s \n", dev_uuid));
        if (AppUUID.equalsIgnoreCase(String(dev_uuid))) {
          Sprintf(("found our app= %s \n", dev_uuid));
          if (!advertising) {
            startAdvertising();
          }
          break;
        }
      }
    }
};

//
// BLE handlers for characteristic read/write
//
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String cuuid = String(pCharacteristic->getUUID().toString().c_str());
      Sprint("write characteristic=");
      Sprintln(cuuid);
      cuuid.toUpperCase();
      #ifdef USE_NIMBLE
        //# 0X9AFF
        //# 012345  
        cuuid = cuuid.substring(2, 6);
      #else
        //# 00009AFF-27b9-42f0-82aa-2e951747bbf9
        //# 012345678
        cuuid = cuuid.substring(4, 8);
      #endif
      std::string value = pCharacteristic->getValue();
      // turn on.off led
      if (cuuid.equals(LEDChar)) {
        Sprint("LED characteristic written, ");
        unsigned char LEDState = value[0]; //characteristic->value();
        Sprint("LED state=");
        Sprintln(LEDState);
        if ( LEDState == '0') {
          digitalWrite(led_gpio, LOW);
          Sprintln(" setting LED off");
        }
        else if ( LEDState == '1') {
          digitalWrite(led_gpio, HIGH);
          Sprintln(" setting LED on");
        }   // this is the  led characteristic)
      }
      // turn on/off relay
      else if (cuuid.equals(RelayChar)) {

        // this is the relay characteristic
        Sprint("Relay characteristic written, ");
        unsigned char RelayState = value[0]; //characteristic->value();
        Sprint("Relay state=");
        Sprintln(RelayState);
        if ( RelayState == '0') {
          digitalWrite(relay_gpio, LOW);
          Sprintln(" setting Relay off");
          configState = false;
        }
        else if ( RelayState == '1') {
          // try to wait for hardware to settle
          TOFLastScan = millis();
          digitalWrite(relay_gpio, HIGH);
          Sprintln(" setting Relay on");
          configState = true;
        }
      }
      #if defined(USE_OTA)
        else if (cuuid.equals(StartOTAChar))
        {
          // OTA start requested
          Sprintln("OTA STart requested");
          startOtaTime = millis();
        }
      #endif
      else
        {
          // this is the  config characteristic
          BLEStringCharacteristic *pc = pCharacteristic;
          Sprintln("received new config info");
          fileName += String(pc->getValue().c_str());
          Sprint("New file name: ");
          if (fileName.indexOf("}}") > 0)
          {
            Sprintln("found end of data");
            int o = fileName.indexOf("}}") + 2;
            int x = fileName.indexOf('{');
            fileName = fileName.substring(x, o);
            Sprint("end fn=");
            Sprintln(fileName);
            String foo = JSON.parse(fileName);
            // configInfo=JSON.parse(fileName);
            uint8_t size = fileName.length();
            // write out the size
            EEPROM.write(0, size);
            for (int i = 0; i < size; i++)
            {
              Sprint(fileName.charAt(i));
              EEPROM.write(i + 1, fileName.charAt(i));
            }
            Sprintln("<-eeprom write completed");
            EEPROM.commit();
            // fileName = "";
            Changed = true;
            configWritten = true;
          }
          Sprintln(fileName);
        }
      }
    void onRead(BLECharacteristic *pCharacteristic) {
      String cuuid = String(pCharacteristic->getUUID().toString().c_str());
      cuuid.toUpperCase();
      Sprint("read characteristic=");
      Sprintln(cuuid);
      #ifdef USE_NIMBLE
        //# 0X9AFF
        //# 012345  
        cuuid = cuuid.substring(2, 6);
      #else
        //# 00009AFF-27b9-42f0-82aa-2e951747bbf9
        //# 012345678
        cuuid = cuuid.substring(4, 8);
      #endif
      Sprint("read UUID=");
      Sprintln(cuuid);
      if (cuuid.equals(ConfigChar)) {
        Sprintln("read config info");
        String x = JSON.stringify(configInfo);
        const char *xp = x.c_str();
        Sprintln(x.c_str());

        pCharacteristic->setValue((uint8_t*)xp, x.length()); //send_buffer);
        //pCharacteristic->setWriteProperty(true);
        Sprintln("done sending config info");
      } else {
        String xp = (HW_NAME_INFO+","+SWVersion);
        pCharacteristic->setValue((uint8_t*)xp.c_str(), xp.length()); //send_buffer);
        Sprintln("done sending config info");
      }
    }
};

#endif



#ifndef ESP32
//
//  worker functions for connect/disconnect
//
void blePeripheralConnectHandler(BLEDevice central) {
  // central connected event handler
  Sprint("Connected event, central: ");
  Sprintln(central.address());
  Sprint("central rssi=");
  Sprintln(central.rssi());
  Sprint(" address=");
  Sprintln(central.address());
  lastConnected = millis();
}
void blePeripheralDisconnectHandler(BLEDevice central) {
  // central disconnected event handler
  Sprint("Disconnected event, central: ");
  Sprintln(central.address());
  lastConnected = 0;
}

// 
// worker functions for characteristic read/write
//

void LEDCharacteristicWrite(BLEDevice central, BLECharacteristic *characteristic) {
  Sprint("LED characteristic written, ");
  unsigned char LEDState = *(unsigned char*)(characteristic->value());
  Sprint("LED state=");
  Sprintln(LEDState);
  if ( LEDState == '0') {
    digitalWrite(led_gpio, LOW);
    Sprintln(" setting LED off");
  }
  else if ( LEDState == '1') {
    digitalWrite(led_gpio, HIGH);
    Sprintln(" setting LED on");
  }
}
void StartOTACharacteristicWrite(BLEDevice central, BLECharacteristic *characteristic)
{
  Sprint("LED characteristic written, ");
  unsigned char LEDState = *(unsigned char *)(characteristic->value());
  Sprint("LED state=");
  Sprintln(LEDState);
  if (LEDState == '0')
  {
    digitalWrite(led_gpio, LOW);
    Sprintln(" setting LED off");
  }
  else if (LEDState == '1')
  {
    digitalWrite(led_gpio, HIGH);
    Sprintln(" setting LED on");
  }
}
void RelayCharacteristicWrite(BLEDevice central, BLECharacteristic *characteristic) {
  Sprint("Relay characteristic written, ");
  unsigned char RelayState = *(unsigned char*)characteristic->value();
  Sprint("Relay state=");
  Sprintln(RelayState);
  if ( RelayState == '0') {
    digitalWrite(relay_gpio, LOW);
    Sprintln(" setting Relay off");
    configState = false;
  }
  else if ( RelayState == '1') {
    // try to wait for hardware to settle
    TOFLastScan = millis();
    digitalWrite(relay_gpio, HIGH);
    Sprintln(" setting Relay on");
    configState = true;
  }
}
void configCharacteristicRead(BLEDevice central, BLECharacteristic characteristic) {
  BLEStringCharacteristic *sble = (BLEStringCharacteristic*)&characteristic;
  String x = JSON.stringify(configInfo);
  Sprintln(x.c_str());
  sble->writeValue(x); //send_buffer);
  Sprintln("done writing config info for read");
}

void SWVersionCharacteristicRead(BLEDevice central, BLECharacteristic characteristic) {
  BLEStringCharacteristic *sble = (BLEStringCharacteristic*)&characteristic;
  sble->writeValue(HW_NAME+","+SWVersion); //send_buffer);
  Sprintln("done writing SWVersion  info for read");
}

void configCharacteristicWrite(BLEDevice central, BLECharacteristic *characteristic) {
  Sprint("config string characteristic written, ");
  {
    fileName += String((char*) characteristic->value());
    Sprint( "New file name: " );
    if (fileName.indexOf("}}") > 0) {
      Sprintln("found end of data");
      int o = fileName.indexOf("}}") + 2;
      int x =  fileName.indexOf('{');
      fileName = fileName.substring(x, o);
      Sprint("end fn=");
      Sprintln(  fileName );
      String foo = JSON.parse(fileName);
      configInfo = JSON.parse(fileName);
      uint8_t size = fileName.length();
      // write out the size
      EEPROM.write(0, size);
      for (unsigned int i = 0; i < fileName.length(); i++) {
        Sprint(fileName.charAt(i));
        EEPROM.write(i + 1, fileName.charAt(i));
      }
      Sprintln("<-eeprom write completed");
      EEPROM.commit();
      //fileName = "";
      Changed = true;
      configWritten = true;
    }
    Sprintln( fileName );
  }
}
void ServiceChangedCharacteristicRead(BLEDevice central, BLECharacteristic characteristic) {


}
#endif
void disconnectClient() {

#ifdef ESP32
  /* ESP_LOGD(LOG_TAG, ">> disconnectClient. GATTS IF: %d. CONN ID: %d", m_gatts_if, m_connId);
    if (m_gatts_if >= 0 and m_connId >= 0) {
    esp_err_t result = ::esp_ble_gatts_close(m_gatts_if, m_connId);
    if (result != ESP_OK) {
      ESP_LOGE(LOG_TAG, "Disconnection FAILED. Code: %d.", result);
    }
    } else {
    ESP_LOGD(LOG_TAG, "Can not disconnect Client.");
    }

    ESP_LOGD(LOG_TAG, "<< disconnectClient"); */

#else
  BLE.disconnect();
#endif
} // disconnectClient



#if USE_OTA

class  myUploadCallbacks :public BleOtaUploadCallbacks {
 void onBegin(uint32_t firmwareLength){
    Sprint("Begin upload, size=");
    Sprintln(firmwareLength);
  };
  void onEnd() {
     Sprint("Upload end");
     ESP.restart();
     // need to check and reboot here
  };
  void onError(uint8_t errorCode) {
    Sprint("upload failed rc=");
    Sprintln(errorCode);
  };
};
void startOTAService()
{
#ifdef ESP32
  #ifdef USE_NIMBLE
    pAdvertising->stop();
    pAdvertising->removeServiceUUID(activeService[Occupied]->getUUID());
  #else
    pAdvertising->stop();
    //BLEDevice::stopAdvertising();
  #endif

  OTAService = ArduinoBleOTA.begin(pServer, InternalStorage, HW_NAME_INFO.c_str(), HW_VER, SW_NAME.c_str(), SW_VER);
  ArduinoBleOTA.setUploadCallbacks( * (new myUploadCallbacks()));
  Sprintln("restart advertising after resetup");
  OTARunning=true;
  #if USE_NIMBLE
   pAdvertising->addServiceUUID(OTAService->getUUID());
   pAdvertising->start();
  #else
   Sprintln("seting OTA service as only");
   pAdvertising->setServiceUUID(OTAService->getUUID());
   pAdvertising->start();
   //BLEDevice::startAdvertising();
  #endif
#else

#endif


}
#endif
void changeServiceUUID() {
#ifdef ESP32
  #ifdef USE_NIMBLE
    pAdvertising->removeServiceUUID(activeService[!status]->getUUID());
    pAdvertising->addServiceUUID(activeService[status]->getUUID());
  #else
    pAdvertising->setServiceUUID(activeService[status]->getUUID());
  #endif

#else                     

#endif
}


/*
                            const gender = parseInt(result.advertisement.serviceUuids[0].slice(0, 1));
                            const facility = parseInt(result.advertisement.serviceUuids[0].slice(1, 2));
                            const sequence_number = parseInt(result.advertisement.serviceUuids[0].slice(2,4, 16))
                            // 2 chars of facility subtype for additional announcement info
                            const facility_type = result.advertisement.serviceUuids[0].slice(4,6);

                            const status = this.isOccupied(result.advertisement.serviceUuids[0].slice(6, 7))
*/
BLEService *  setupService(int index) {
  Sprintln("in setupservice");

  String* ServiceUUID=makeUUIDString(index);
#ifndef ESP32
  BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);
#endif


  #ifdef ESP32
    if (pServer == NULL) {
      Sprintln("create server");
      pServer = BLEDevice::createServer();
      Sprintln("ble server");
      pServer->setCallbacks(new MyServerCallbacks());
      Sprintln("setup ble server callbacks");
    }

    Sprintln("after server create");
    BLEDevice::setPower(ESP_PWR_LVL_P9);  // Changing had no affect.  Client reports power level = 3
    Sprintln("after setpower1");
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL1, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL2, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL3, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL4, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL5, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL6, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL8, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    Sprintln("after setpower2");
    BLEService *pService=  pServer->createService(ServiceUUID->c_str());  // <--- creating new service, on new server() instance
    Sprint("setup service index=");
    Sprintln(index);
    Sprintln("after service create");
    
  #endif



   LEDCharacteristic=createCharacteristic(LEDChar, BLEWrite, sizeof(led_gpio), 
    #ifdef ESP32
       new MyCallbacks()
    #else
       (void *)LEDCharacteristicWrite
    #endif
    , pService);
 
   RelayCharacteristic=createCharacteristic(RelayChar, BLEWrite, sizeof(relay_gpio), 
    #ifdef ESP32
       new MyCallbacks()
    #else
       (void *)RelayCharacteristicWrite
    #endif
    , pService);  
      

  SWVersionCharacteristic=createCharacteristic(SWVersionChar, BLERead, sizeof(SWVersion), 
    #ifdef ESP32
       new MyCallbacks()
    #else
       (void *)SWVersionCharacteristicRead
    #endif
    , pService);

   StartOTACharacteristic = createCharacteristic(StartOTAChar, BLEWrite, sizeof(SWVersion),
#ifdef ESP32
      new MyCallbacks()
#else
    (void *)StartOTACharacteristicWrite
#endif
          ,
      pService);
    //----------
    // only add the service for the 1st time
#ifdef ESP32
    pService->start();
    Sprintln("after service start"); 
                                  

    if (pAdvertising == null)
    {
      pAdvertising = BLEDevice::getAdvertising();
      pAdvertising->setScanResponse(true);
      pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
      pAdvertising->setMaxPreferred(0x12);
      pAdvertising->addServiceUUID(pService->getUUID());
    }

#endif
    Sprintln("after advertising setup, in setupService");
    return pService;
}

void restartServices()
{

    #ifdef ESP32
      #ifdef USE_NIMBLE
        pAdvertising->stop();
      #else
        BLEDevice::stopAdvertising();
      #endif
    #else
    BLE.stopAdvertise();
    #endif
    #ifdef USE_NIMBLE
    pAdvertising->removeServiceUUID(activeService[!Occupied]->getUUID());
    pAdvertising->addServiceUUID(activeService[Occupied]->getUUID());
    #else
    
    #endif
    #ifdef ESP32
      #ifdef USE_NIMBLE
        pAdvertising->start();
      #else
        BLEDevice::startAdvertising();
      #endif
    #else
      BLE.advertise();
    #endif
}

#ifdef ESP32
void createScanner() {
  pBLEScanner = BLEDevice::getScan();
  //pBLEScanner->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScanner->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScanner->setInterval(100);
  pBLEScanner->setWindow(99);  // less or equal setInterval value
}
#endif
void setup() {

  float swinfo = atof(SWVersion.c_str());
  // "1.00"
  int value = (swinfo * 100.0);
  // Extract each digit with the 'modulo' operator (%)

  uint8_t hundredsDigit =  ((value / 100) % 10);
  // 1
  uint8_t tensDigit = ((value / 10) % 10);
  // 0
  uint8_t onesDigit = (value % 10);
  // 0
  SW_VER = {hundredsDigit, tensDigit, onesDigit};

#if DEBUG
  Serial.begin(115200);
  delay(100);
// while (!Serial)
// continue;
#endif
  Serial.print("hardware name=");
  Serial.println(HW_NAME_INFO);
  Sprintln("starting");

  Sprintln("ble setup");
  // begin initialization
#ifdef ESP32
  BLEDevice::init("test");

  BLEAddress deviceAddress = BLEDevice::getAddress();
  Sprintln("after get address");
  DeviceName = String(deviceAddress.toString().c_str());
  Sprint("DeviceName=");
  Sprintln(DeviceName);
  #ifdef USE_NIMBLE
    BLEDevice::setDeviceName(DeviceName.c_str());
  #else
    esp_ble_gap_set_device_name(DeviceName.c_str());
  #endif

  delay(100);
  Sprintln("after init");

#endif
  for(int i=0; i<NUM_SERVICES; i++){
    activeService[i]=setupService(i);
  }
#ifdef ESP32
  Sprint("setting addvertised service");
  Sprintln(activeService[Occupied]->getUUID().toString().c_str());
  //pAdvertising = BLEDevice::getAdvertising();
  //pAdvertising->addServiceUUID(activeService[Occupied]->getUUID());
  pAdvertising->setScanResponse(true);
#endif
  //pAdvertising->setMinPreferred(0x00);  // set value to 0x00 to not advertise this parameter
  Sprintln("after advertise setup ");
#if AdvertiseAtStartup
  startAdvertising();
  Sprintln("advertising");
#endif

  scanCheck = lastHeard = millis();
#ifdef ESP32
  createScanner();
#endif
  // startup assumption
  PreviousStatus  = status = false;
  Sprintln("end of setup");
}

//////////////////////////////////////////////// loop here

void loop() {
  // put your main code here, to run repeatedly:
  // Sprintln("checking for connecting remotes");

  unsigned long now = millis();
  #ifdef USE_OTA
    if(startOtaTime !=0 && now>startOtaTime+OtaDelay){
    startOtaTime = 0;
    startOTAService();
    }
    if(OTARunning){
      ArduinoBleOTA.pull();
    }
  #endif
  if (now > connectTimeout + lastConnected)
    disconnectClient();
#ifdef ESP32
  // if not advertising, we are still looking for our app
  //if(!advertising){
    // scan to see iff our app is around
    BLEScanResults foundDevices = pBLEScanner->start(2 /*scanTimeout/1000*/, true);
    //Sprint("Devices found: ");
    //Sprintln(foundDevices.getCount());
    //Sprintln("Scan done!");
    for (int i = 0; i < foundDevices.getCount(); i++) {
      BLEAdvertisedDevice peripheral = foundDevices.getDevice(i);
      //Sprintf(("Advertised Device: %s \n", peripheral.toString().c_str()));
      for (int s = 0; s < peripheral.getServiceUUIDCount(); s++) {
        //const char* dev_uuid = peripheral.getServiceUUID(s).toString().c_str();
        //Sprintf(("\tAdvertised service: %s \n", peripheral.getServiceUUID(s).toString().c_str()));
        //Sprintf("App UUID= %s=%s \n",AppUUID.c_str(),peripheral.getServiceUUID(s).toString().c_str());
        if (AppUUID.equalsIgnoreCase(String(peripheral.getServiceUUID(s).toString().c_str()))) {
          if (!advertising) {
            Serial.printf("found our app= %s \n", peripheral.getServiceUUID(s).toString().c_str());
            startAdvertising();
          } else {
            Sprintln("already advertising to our app");;
          }
          // indicate we heard app
          lastHeard = now;;
          break;
        }
      }
    }
    pBLEScanner->clearResults();
    pBLEScanner->stop();
  //} 
#endif


  //Sprintln("checking for changed");
  if (Changed == true) {    
    // if we were advertising (some client app in the room)
    if (advertising) {
      if (PreviousStatus  != status || configWritten == true) { // don't redo if no change is state
        Sprintln(" need to restart the service with updated UUID");
        // redo the UUID to include the status status change
        restartServices();
        Sprintln("updated service");
        PreviousStatus  = Occupied;
      }
      Changed = false;
      configWritten = false;
    }
    if (now > lastHeard + timeout) {
      Sprintln("silence");
    }  else {

      Sprintln("end of changed check");
    }
    //last=now;
  }



#ifdef ESP32
  // check for central connecting
  if (deviceConnected) {
    delay(10); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
  if(!OTARunning){
    if( (advertising == true) && (deviceConnected == false) && ((lastHeard + timeout) < now) ) {
      Sprintln("haven't heard our app for a while, stop advertising");
      stopAdvertising();
      // make sure or LED is not on
      digitalWrite(led_gpio, LOW);
      lastHeard = now;
    }
  }

#endif
  delay(500);

}

