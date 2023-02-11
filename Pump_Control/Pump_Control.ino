#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include "Gsender.h"
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>


#define relay D1 //relay output pin
#define pressure A0 //pressure input pin
#define burst_pipe 20 //PSI delta to trigger maintenance check
#define off_minutes 2 //number of minutes in between the pump turning on and water running through pipe and to detect a burst pipe *don't set at 1 minute* (5)
#define hr 15 //current time in hours (0-23)
#define mi 27 //current time in minutes (0-59)
#define pump_cycle 30 //seconds the pump remains off before turning back on
#define Water_left D4 //GPIO pin reading exposed traces on the left of the board
#define Water_right 10 //GPIO reading exposed traces on the right of the board
#define maintenance_email "2089494547@vtext.com" //phone number to receive maintenance updates
#define minute_between_PSI 2 //minutes elapsed between data logging PSI readings


// WiFi Router Login - change these to your router settings
const char* DNShost = "WaterTheGrass";
const char* SSID = "####";
const char* password = "####";
const char *host = "api.pushingbox.com"; // pushingbox Host
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
const char* login = "<form method='GET' action='/login'><input type='submit' value='Login'></form>";


const char* www_username = "water";
const char* www_password = "grass";
const char* www_username2 = "admin";
const char* www_password2 = "esp8266";
const char* www_realm = "Custom Auth Realm";
String authFailResponse = "Authentication Failed";


static String name[24]; //holds neighbors names for the table on website
String Error = ""; //HTML code for error
String Error_response = ""; //HTML code for error form
String Error_reason = ""; //HTML code for error reason
String maintenance_response = ""; //Stores the maintenance response
String Pump_manual = ""; //HTML code for manually turning the pump on or off
String pump_override = ""; //On or Off from manual overrride on webpage
String pump_mode = ""; //Automatic or Manual from webpage
String set_time = ""; //HTML code for setting the current time on start up
int time_hour; //the current hour (0-23) from the webpage
int time_minute; //the current minute (0-59) from the webpage
int pump_hours[24]; //holds the number of hours to run the pump for the table on website
String pump_status[24]; //holds the pump status for the table on the website
String System_Time;
float PSI_average = 0.0;
float PSI_average_past = 0.0;
int seconds_start = second();

// Create the ESP Web Server on port 80
ESP8266WebServer WebServer(80);

