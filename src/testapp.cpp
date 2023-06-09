
#define ST(A) #A
#define STR(A) ST(A)
unsigned long looptimeout=15000;
unsigned long next=0;
#define AdvertiseAtStartup 1
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
#define OTA_IDLE 0
#define OTA_Running 1
#define OTA_Pending 2
  unsigned int OTARunning = OTA_IDLE;
 
  BLEService *OTAService = NULL;
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
#define NUM_SERVICES 2
BLEService* activeService[NUM_SERVICES] = {NULL, NULL};
void startAdvertising();
String uuids[NUM_SERVICES] ={"00000001"+UUIDEnd,"00000011"+UUIDEnd};
unsigned char dummy=0;

unsigned long nextScan = 0;
unsigned scanDelay = 10000; // 10 seconds between start of scan cycle looking for app

//
// flags
//
// when advertisng = true
bool advertising = false;
// when tof or OTA service needed
bool Changed = true;
// tof indicates person occupies space
bool status = false;
bool PreviousStatus=false;
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
  BLEAdvertising *pAdvertising = NULL;
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
String 
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
    Sprintln(activeService[status]->getUUID().toString().c_str());
#else
  BLE.advertise();
  Sprintln(activeService[status]->uuid());
#endif
  Sprint("after starting advertising");
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

    void onDisconnect(BLEServer* pServer) {
      Sprintln("disconnect callback");
      deviceConnected = false;
      lastConnected = 0;     
        if (advertising) {  // if we SHOULD be advertising
          Sprintln("on disconnect");          
          if (OTARunning == OTA_Running){
            Sprintln("restart advertising ");
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
        }
      Sprintln("client disconnected");
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
        if ( LEDState == '0') {\
          Sprintln(" setting LED off");
        }
        else if ( LEDState == '1') {
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
          Sprintln(" setting Relay off");        }
        else if ( RelayState == '1') {
          // try to wait for hardware to settle
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
            Changed = true;
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

        pCharacteristic->setValue((uint8_t*)"test", 4); //send_buffer);
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

void disconnectClient()
{

#ifdef ESP32
#ifdef USE_NIMBLE
    // disconnect the(any) connected clients
    for (auto it : pServer->getPeerDevices())
    {
      pServer->disconnect(it);
    }
    while (pServer->getConnectedCount() > 0)
    {
      yield();
    }

#else
    if (pClient != null)
    {
      pClient->disconnect();
    }
#endif
    delay(100);
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
  OTARunning = OTA_Pending;
#ifdef ESP32
  advertising = false;
  pAdvertising->stop();
  Sprintf("removing advertised UUID=");
  Sprintln(activeService[status]->getUUID().toString().c_str());
  pAdvertising->removeServiceUUID(activeService[status]->getUUID());
  delay(50);
#ifdef USE_NIMBLE
  Sprintln("nimble adding OTA service");
  pServer->addService(OTAService);
#else
  // start the Over The Air update service
  Sprintln("NOT nimble creating OTA service");
  OTAService = ArduinoBleOTA.begin(pServer, InternalStorage, HW_NAME_INFO.c_str(), HW_VER, SW_NAME.c_str(), SW_VER);
#endif
  // disconnect the(any) connected clients
  disconnectClient();
  OTARunning = OTA_Running;
  ArduinoBleOTA.setUploadCallbacks(*(new myUploadCallbacks()));
  Sprintln("restart advertising after OTA Service setup");
  Sprintln("setting OTA service as only");
  Sprint("added UUID to advertising, should be only=");
  Sprintln(OTAService->getUUID().toString().c_str());
#if USE_NIMBLE
  pAdvertising->addServiceUUID(OTAService->getUUID());
#else
  pAdvertising->setServiceUUID(OTAService->getUUID());
#endif
  Sprintln("restart advertising");
  pAdvertising->start();
  advertising = true;
#else
  // non esp32 , aka arduino
#endif
}
#endif
void changeServiceUUID() {
#ifdef ESP32
  #ifdef USE_NIMBLE
    pAdvertising->removeServiceUUID(activeService[!status]->getUUID());
    pAdvertising->addServiceUUID(activeService[status]->getUUID());
    Sprint("done toggling, active=");
    Sprintln(activeService[status]->getUUID().toString().c_str());
  #else
    pAdvertising->setServiceUUID(activeService[status]->getUUID());
  #endif

#else                     

#endif
}

BLEService *  setupService(int index) {
  Sprintln("in setupservice");

  String ServiceUUID=makeUUIDString(index);
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
      pServer->advertiseOnDisconnect(false);
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
    BLEService *pService=  pServer->createService(ServiceUUID.c_str());  // <--- creating new service, on new server() instance
    Sprint("setup service index=");
    Sprintln(index);
    Sprintln("after service create");
    
  #endif



   LEDCharacteristic=createCharacteristic(LEDChar, BLEWrite, sizeof(dummy), 
    #ifdef ESP32
       new MyCallbacks()
    #else
       (void *)LEDCharacteristicWrite
    #endif
    , pService);
 
   RelayCharacteristic=createCharacteristic(RelayChar, BLEWrite, sizeof(SCAN_DUPLICATE_MODE_NORMAL_ADV_ONLY), 
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
                                  

    if (pAdvertising == NULL)
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
    pAdvertising->removeServiceUUID(activeService[!status]->getUUID());
    pAdvertising->addServiceUUID(activeService[status]->getUUID());
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
  Sprintln(activeService[status]->getUUID().toString().c_str());
  //pAdvertising = BLEDevice::getAdvertising();
  //pAdvertising->addServiceUUID(activeService[status]->getUUID());
  pAdvertising->setScanResponse(true);
#endif
  #ifdef USE_NIMBLE
    #ifdef USE_OTA
      // create the service
      OTAService = ArduinoBleOTA.begin(pServer, InternalStorage, HW_NAME_INFO.c_str(), HW_VER, SW_NAME.c_str(), SW_VER);
      // but remove it til needed... Nimble library requirement
      pServer->removeService(OTAService, false);
      pAdvertising->removeServiceUUID(OTAService->getUUID());
    #endif
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
  // set toggle timeout;
  next = millis()+looptimeout;
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
    if(OTARunning == OTA_Running){
      ArduinoBleOTA.pull();
    }
  #endif
    if (now > next && OTARunning==OTA_IDLE && startOtaTime==0){
      pAdvertising->stop();
      Sprintln("toggling uuid");
      status = !status; // toggle
      changeServiceUUID();
      pAdvertising->start();
      next = now + looptimeout;
    }

#ifdef ESP32
  // if not advertising, we are still looking for our app
  //if(!advertising){

    // scan to see iff our app is around
  if (now > nextScan && OTARunning == 0 && startOtaTime==0){
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
    nextScan = now + scanDelay;
  }
  //} 
#endif


  //Sprintln("checking for changed");
  if (Changed == true) {    
    // if we were advertising (some client app in the room)
    if (advertising) {
      if (PreviousStatus  != status) { // don't redo if no change is state
        Sprintln(" need to restart the service with updated UUID");
        // redo the UUID to include the status status change
        restartServices();
        Sprintln("updated service");
        PreviousStatus  = status;
      }
      Changed = false;

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
  if(OTARunning==OTA_IDLE){
    if( (advertising == true) && (deviceConnected == false) && ((lastHeard + timeout) < now) ) {
      Sprintln("haven't heard our app for a while, stop advertising");
      stopAdvertising();
      lastHeard = now;
    }
  }

#endif
  delay(500);

}

