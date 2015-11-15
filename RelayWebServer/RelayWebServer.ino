/*
Relay Web Server
RadioShack Ethernet shield + K1FSY Relay driver shield = RelayWebServer 
*/

#include <SPI.h>
#include <EthernetV2_0.h>
#include <EthernetUdpV2_0.h>

#define BUTTONS 5

static int BUTTONVALS[BUTTONS][3] = {// low,  hi, pin
  { 830, 860, 2 },
  { 665, 690, 3 },
  { 490, 515, 6 },
  { 330, 350, 5 },
  { 155, 180, 7 }
};

int lastButtonState = 0;
int outputDelay = 0;
long debounce;
long lastClientTime;
long firstClientTime;

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = { 
  0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE };
IPAddress ip(192,168,1, 177);
IPAddress syslogip(192,168,1,254);
EthernetUDP Udp;
String HTTP_req;

// Initialize the Ethernet server library
// with the IP address and port you want to use 
// (port 80 is default for HTTP):
EthernetServer server(80);

#define W5200_CS  10
#define SDCARD_CS 4

void setup() {
  pinMode(SDCARD_CS,OUTPUT);
  pinMode(W5200_CS,OUTPUT);
  digitalWrite(SDCARD_CS,HIGH);//Deselect the SD card
  digitalWrite(W5200_CS,LOW);//select ethernet

  for(int i = 0; i < BUTTONS; i++) {
    pinMode(BUTTONVALS[i][2],OUTPUT);
  }
  // start the Ethernet connection and the server:
  if(Ethernet.begin(mac) == 0) {
    Ethernet.begin(mac,ip); 
  }
  server.begin();
}

void loop() {
  readButtonStates();

  EthernetClient client = server.available();
  if (client) {
    boolean currentLineIsBlank = true;
    while(client.connected()) { 
      if (client.available()) {
        char c = client.read();
        HTTP_req += c;  // save the HTTP request 1 char at a time
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connnection: keep-alive");
          client.println();

          // process any requests
          if( HTTP_req.indexOf("getrelaystate") >  -1) { 
            printForm(client);
          }
          else if(HTTP_req.indexOf("setrelay") > -1) {
            int x = HTTP_req.indexOf("setrelay");
            //hack: we know it is at 14 based on url, should do better matching
            int sw = HTTP_req.charAt(14) - '0';
            // brief sanity check
            if(sw < BUTTONS) 
              digitalWrite(BUTTONVALS[sw][2], digitalRead(BUTTONVALS[sw][2]) ^ 1);            
          }
          else {
            // main html          
            client.println("<!DOCTYPE HTML>");
            client.println("<html>");
            // add a meta refresh tag, so the browser pulls again every 5 seconds:
            client.println("<title>Coax relay controller</title>");
            client.println("<script>");
            client.println("function tcb(x) { id=x.value; var r = new XMLHttpRequest(); r.open(\"GET\", \"setrelay/\" + id, true); r.send(null);}");
            client.println("function GetSwitchState() {");
            client.println("nocache = \"&nocache=\" + Math.random() * 1000000;");
            client.println("var request = new XMLHttpRequest();");
            client.println("request.onreadystatechange = function() {");
            client.println("if (this.readyState == 4) {");
            client.println("if (this.status == 200) {");
            client.println("if (this.responseText != null) {");
            client.println("document.getElementById(\"switch_txt\").innerHTML = this.responseText;");
            client.println("}}}}");
            client.println("request.open(\"GET\", \"getrelaystate\" + nocache, true);");
            client.println("request.send(null);");
            client.println("setTimeout('GetSwitchState()', 1000);");
            client.println("}");
            client.println("</script>");
            client.println("</head>");
            client.println("<body onload=\"GetSwitchState()\">");
            client.println("<h1>Relay Controller</h1>");
            client.println("<div id=\"switch_txt\" width=\"50%\">Waiting for update</div>");

            //printForm(client);

            client.println("</body>");
            client.println("</html>");
          }
          HTTP_req = "";
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } 
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();    
  }
}

void readButtonStates() {
  int sensorValue = analogRead(A0);
  int i = 0;
  long now = millis();
  if(sensorValue > (lastButtonState + 10) || sensorValue < (lastButtonState - 10)) {          
    if((now - debounce) > 200) {
      for(i = 0; i < BUTTONS; i++) {
        if(sensorValue > BUTTONVALS[i][0] && sensorValue < BUTTONVALS[i][1]) {
          // flip the bit, use digitalR/W because of the different ports
          digitalWrite(BUTTONVALS[i][2], digitalRead(BUTTONVALS[i][2]) ^ 1);
          debounce = now;
        } 
      }
    }
    lastButtonState = sensorValue;
    //    savePortsToEEPROM();
  }  
}

void writeRelayOutput(EthernetClient client) {
  String state;
  for(int i=2;i<10;i++) {
    state += digitalRead(i);
  }
  client.println(state);
}

void printForm(EthernetClient client) {
  client.println("<form action=\"#\" method=\"post\" class=\"relayform\" id=\"relayform\" width=\"35%\">");
  client.println("<fieldset><legend>Relay switch</legend><div id=\"relays\">Relays:");
  for(int i=0; i<BUTTONS;i++) {
    client.print("<label><input type=\"checkbox\" name=\"SW");
    client.print(i);
    client.print("\" value=\"");
    client.print(i);
    if(digitalRead(BUTTONVALS[i][2]) == HIGH) {
      client.print("\" CHECKED onchange=\"tcb(this)\" />SW");
    } else {
      client.print("\" onchange=\"tcb(this)\" />SW");
    }
    client.print(i);
    client.println("</label>");
  }
  client.println("</div></fieldset></form>");
}

void checkServer()
{
  if((millis()-lastClientTime) > 30000) {
    // reset all server sockets every 30 seconds of inactivity
    server.reset();
    lastClientTime=millis();
  } 
}