/*Grabs data through GET from the HTML page
   Stores all the HTML code as a string
   Inputs new data to the website
*/
String prepareHtmlPage()
{
  //Grabs all necessary form information from website
  String lastname = WebServer.arg("lastname");
  String time_period = WebServer.arg("time_period");
  int start_time = (WebServer.arg("start_time")).toInt();
  int run_hour = (WebServer.arg("run_hour")).toInt();
  int run_minute = (WebServer.arg("run_minute")).toInt();
  if (WebServer.hasArg("pump_override"))
  {
    pump_override = WebServer.arg("pump_override");
  }
  if (WebServer.hasArg("pump_mode"))
  {
    pump_mode = WebServer.arg("pump_mode");
  }
  if (pump_mode == "")
  {
    pump_mode = "Automatic";
  }
  time_hour = WebServer.arg("time_hour").toInt();
  time_minute = WebServer.arg("time_minute").toInt();
  Serial.print("The current hour is : ");
  Serial.println(time_hour);
  Serial.print("The current minute is : ");
  Serial.println(time_minute);
  Serial.print("The pump mode is: ");
  Serial.println(pump_mode);
  Serial.print("The pump manual status is: ");
  Serial.println(pump_override);

  //only grabs maintenance response if there is a CURRENT error
  if (Error != "")
  {
    maintenance_response = WebServer.arg("maintenance");
  }
  else
  {
    maintenance_response = "";
  }
  //Resets website to origional structure if maintenance has been delt with
  if (maintenance_response == "yes")
  {
    Error = "";
    Error_response = "";
    Error_reason = "";
  }

  //Store all names and times to run the pump in arrays
  //the position of the array correlates to an hour of the day
  if (time_period == "AM")
  {
    int index;
    for (int i = 0; i < run_hour; i++)
    {
      index = (start_time - 1) + i;
      name[index] = lastname;
      if (lastname != "")
      {
        pump_hours[index] = 1;
      }
      else
      {
        pump_hours[index] = 0;
      }
    }
    if (run_minute == 30)
    {
      name[index + 1] = lastname;
      if (lastname != "")
      {
        pump_hours[index + 1] = run_minute;
      }
      else
      {
        pump_hours[index] = 0;
      }
    }
  }
  else if (time_period == "PM")
  {
    int index1;
    for (int i = 0; i < run_hour; i++)
    {
      index1 = ((start_time + 12) - 1) + i;
      if (index1 == 23)
      {
        start_time = -12;
      }
      name[index1] = lastname;
      if (lastname != "")
      {
        pump_hours[index1] = 1;
      }
      else
      {
        pump_hours[index1] = 0;
      }
    }
    if (run_minute == 30)
    {
      Serial.println(name[index1 + 1]);
      name[index1 + 1] = lastname;
      if (lastname != "")
      {
        pump_hours[index1 + 1] = run_minute;
      }
      else
      {
        pump_hours[index1] = 0;
      }
    }
  }
  String htmlPage = "<html> <meta http-equiv=\"refresh\" content=\"120\"> <head> <style> .pump { display: inline-block; width: 49.6%; border: none; color: white; padding: 14px 28px; font-size: 28px; cursor: pointer; text-align: center; } .tablink { background-color: #555; color: black; float: left; border-width: 0px 3px 0px 0; /* top right bottom left */ border-style: solid solid inset double; border-color: #f00 black #00f #ff0; outline: none; cursor: pointer; padding: 14px 16px; font-size: 17px; width: 25%; } .tablink:hover { background-color: #777; } #Home { background-color: #9CBA7F; } #Data_Log { background-color: #9CBA7F; } #User_Guide { background-color: #9CBA7F; } /* Style the tab content (and add height:100% for full page content) */ .tabcontent { color: black; display: none; padding: 100px 20px; height: 100%; } .pump:hover { background-color: #ddd; color: black; } /* Green */ .on { background-color: #4CAF50; } .off{ background-color: #f44336; } </style> <h1 style=\"text-align: center;\">Water the Grass</h1>  </head> <body style=\"background-color: #9CBA7F;\"> <button class=\"tablink\" onclick=\"openPage('Home', this, '#9CBA7F')\" id=\"defaultOpen\">Home</button> <button class=\"tablink\" onclick=\"openPage('Data_Log', this, '#9CBA7F')\">Data Log</button> <button class=\"tablink\" onclick=\"openPage('User_Guide', this, '#9CBA7F')\">User Guide</button><div id=\"Home\" class=\"tabcontent\">" + set_time + "<div align=\"center\">" + Pump_manual + "<h2><strong><span style=\"color: #ff0000;\">" + Error + "</span></strong><p><span style=\"color: #ff0000;\">" + Error_reason + "</span></p></h2>" + Error_response + "<p><img border=\"3\" src='https://drive.google.com/uc?export=view&id=1KYyTr4uTn30eAdCH6A5QWWtEoGcyWpp0' alt='Ultimate Sprinkler'></p> <form method=\"get\" onsubmit=\"setTimeout(function(){window.location.reload();},5000);\"> Please enter a time slot that doesn't conflict with anyone in the table below <p>Last name: <input name=\"lastname\" type=\"text\" /></p> <p>Starting Time: <select name=\"start_time\" id=\"start_time\"> <option value=\"1\">one</option> <option value=\"2\">two</option> <option value=\"3\">three</option> <option value=\"4\">four</option> <option value=\"5\">five</option> <option value=\"6\">six</option> <option value=\"7\">seven</option> <option value=\"8\">eight</option> <option value=\"9\">nine</option> <option value=\"10\">ten</option> <option value=\"11\">eleven</option> <option value=\"12\">twelve</option> </select><select name=\"time_period\" id=\"time_period\"> <option value=\"AM\">AM</option> <option value=\"PM\">PM</option> </select> </p> <p>Run Time: <select name=\"run_hour\" id=\"run_hour\"> <option value=\"1\">one</option> <option value=\"2\">two</option> <option value=\"3\">three</option> </select>hour(s)<select name=\"run_minute\" id=\"run_minute\"> <option value=\"00\">00</option> <option value=\"30\">30</option> </select>minutes </p> <input type=\"submit\" value=\"Submit\" /></form> <form> </div> <hr size=\"4\" style=\"background-color:black;\"/> <table align=\"center\" cellspacing=\"0\" cellpadding=\"0\" border=\"3\" width=\"800\"> <tr> <td> <table cellspacing=\"0\" cellpadding=\"1\" border=\"3\" width=\"800\" > <tr> <th style=\"width: 200px;\">Time</th> <th style=\"width: 200px;\">AM/PM</th> <th style=\"width: 200px;\">Name</th> <th >Pump Status</th> </tr> </table> </td> </tr> <tr> <td> <div style=\"width:825px; height:270px; overflow:auto;\"> <table cellspacing=\"0\" cellpadding=\"1\" border=\"3\" width=\"800\" > <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">ONE</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[0] + "</td> <td style=\"text-align: center;\">" + pump_status[0] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">TWO</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[1] + "</td> <td style=\"text-align: center;\">" + pump_status[1] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">THREE</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[2] + "</td> <td style=\"text-align: center;\">" + pump_status[2] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">FOUR</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[3] + "</td> <td style=\"text-align: center;\">" + pump_status[3] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">FIVE</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[4] + "</td> <td style=\"text-align: center;\">" + pump_status[4] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">SIX</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[5] + "</td> <td style=\"text-align: center;\">" + pump_status[5] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">SEVEN</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[6] + "</td> <td style=\"text-align: center;\">" + pump_status[6] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">EIGHT</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[7] + "</td> <td style=\"text-align: center;\">" + pump_status[7] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">NINE</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[8] + "</td> <td style=\"text-align: center;\">" + pump_status[8] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">TEN</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[9] + "</td> <td style=\"text-align: center;\">" + pump_status[9] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">ELLEVEN</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[10] + "</td> <td style=\"text-align: center;\">" + pump_status[10] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">TWELVE</td> <td style=\"width: 200px; text-align: center;\">AM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[11] + "</td> <td style=\"text-align: center;\">" + pump_status[11] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">ONE</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[12] + "</td> <td style=\"text-align: center;\">" + pump_status[12] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">TWO</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[13] + "</td> <td style=\"text-align: center;\">" + pump_status[13] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">THREE</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[14] + "</td> <td style=\"text-align: center;\">" + pump_status[14] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">FOUR</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[15] + "</td> <td style=\"text-align: center;\">" + pump_status[15] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">FIVE</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[16] + "</td> <td style=\"text-align: center;\">" + pump_status[16] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">SIX</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[17] + "</td> <td style=\"text-align: center;\">" + pump_status[17] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">SEVEN</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[18] + "</td> <td style=\"text-align: center;\">" + pump_status[18] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">EIGHT</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[19] + "</td> <td style=\"text-align: center;\">" + pump_status[19] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">NINE</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[20] + "</td> <td style=\"text-align: center;\">" + pump_status[20] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">TEN</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[21] + "</td> <td style=\"text-align: center;\">" + pump_status[21] + "</td> </tr> <tr style=\"background-color: white\"> <td style=\"width: 200px; text-align: center;\">ELLEVEN</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[22] + "</td> <td style=\"text-align: center;\">" + pump_status[22] + "</td> </tr> <tr> <td style=\"width: 200px; text-align: center;\">TWELVE</td> <td style=\"width: 200px; text-align: center;\">PM</td> <td style=\"width: 200px; text-align: center;\">" +
                    name[23] + "</td> <td style=\"text-align: center;\">" + pump_status[23] + "</td> </tr> </table> </div> </td> </tr> </table> <div align=\"center\"> <p>" + System_Time + "</p> <p>Date/Time: <span id=\"datetime\"></span></p><script> var dt = new Date(); document.getElementById(\"datetime\").innerHTML = dt.toLocaleString(); function pageRedirect() { var delay = 4000; document.getElementById(\"message\").innerHTML = \"Please wait, you are redirecting to the new page.\"; setTimeout(function() { window.location = \"https://10.0.0.122\"; }, delay); } function openPage(pageName, elmnt, color) { var i, tabcontent, tablinks; tabcontent = document.getElementsByClassName(\"tabcontent\"); for (i = 0; i < tabcontent.length; i++) { tabcontent[i].style.display = \"none\"; } tablinks = document.getElementsByClassName(\"tablink\"); for (i = 0; i < tablinks.length; i++) { tablinks[i].style.backgroundColor = \"\"; } document.getElementById(pageName).style.display = \"block\"; elmnt.style.backgroundColor = color; } document.getElementById(\"defaultOpen\").click();</script> <h2>The last pressure reading is:" +
                    String(PSI_average) + " PSI</h2> <p>*Refresh the webpage to get the most current reading*</p></div> <hr size=\"4\" style=\"background-color:black;\"/> <div align=\"center\"> <form method =\"get\"> <p >Sprinkler System Mode: <select name=\"pump_mode\" id=\"pump_mode\"> <option value=\"Automatic\">Automatic</option> <option value=\"Manual\">Manual</option> </select> <p><input type=\"submit\" value=\"Change\" /></p> </form> </div> <form method =\"get\"> <button class=\"pump on\" id=\"pump_override\" name=\"pump_override\" value=\"On\">Pump On</button> <button class=\"pump off\" id=\"pump_override\"name=\"pump_override\" value=\"Off\">Pump Off</button></form></body><p align=\"center\">"+serverIndex+login+",</div><div id=\"Data_Log\" class=\"tabcontent\" align=\"center\"> <iframe width=\"875px\" height=\"100%\" frameborder=\"0\" src=\"https://docs.google.com/spreadsheets/d/e/2PACX-1vTJ8AwqcqQSjO4ctPSXnoun51YC0vhk2LZRMorJqII0PVmwa_OgHQnZbj6Rn86m0Zk9e6k71WDG7rkv/pubhtml?gid=0&amp;single=true&amp;widget=true&amp;headers=false\"></iframe> </div><div id=\"User_Guide\" class=\"tabcontent\" align=\"center\"> <iframe align=\"center\" width=\"850px\" height=\"100%\" frameborder=\"0\" src=\"https://docs.google.com/document/d/e/2PACX-1vS_m2oqZmELpFQtM8eWJ1opEbjfEcJchK85VWzgWdAvr46cmtnCxYJN_BYbzQoOZyROv-Axd8mRxcxn/pub?embedded=true\"></iframe> </div><i> Version 1.11</i></p> </html>";
  return htmlPage;
}

