#include <WiFiClient.h>
#include <ESP32WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <EEPROM.h>

#include <FS.h>
#include <LITTLEFS.h>

#include <AnimatedGIF.h>
AnimatedGIF gif;

#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();
#define FORMAT_LITTLEFS_IF_FAILED true

#define NORMAL_SPEED

char *szDir = "/"; // play all GIFs in this directory on the SD card
char fname[256];
File root, temp;
File f;

bool wifi_flag;
bool First_run_done;

String ssid_i;
String password_i;

String inputMessage = "" ;
String inputParam = "" ;
    
char ssid[100];
char password[100];

String input = "";
String command = "";

String serverIndex = "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update'>"
    "<input type='submit' value='Upload'>"
"</form>"
"<div id='prg'>progress: 0%</div>"
"<script>"
"$('form').submit(function(e){"
    "e.preventDefault();"
      "var form = $('#upload_form')[0];"
      "var data = new FormData(form);"
      " $.ajax({"
            "url: '/update',"
            "type: 'POST',"               
            "data: data,"
            "contentType: false,"                  
            "processData:false,"  
            "xhr: function() {"
                "var xhr = new window.XMLHttpRequest();"
                "xhr.upload.addEventListener('progress', function(evt) {"
                    "if (evt.lengthComputable) {"
                        "var per = evt.loaded / evt.total;"
                        "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
                    "}"
               "}, false);"
               "return xhr;"
            "},"                                
            "success:function(d, s) {"    
                "console.log('success!')"
           "},"
            "error: function (a, b, c) {"
            "}"
          "});"
"});"
"</script>";

//const char* ssid = "Shahrukh13";
//const char* password = "shahan2020";

ESP32WebServer server(80);

bool opened = false;

String printDirectory(File dir, int numTabs) {
  String response = "";
  dir.rewindDirectory();
  
  while(true) {
     File entry =  dir.openNextFile();
     if (! entry) {
       // no more files
       // Serial.println("**nomorefiles**");
       break;
     }
     for (uint8_t i=0; i<numTabs; i++) {
       Serial.print('\t');   // we'll have a nice indentation
     }
     // Recurse for directories, otherwise print the file size
     if (entry.isDirectory()) {
       printDirectory(entry, numTabs+1);
     } else {
       response += String("<a href='") + String(entry.name()) + String("'>") + String(entry.name()) + String("</a>") + String("</br>");
     }
     entry.close();
   }
   return String("List files:</br>") + response + String("</br></br> Upload file:") + serverIndex;
}

void handleRoot() {
  root = LITTLEFS.open("/");
  String res = printDirectory(root, 0);
  server.send(200, "text/html", res);
}

bool loadFromSDCARD(String path){
  path.toLowerCase();
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";

  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".txt")) dataType = "text/plain";
  else if(path.endsWith(".zip")) dataType = "application/zip";  
  Serial.println(dataType);
  File dataFile = LITTLEFS.open(path.c_str());

  if (!dataFile)
    return false;

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    Serial.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void handleNotFound(){
  if(loadFromSDCARD(server.uri())) return;
  Serial.println("SDCARD Not Detected");
}


void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  f = LITTLEFS.open(fname);
  if (f)
  {
    *pSize = f.size();
    return (void *)&f;
  }
  return NULL;
} /* GIFOpenFile() */


void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
} /* GIFCloseFile() */


int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
} /* GIFReadFile() */

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{ 
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
//  Serial.printf("Seek time = %d us\n", i);
  return pFile->iPos;
} /* GIFSeekFile() */


bool wifi_state = false;

#define DEBOUNCE_TIME 250
volatile uint32_t DebounceTimer = 0;

void IRAM_ATTR buttonpressed() 
{
  if ( millis() - DEBOUNCE_TIME  >= DebounceTimer ) 
  {
    DebounceTimer = millis();
    wifi_state = ! wifi_state;
  }
}

