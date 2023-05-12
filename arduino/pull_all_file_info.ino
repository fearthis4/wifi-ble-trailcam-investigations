/**
 * TRAIL Cam Bridge Example test for Dsoon wifi/ble trailcam
 * Sets up trailcam wifi mode and pulls all files info down and prints to serial
 * Requires Partitioning Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 * Built from ESP wifi and BLE Client library examples
 * Updated by Chris Jones
 */

#include "BLEDevice.h"
//#include "BLEScan.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <map>

// WIFI Settings
static String targetAPName = "Trail Cam Pro";
static char* targetAPPassword = "12345678";

// Target Server IP
static char* targetIp = "192.168.1.8";

// Rest Endpoints
static char* setupModeBase = "SetMode?Setup";
static char* menuOptionsReq = "Setup?GetMenuJson";
static char* storageModeReq = "SetMode?Storage";
static char* dirInfoReq = "Storage?GetDirFileInfo";
static char* filePageInfoReq = "Storage?GetFilePage="; // need to provide page # and "&type=" type
static char* downloadThumbnailReq = "Storage?GetFileThumb=";
static char* downloadFileReq = "Storage?Download=";
static char* deleteFileReq = "Storage?Delete=";
static char* enableLiveFeedReq = "SetMode?PhotoCapture";
static char* turnOffWifiReq = "Misc?PowerOff";

static boolean doWifiConnect = false;
static boolean WifiConnected = false;
static boolean doWifiScan = false;
static char* ssidToConnect = "";
const int wifiConnectRetries = 10;

// Bluetooth Settings
// The remote service we search for to find device.
static BLEUUID searchUUID("0000feb3-0000-1000-8000-00805f9b34fb");
// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000ff00-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("0000ff01-0000-1000-8000-00805f9b34fb");
// Target Value to send
static String SENDVALUE = "BT_Key_On";

static boolean doBLEConnect = false;
static boolean BLEConnected = false;
static boolean doBLEScan = false;
static BLERemoteCharacteristic* pRemoteBLECharacteristic;
static BLEAdvertisedDevice* myBLEDevice;
// File Dict
std::map<String, String> fileDict;
static int totalFiles = 0;

char* convertConstCharToChar(const char* constChar) {
  // Allocate a new buffer on the heap
  char* newChar = new char[strlen(constChar) + 1];
  // Copy the string to the new buffer
  strcpy(newChar, constChar);
  return newChar;
}

static void notifyBLECallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    Serial.print("data: ");
    Serial.println((char*)pData);
}

class MyBLEClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("BLE Connected");
  }

  void onDisconnect(BLEClient* pclient) {
    Serial.println("BLE Disconnected");
  }
};