/*Converts the string info to HTML and sends to server*/
void response() {
  String htmlPage = prepareHtmlPage();
  WebServer.send(200, "text/html", htmlPage);
}

/*Averages the last ten PSI measurements*/
void compute_PSI_average()
{
  float send_average, pressure_reading, PSI;
  static int sample;
  static float average[10];
  pressure_reading = analogRead(pressure);
  PSI = (0.1215) * pressure_reading - 18.215; //linearized calibration equation
  if (sample == 10)
  {
    sample = 0;
  }
  average[sample] = PSI;
  send_average = 0;
  for (int i = 0; i < 10; i++)
  {
    send_average = send_average + average[i];
  }
  PSI_average = send_average / 10.0;
  sample++;
}

/*Calculates the number of minutes elapsed*/
int timer(int minute_shut_off, int begin_timer)
{
  static int timeLast;
  int timeNow = millis() / 1000; //number of seconds since start
  if (begin_timer < 1)
  {
    timeLast = timeNow;
  }
  int seconds = timeNow - timeLast;
  //Serial.println(seconds);
  if ((seconds % 60 == 0) && (seconds != 0))
  {
    timeLast = timeNow;
    minute_shut_off++;
  }
  return minute_shut_off;
}

/*detects water on either side of board*/
int water_detection() {
  if ((!digitalRead(Water_left)) || (!digitalRead(Water_right)))
  {
    Serial.println("Water detected");
    return 1;
  }
  else
  {
    return 0;
  }
}