void setup(void){
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(35,INPUT);     // Wifi_connect_button
  pinMode(27,INPUT);     //First module select ->  If HIGH this is the first ESP32
  pinMode(26,OUTPUT);    // Notify next ESP32 to play GIF
  pinMode(25,INPUT);     // Receive notification from Last ESP32 to start playing GIF again
  attachInterrupt(35, buttonpressed, FALLING);

  tft.begin();
#ifdef USE_DMA
  tft.initDMA();
#endif
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(1); // Any text size muliplier will work
  tft.setTextColor(TFT_RED, TFT_BLUE);
  tft.setTextDatum(TC_DATUM); // Top Centre is datum
                                // any datum could be used
  tft.setTextPadding(0); // Setting to zero switches off padding
  

  gif.begin(BIG_ENDIAN_PIXELS);

  tft.drawString("Ready", 80, 0, 4);

  display_menu();
  First_run_done = false;
}

void ShowGIF(char *name)
{
  
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
  {
    //Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    tft.startWrite(); // The TFT chip slect is locked low
    
    if(digitalRead(27)==HIGH)
    {
      if(First_run_done == false)
      {
        while (gif.playFrame(true, NULL))
        {
          yield();
        }
      }
      else
      {
        digitalWrite(26,LOW);
        if(digitalRead(25)==HIGH)
        {
          while (gif.playFrame(true, NULL))
          {
            yield();
          }
        }
      }

      First_run_done = true;
      digitalWrite(26,HIGH);
    }


    else
    {
      digitalWrite(26,LOW);
      if(digitalRead(25)==HIGH)
      {
        while (gif.playFrame(true, NULL))
        {
          yield();
        }
      }
      digitalWrite(26,HIGH);
       //__asm__("nop\n\t");
       //__asm__("nop\n\t");
       delay(8);
    }

    gif.close();
    tft.endWrite(); // Release TFT chip select for other SPI devices
  }

} /* ShowGIF() */

//
// Return true if a file's leaf name starts with a "." (it's been erased)
//
int ErasedFile(char *fname)
{
int iLen = strlen(fname);
int i;
  for (i=iLen-1; i>0; i--) // find start of leaf name
  {
    if (fname[i] == '/')
       break;
  }
  return (fname[i+1] == '.'); // found a dot?
}

void loop(void){
  server.handleClient();

  if(wifi_state && WiFi.status() != WL_CONNECTED)
  { 
    WiFi.mode(WIFI_STA);

    ssid_i = read_String(0);
    password_i = read_String(101);

    ssid_i.toCharArray(ssid, ssid_i.length()+1);
    password_i.toCharArray(password, password_i.length()+1);
    WiFi.begin(ssid, password);
    Serial.println("");

      // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tft.drawString("Connecting", 80, 0, 4);
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  tft.drawString(ssid, 80, 0, 4);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  tft.drawString(WiFi.localIP().toString(), 80, 50, 4);
  Serial.println("Initialization done.");
  delay(3000);
  //handle uri  
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  
  
  /*handling uploading file */
  server.on("/update", HTTP_POST, [](){
    server.sendHeader("Connection", "close");
  },[](){
    HTTPUpload& upload = server.upload();
    if(opened == false){
      opened = true;
        LITTLEFS.remove(fname);
        yield();
        //delay(100);
        root = LITTLEFS.open((String("/") + upload.filename).c_str(), FILE_WRITE); 

      if(!root){
        Serial.println("- failed to open file for writing");
        return;
      }
    } 
    if(upload.status == UPLOAD_FILE_WRITE){
      //deleteFile(LITTLEFS, fname);
      if(root.write(upload.buf, upload.currentSize) != upload.currentSize){
        Serial.println("- failed to write");
        return;
      }
    } else if(upload.status == UPLOAD_FILE_END){
      root.close();
      Serial.println("UPLOAD_FILE_END");
      opened = false;
    }
  });

  server.begin();
  Serial.println("HTTP Server started");
  
    tft.drawString(ssid, 80, 0, 4);
    tft.drawString(WiFi.localIP().toString(), 80, 50, 4);
    wifi_state = 0;
    delay(5000);
  }

  if(wifi_state == 1 && WiFi.status() == WL_CONNECTED)
  {
    Serial.println(WiFi.localIP().toString());

    tft.setTextSize(1); // Any text size muliplier will work
    tft.setTextColor(TFT_RED, TFT_BLUE);
    tft.setTextDatum(TC_DATUM); // Top Centre is datum
                                // any datum could be used
    tft.setTextPadding(0); // Setting to zero switches off padding
    tft.drawString(ssid, 80, 0, 4);
    tft.drawString(WiFi.localIP().toString(), 80, 50, 4);
    
    wifi_state = 0;
    delay(5000);
  }
  
   if(!LITTLEFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
        Serial.println("LITTLEFS Mount Failed");
        return;
    }
    //Serial.println( "SPIFFS-like write file to new path and delete it w/folders" );


   
      root = LITTLEFS.open(szDir);
      if (root)
      {
         temp = root.openNextFile();
            while (temp)
            {
              if (!temp.isDirectory()) // play it
              {
                strcpy(fname, temp.name());
                if (!ErasedFile(fname))
                {
                  //Serial.printf("Playing %s\n", temp.name());
                  //Serial.flush();
                  ShowGIF((char *)temp.name());
                }
              }
              temp.close();
              temp = root.openNextFile();
            }
         root.close();
      } 
scan_connect_del();
}