bool connectToBLEServer() {
    Serial.print("Forming a BLE connection to ");
    Serial.println(myBLEDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyBLEClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myBLEDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to BLE server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our target BLE service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our target BLE service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteBLECharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteBLECharacteristic == nullptr) {
      Serial.print("Failed to find our target BLE characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteBLECharacteristic->canRead()) {
      std::string value = pRemoteBLECharacteristic->readValue();
      Serial.print("The BLE characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteBLECharacteristic->canNotify())
      pRemoteBLECharacteristic->registerForNotify(notifyBLECallback);

    BLEConnected = true;
    return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are searching for.    
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(searchUUID)) {
      BLEDevice::getScan()->stop();
      // Set target myBLEDevice 
      myBLEDevice = new BLEAdvertisedDevice(advertisedDevice);
      // Set doBLEConnect to true to connect
      doBLEConnect = true;
      //doBLEScan = true;
    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

void disableBluetooth() {
  BLEDevice::deinit(true); // Deinitialize the BLE stack and free resources
}

void disableWifi() {
  WiFi.mode(WIFI_OFF);
}

class WiFiManager {
public:
  WiFiManager() {}
  void begin();
  void scanForNetwork(const String& substring);
  bool connectToNetwork(const char* ssid, const char* password);
  void setOnConnectCallback(void (*callback)());
private:
  void (*_onConnectCallback)() = nullptr;
};

void WiFiManager::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
}

void WiFiManager::scanForNetwork(const String& substring) {
  Serial.print("Scanning for Wi-Fi networks...");
  int numNetworks = WiFi.scanNetworks();
  Serial.println("Scan complete!");
  if (numNetworks == 0) {
    Serial.println("No networks found.");
  } else {
    Serial.print(numNetworks);
    Serial.println(" networks found.");
    for (int i = 0; i < numNetworks; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.indexOf(substring) >= 0) { // Check if the SSID contains the substring
        Serial.print("Matching network found!");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(ssid);
        Serial.print(" (");
        Serial.print(WiFi.RSSI(i));
        Serial.println(" dBm)");
        // Set target AP SSID to connect        
        ssidToConnect = convertConstCharToChar(ssid.c_str());
        break;
      }
    }
  }
}

bool WiFiManager::connectToNetwork(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wifi AP: ");
  Serial.println(ssid);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    retries += 1;
    if (retries == int(wifiConnectRetries)) {
      Serial.println("Failed to connect to target Wifi AP!");
      return false;
    }
  }
  Serial.println("\nConnected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  //if (_onConnectCallback) {
  //  _onConnectCallback(); // Call the connect callback, if set    
  //}
  return true;
}

void WiFiManager::setOnConnectCallback(void (*callback)()) {
  _onConnectCallback = callback;
}

// Define the callback function to be called after successful connection
void onConnectCallback() {
  Serial.println("Connected to network!");
}

WiFiManager wifiManager; // Create an instance of WiFiManager

String sendGetRequest(const char* servername, const char* uri, String params = "") {
  String url = "http://" + String(servername) + "/" + String(uri);
  if (params.length() > 0) {
    //url += "?" + params;
    url += params;
  }
  Serial.println("Sending request:\n" + url);
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  String resp = "";
  if (httpCode == HTTP_CODE_OK) {
    resp = http.getString();
    Serial.println("Response:\n" + resp);
  }
  else {
    Serial.print("Request failed: ");
    Serial.println(httpCode);
  }  

  http.end();
  return resp;
}

void printEachFile() {
  // create an iterator to traverse the map
  std::map<String, String>::iterator it;

  // loop through the map using the iterator
  for (it = fileDict.begin(); it != fileDict.end(); ++it) {
    // get the key and value of the current element
    String key = it->first;
    String value = it->second;

    // do something with the key and value
    Serial.print("Filename: ");
    Serial.println(key);
    Serial.print("File ID: ");
    Serial.println(value);
  }
}

int loadJSONFileData(String jsonString) {
  // Define the maximum size of the JSON document
  const size_t bufferSize = JSON_ARRAY_SIZE(totalFiles) + totalFiles*JSON_OBJECT_SIZE(5) + 100;
  // Allocate a buffer for the JSON document
  DynamicJsonDocument doc(bufferSize);
  // Load the JSON
  auto err = deserializeJson(doc, jsonString);
  if (err) {
    Serial.println("deserializeJson() failed");
    Serial.println(err.c_str());    
  }
  int numFiles = doc["number_of_files"];
  JsonArray fileArray = doc["fs"];
  Serial.println("Parsing JSON file array..");
  //for (JsonVariant file: fileArray) {
  //for (int i = 0; i < numFiles; i++) {
  for (int i = 0; i < fileArray.size(); i++) {
    JsonObject fileObject = fileArray[i];

    String filename = fileObject["n"].as<String>();
    String fid = fileObject["fid"].as<String>();
    Serial.println("Adding " + filename + ": " + fid + " to file dict map.");
    // Add to the file dict
    fileDict[filename] = fid;
  }
  return fileArray.size();
}

void getAllFileInfo() {
  StaticJsonDocument<200> jd;
  Serial.println("Setting Trail Cam api to 'storage mode'.");
  // Send Request to put server into storage mode
  String resp = sendGetRequest(targetIp, storageModeReq, "");
  // Send Request to get the directory info 
  Serial.println("Getting Trail Cam directory info.");
  resp = sendGetRequest(targetIp, dirInfoReq, "");
  auto err = deserializeJson(jd, resp);
  if (err) {
    Serial.println("deserializeJson() failed");
    Serial.println(err.c_str());    
  }
  int numDirs = jd["NumberOfDirs"];
  totalFiles = jd["NumberOfFiles"];
  //int numImages = jd["NumberOfJPG"];
  //int numVideos = jd["NumberOfAVIS"];
  int fc = 0;
  int cd = 0;   
  // Send a request for each directory until all files are pulled
  while (true) {
    // Pull all files for each directory
    Serial.println("Getting Trail Cam file info for all files stored in directory: " + String(cd));
    // Send Request to get file info
    String resp = sendGetRequest(targetIp, filePageInfoReq, String(cd));
    // Check response is not empty   
    if (resp.length() == 0) {
      // Breakout of loop on blank response
      break;
    }
    // Load the JSON File data into File Dict and increment file count
    fc += loadJSONFileData(resp);
    // Get data by file type:
    // Check if there are image files
    // if (numImages > 0) {
    //   Serial.printf("Getting Trail Cam file info for images stored in directory: '%s'.");
    //   // Setup params string for file info request
    //   String params = i + "&type=Photo";
    //   // Send Request to get file info
    //   String resp = sendGetRequest(targetIp, filePageInfoReq, params);
    // }
    // // Check if there are video files
    // if (numVideos > 0) {
    //   Serial.printf("Getting Trail Cam file info for videos stored in directory: '%s'.");
    //   // Setup params string for file info request
    //   String params = i + "&type=Video";   
    //   // Send Request to get file info
    //   String resp = sendGetRequest(targetIp, filePageInfoReq, params);
    // }
    if (fc >= totalFiles) {
      break;
    }
    cd++;     
  }
}

void turnRemoteServerWifiOff() {
  Serial.println("Turning off the Trail Cam's Wifi now..");  
  // Send Request to turn off Trail Cam's wifi
  String resp = sendGetRequest(targetIp, turnOffWifiReq, "");
  Serial.println(resp);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Trail Cam Bridge application...");
  
  // Initialize BLE
  BLEDevice::init("");
  Serial.println("Searching for target Trail Cam BLE device with matching Service UUID: ");
  Serial.println(searchUUID.toString().c_str());

  // Start initial BLE Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  doBLEScan = true;
} // End of setup.

// This is the Arduino main loop function.
void loop() {
  
  // If the bool "doWifiConnect" is true then we have scanned for and found the target Wifi AP
  if (doWifiConnect == true) {
    if (wifiManager.connectToNetwork(ssidToConnect, targetAPPassword)) {
      Serial.println("We are now connected to the Trail Cam Wifi AP.");
      // Set Wifi Connected Flag
      WifiConnected = true;
      // Reset do Wifi Connect flag as we are now connected
      doWifiConnect = false;
    } else {
      Serial.println("We have failed to connect to the Trail Cam Wifi AP..");
    } 
  }
  
  // If the bool "doBLEConnect" is true then we have scanned for and found the target BLE Server
  if (doBLEConnect == true) {
    if (connectToBLEServer()) {
      Serial.println("We are now connected to the BLE Server.");
      // Set BLEConnected bool
      BLEConnected = true;
      // Reset doBLEConnect bool now that we are connected
      doBLEConnect = false;
    } else {
      Serial.println("We have failed to connect to the BLE server.");
    }
  }

  // Check if we are connected to a peer BLE Server
  if (BLEConnected) {
    // Send value to target characteristic to enable wifi on trail camera
    Serial.println("Sending Value: \"" + SENDVALUE + "\" to BLE characteristic.");      
    pRemoteBLECharacteristic->writeValue(SENDVALUE.c_str(), SENDVALUE.length());
    // Disable Bluetooth
    disableBluetooth();
    // Reset BLEConnected bool
    BLEConnected = false;
    // Reset doBLEScan bool
    doBLEScan = false;
    // Enable WIFI Scan
    doWifiScan = true;
    // Pause Loop        
    delay(100);
  }

  // Check if Wifi is connected  
  if (WifiConnected) {
    // Send requests        
    getAllFileInfo();
    // Print the file data
    Serial.println("File data populated:");
    printEachFile();
    // Send Request to Turn off Trail Cam Wifi
    turnRemoteServerWifiOff();
    // Disconnect from Wifi AP
    WiFi.disconnect();
    // Disable Wifi
    disableWifi();
    // Reset the WifiConnected bool
    WifiConnected = false;
    // Re-enable BLE Scan
    //doBLEScan = true;    
  }
  
  // Check if Wifi scan is enabled
  if (doWifiScan) {
    // Enable Wifi
    wifiManager.begin();
    // Set the connect callback    
    wifiManager.setOnConnectCallback(onConnectCallback);
    // Scan for network
    wifiManager.scanForNetwork(targetAPName);
    // Check that target Wifi AP is found
    if (strlen(ssidToConnect) > 0) {
      // Target AP found
      // Reset doWifiScan bool
      doWifiScan = false;
      // Enable Wifi Connect
      doWifiConnect = true;
    } else {
      Serial.println("No matching WIFI AP found yet..");
      delay(100);
    }
  }
  // Check if BLE Scan is enabled
  else if (doBLEScan) {
    BLEDevice::getScan()->start(5);  // Start scan after disconnect
  }
  
  delay(100); // Delay a second between loops.
} // End of loop