void data_logger(String pump_mode, String pump_status[], String Error_reason1, String maintenance_response1, int PSI_average_past, int PSI_average, int current_hour)
{
  Serial.println("Entered data logger");
  Serial.print("The error reason is: ");
  Serial.println(Error_reason1);
  Serial.print("The error response is: ");
  Serial.println(maintenance_response1);
  if (maintenance_response1 == "")
  {
    maintenance_response1 = "N/A";
  }
  if (Error_reason1 == "")
  {
    Error_reason1 = "N/A";
  }
  //WebServer.close();
  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return;
  }
  // We now create a URI for the request
  String url = "/pushingbox?";
  url += "devid=";
  url += "v33DC700982D7D32";
  url += "&Mode=" + pump_mode;
  url += "&Condition=" + pump_status[current_hour];
  url += "&Details=" + Error_reason1;
  url += "&Maintenance=" + maintenance_response1;
  url += "&Before=" + String(PSI_average_past);
  url += "&During=" + String(PSI_average);
  //url += "&After=" + String(PSI_average);

  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 4000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
    Serial.print("Data Sent!");
  }
  //client.stop();
}

int pump_status_FSM(String pump_override, String pump_mode, int pump_hours1, int current_hour)
{
  int pump_condition = 0;
  static int prev_pump_condition;
  static int prev_hour;
  if ((pump_override == "On") && (pump_mode == "Manual"))
  {
    pump_condition = 1;
  }
  else if ((pump_override == "Off") && (pump_mode == "Manual"))
  {
    pump_condition = 2;
  }
  else
  {
    //    Serial.print("The pump hours are: ");
    //    Serial.println(pump_hours1);
    if (pump_hours1 == 1)
    {
      pump_condition = 3;
    }
    else if ((pump_hours1 == 30) && (minute() < 30))
    {
      pump_condition = 4;
    }
    else
    {
      pump_condition = 5;
    }
  }
  if (prev_pump_condition == pump_condition)
  {
    if (prev_hour != current_hour)
    {
    }
    else
    {
      return 0;
    }
  }
  prev_pump_condition = pump_condition;
  prev_hour = current_hour;
  return pump_condition;
}