void writeString(char add,String data)
{
  int _size = data.length();
  int i;
  for(i=0;i<_size;i++)
  {
    EEPROM.write(add+i,data[i]);
  }
  EEPROM.write(add+_size,'\0');   //Add termination null character for String Data
  EEPROM.commit();
}


String read_String(char add)
{
  int i;
  char data[100]; //Max 100 Bytes
  int len=0;
  unsigned char k;
  k=EEPROM.read(add);
  while(k != '\0' && len<500)   //Read until null character
  {    
    k=EEPROM.read(add+len);
    data[len]=k;
    len++;
  }
  data[len]='\0';
  return String(data);
}

void scan_connect_del()
{
    while (Serial.available() > 0)
    {
      //input += (char) Serial.read(); 
      input =Serial.readString();
      //command =input.substring(0,input.length()-2);  //both NL and CR
      command =input.substring(0,input.length());  // No newline
      if(command == "scan")
      {
        Serial.println("scan start");
        // WiFi.scanNetworks will return the number of networks found
        int n = WiFi.scanNetworks();
        Serial.println("scan done");
        if (n == 0) 
        {
          Serial.println("no networks found");
        } 
        else 
        {
          Serial.print(n);
          Serial.println(" networks found");
           for (int i = 0; i < n; ++i) 
           {
             // Print SSID and RSSI for each network found
             Serial.print(i + 1);
             Serial.print(": ");
             Serial.print(WiFi.SSID(i));
             Serial.print(" (");
             Serial.print(WiFi.RSSI(i));
             Serial.print(")");
             Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
             delay(10);
           }
         }
         Serial.println("");
         Serial.print("If you want to connect to a new network, enter 'ssid' and 'passowrd' in following format: ssid,password");
         Serial.println();
        }

       
      else
      {
        if(input.substring(0,input.indexOf(',')) == "delete")
        {
          Serial.print("Deleting file: ");
          Serial.print(input.substring(input.indexOf(',')+1));
          LITTLEFS.remove(input.substring(input.indexOf(',')+1));
          Serial.println();
        }
        else
        {
          ssid_i = input.substring(0,input.indexOf(','));
          password_i =   input.substring(input.indexOf(',')+1);
  
          writeString(0, ssid_i);
          writeString(101, password_i);
  
          Serial.println();
          Serial.print("new ssid: ");
          Serial.print(ssid_i);
          Serial.print(" , ");
          Serial.print("new password: ");
          Serial.print(password_i);
          Serial.println();
           
          delay(5); 
        }
      }
    }
}

void display_menu()
{
  Serial.println();
  Serial.print("Enter 'scan' to scan for available networks");
  Serial.println();
  Serial.print("If you want to connect to a new network, enter 'ssid' and 'passowrd' in following format: ssid,password");
  Serial.println();
  Serial.print("If you want to delete a file enter 'delete' and '/FileName' in following format: delete,/FileName");
  Serial.println();
}