void setup() {
  Serial.begin(115200);
  delay(10);

  //pin for Pump relay
  pinMode(relay, OUTPUT);
  pinMode(Water_left, INPUT);
  pinMode(Water_right, INPUT);
  digitalWrite(Water_right, LOW);

  // Connect to WiFi network
  Serial.println();
  WiFi.disconnect();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(SSID);
  WiFi.begin(SSID, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Connected to WiFi");


  // Start the Web Server
  //MDNS.begin(DNShost);
  //WebServer.on("/", HTTP_GET, []() {
   //   response();
   // });
//  WebServer.on("/login", HTTP_GET,[](){ 
//  if ((!WebServer.authenticate(www_username, www_password)) && (!WebServer.authenticate(www_username2, www_password2)))
//      //Basic Auth Method with Custom realm and Failure Response
//      //return server.requestAuthentication(BASIC_AUTH, www_realm, authFailResponse);
//      //Digest Auth Method with realm="Login Required" and empty Failure Response
//      //return server.requestAuthentication(DIGEST_AUTH);
//      //Digest Auth Method with Custom realm and empty Failure Response
//      //return server.requestAuthentication(DIGEST_AUTH, www_realm);
//      //Digest Auth Method with Custom realm and Failure Response
//    {
//      return WebServer.requestAuthentication(DIGEST_AUTH, www_realm, authFailResponse);
//    }
//    //WebServer.send(200, "text/plain", "Login OK");
//    //response();
//    Serial.println("SUCCESS");
//    yield();
//  });
  WebServer.on("/update", HTTP_POST, []() {
    WebServer.sendHeader("Connection", "close");
    WebServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = WebServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });
  WebServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.println("Web Server started");

  // Print the IP address...
  Serial.print("You can connect to the ESP8266 locally at this URL: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
  Serial.printf("Ready! Open http://%s.local in your browser\n", DNShost);
  String Start[1] = {"Start"};
  
  //log inital startup
  data_logger("Device_Start", Start, "N/A", "N/A", 0, 0, 0);
  Gsender *gsender = Gsender::Instance();    // Class instance pointer
  String subject = "Webpage-Reset";
  if (gsender->Subject(subject)->Send(maintenance_email, Error_reason)) {
    Serial.println("Message send.");
  } else {
    Serial.print("Error sending message: ");
    Serial.println(gsender->getError());
  }

  //Get the current hour and minute from the webpage
  set_time = "<div align=\"center\"> <form method=\"get\"> <strong>Enter the current <span style=\"text-decoration: underline;\"> military time </span> before any other action</strong> <p>Hour: <input name=\"time_hour\" type=\"text\" maxlength=\"2\" size=\"2\" /> Minute: <input name=\"time_minute\" type=\"text\" maxlength=\"2\" size=\"2\" /></p> <input type=\"submit\" value=\"Set\" onclick=\"setTimeout(function(){window.location.reload(true)},1000);\"> </form> </div>";
  while ((time_hour == NULL) || (time_minute == NULL))
  {
    WebServer.handleClient();
    MDNS.update();
  }
  set_time = "";
  response();
  WebServer.handleClient();
  MDNS.update();
  String AM_PM = " AM";
  int temp_hour = time_hour;
  //sets the time
  setTime(time_hour - 1, time_minute, 00, 19, 04, 2020);
  if (time_hour > 12)
  {
    AM_PM = " PM";
    temp_hour = time_hour - 12;
  }
  System_Time = "Pump System Time: " + String(temp_hour) + ":" + String(time_minute) + AM_PM;
}

void loop() {
  static float PSI_high;
  float PSI_drop;
  static int shut_off;
  int time_up = 0;
  int water_sensor;
  static int begin_timer = 0;
  static int pi = 0;

  int current_second = second();
  int current_hour = hour();
  int seconds_elapsed = abs(current_second - seconds_start);

  WebServer.handleClient(); //Updates the cached website on the browser
  MDNS.update();
  //Water sensor value
  water_sensor = water_detection();

  //Calculates the drop in PSI
  compute_PSI_average();
  if (PSI_average > PSI_high)
  {
    PSI_high = PSI_average;
  }
  PSI_drop = PSI_high - PSI_average;
  if (seconds_elapsed == (60 * minute_between_PSI))
  {
    PSI_average_past = PSI_average;
    seconds_start = second();
  }
  //Error conditions to turn the pump off immediately
  //Also displays error on website
  if (((pump_status[current_hour] == "ON" || pump_status[current_hour] == "ON: 30 MIN." || pump_status[current_hour] == "Manual:ON") && (PSI_average < 5.00 ||  PSI_drop >= burst_pipe)) || water_sensor)
  {
    shut_off = timer(shut_off, begin_timer);
    begin_timer = 1; //don't start timer
    //    Serial.println(shut_off);
    if ((shut_off >= off_minutes) || water_sensor)
    {
      begin_timer = 0;
      Serial.println("PSI_drop: ");
      Serial.print(PSI_drop);
      digitalWrite(relay, LOW);
      Error = "MAINTENANCE REQUIRED ON PUMP";
      if (water_sensor)
      {
        Error_reason = "*Water-On-Board*";
        water_sensor = 0;
        digitalWrite(Water_right, HIGH);
      }
      else if (PSI_average < 5)
      {
        Error_reason = "*No-Water-Running-Through-Pump*";
      }
      else
      {
        Error_reason = "*Large-Pressure-Drop-While-Running*";
      }

      //log data
      //      pump_status[current_hour] = "OFF:MAINTENANCE";
      //      data_logger(pump_mode, pump_status, Error_reason, maintenance_response, PSI_average_past, PSI_average, current_hour);

      Error_response = "<form method =\"get\"> <p>Has the maintenance been serviced?</p> <input name=\"maintenance\" type=\"radio\" value=\"yes\" /> Yes <input name=\"maintenance\" type=\"radio\" value=\"no\" /> No <p><input type=\"submit\" value=\"Submit Response\" /></p> </form>";

      //send maintenance email
      Gsender *gsender = Gsender::Instance();    // Getting pointer to class instance
      String subject = Error;
      if (gsender->Subject(subject)->Send(maintenance_email, Error_reason)) {
        Serial.println("Message send.");
      } else {
        Serial.print("Error sending message: ");
        Serial.println(gsender->getError());
      }
      //Wait for "yes" response from webpage and 30 seconds before turning on the pump again
      int seconds_start = second();
      int prev_hour = 0;
      while (((maintenance_response != "yes") || (time_up != 1)))
      {
        current_hour = hour();
        pump_status[current_hour] = "OFF:MAINTENANCE";
        shut_off = 0;
        WebServer.handleClient();
        MDNS.update();
        int seconds_update = second();
        int seconds_elapsed = abs(seconds_update - seconds_start);
        if (prev_hour != current_hour)
        {
          data_logger(pump_mode, pump_status, Error_reason, maintenance_response, PSI_average_past, PSI_average, current_hour);
        }
        if (seconds_elapsed == pump_cycle)
        {
          time_up = 1;
        }
        prev_hour = current_hour;
      }

      //log data
      data_logger(pump_mode, pump_status, Error_reason, maintenance_response, PSI_average_past, PSI_average, current_hour);

      maintenance_response = "";
      time_up = 0;
    }
    //reset PSI_high after burst pipe has been fixed
    if (PSI_drop >= burst_pipe)
    {
      //log data
      data_logger(pump_mode, pump_status, Error_reason, maintenance_response, PSI_average_past, PSI_average, current_hour);

      PSI_high = 0;
      Serial.println("PSI_high: ");
      Serial.print(PSI_high);
    }
  }

  int pump_condition = pump_status_FSM(pump_override, pump_mode, pump_hours[current_hour], current_hour);
  if ((pump_condition > 2) && (pump_mode == "Automatic"))
  {
    Pump_manual = "";
    pump_override == "";
  }
  switch (pump_condition)
  {
    case 0:
      break;
    case 1:
      digitalWrite(relay, HIGH);
      pump_status[current_hour] = "Manual:ON";
      Pump_manual = "<p><span style=\"color: #00a907;\"><strong>PUMP MANUAL OVERRIDE: ON<br /></strong></span></p>";
      break;
    case 2:
      digitalWrite(relay, LOW);
      pump_status[current_hour] = "Manual:OFF";
      Pump_manual = "<p><span style=\"color: #ff0000;\"><strong>PUMP MANUAL OVERRIDE: OFF</span><br /></strong></span></p>";
      break;
    case 3:
      digitalWrite(relay, HIGH);
      pump_status[current_hour] = "ON";
      break;
    case 4:
      digitalWrite(relay, HIGH);
      pump_status[current_hour] = "ON: 30 MIN.";
      break;
    case 5:
      digitalWrite(relay, LOW);
      pump_status[current_hour] = "OFF";
      break;
    default:
      break;
  }
  if (pump_condition > 0)
  {
    data_logger(pump_mode, pump_status, Error_reason, maintenance_response, PSI_average_past, PSI_average, current_hour);
  }
  delay(100); //Can't continiously pull the analog pin
}
