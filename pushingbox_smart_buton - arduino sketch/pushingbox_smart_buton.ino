/*
 *  Title: (Pushingbox) Smart button with ultra low standby power
 *  Version: 0.9
 *  Date: 27. Apr, 2016
 *  
 *  Description:
 *   -- Sets the pin 'power_pin' to HIGH (this keeps the ldo enabled)
 *   - Measures the battery voltage 
 *    -> If the voltage is lower then 'undervoltage' volts, signal a recharge (see Blink Codes) and shut down.
 *    -> If the voltage is higher, connects to the given access point and proceeds...
 *   - Collects some more data (wifi signal strength, ip address, time to get the ip)
 *   - Launches the given Pushingbox scenario and sends the colleted data(variables) with it
 *   -- Sets the 'power_pin' pin to LOW (this causes the ldo to shut down)
 *   
 *  Configuration:
 *   Most important (MUST be configured): Wifi(ssid, password) and Pushingbox(devid) config
 *   May needs to be configured: vref, undervoltage
 *   The other variables can be left unchanged
 *
 *  Used pushingbox variables:
 *   $rssi$     = Wifi signal strength in dBm (example: "-59")
 *   $ip$       = The ip address (example: "192.168.0.45")
 *   $ip_time$  = The time it took to get the ip in seconds ("3.4")
 *   $vbat$     = Measured battery voltage ("3.86")
 *
 *  Status LED Blink Codes:
 *   2x green = GET reqeust successfully sent (scenario launched)
 *   2x red   = GET request failed
 *   3x red   = Could not connect to the wifi (wrong password or wifi network not in range)
 *   1x red   = Battery empty. Time to reacharge.
 *
 *  TODO:
 *   Pushingbox request error handling - #131
 *   Fix ugly ip_time formating - #166
 */


// Wifi config
const char* ssid     = "xxxxx";
const char* password = "xxxxx";

// Pushingbox config
// Device id of the pushingbox scenario to launch
const char* devid = "xxxxx";


// Power/Shutdown pin
const int power_pin = 2;

// Status leds
const int ok_led = 12;
const int error_led = 14;
const int act_led = 13;

// Activity led blink speed in seconds
const float blink_speed = 0.4;

// Voltage divider config
const int r1 = 4700;
const int r2 = 1000;
// Acutal adc reference voltage (turns out it isn't exactly 1.0V)
const float vref = 0.975;

// Undervoltage detection (can be set to 0 if not needed)
// If the measured voltage is lower then 'undervoltage' volts, signal a recharge and shut down
const float undervoltage = 3.0;

// Seconds to wait for a ip before giving up and shutting down
const int wifi_timeout = 15;


#include <ESP8266WiFi.h>
#include <Ticker.h>

Ticker act_led_flipper;

int rssi;
unsigned long ip_time;
float vcc;
IPAddress ip_address;

void connect_to_wifi()
{
  // connect to the given ssid
  WiFi.begin(ssid, password);

  // wait for a ip from DHCP
  unsigned long timeout = millis();
  while (WiFi.status() != WL_CONNECTED) {
    // but not longer then "wifi_timout" seconds
    if (millis() - timeout > wifi_timeout * 1000) {
      Serial.print("\n-> Could not connect to ap: ");
      Serial.println(ssid);
      // shut down
      shutdown(1, 5);
    }
    Serial.print(".");
    delay(500);
  }
  ip_time = millis();

  // get the wifi signal strength:
  rssi = WiFi.RSSI();
  // save the ip address
  ip_address = WiFi.localIP();

  Serial.println("");
  Serial.println("-> Connected.");
  Serial.print(" IP: ");
  Serial.print(ip_address);
  Serial.printf(" (%i.%is)\n", millis() / 1000, millis() % 1000);
  Serial.print(" RSSI:");
  Serial.print(rssi);
  Serial.println("dBm\n");
  Serial.println("");
}

void send_pushingbox_request()
{
  // combine all variables into a single string
  String request_uri = "";

  // add the signal strength
  request_uri += "&rssi=";
  request_uri += rssi;

  // add the ip address
  String ip = String(ip_address[0]) + "." + String(ip_address[1]) + "." + String(ip_address[2]) + "." + String(ip_address[3]);
  request_uri += "&ip=";
  request_uri += ip;

  // add the time it took to get the ip
  // TODO: fix ugly formating (example: in:3450; out=3.4)
  String formated_ip_time;
  formated_ip_time = ip_time / 1000;
  formated_ip_time += "\.";
  String temp;
  temp = String(ip_time % 1000);
  temp = temp.substring(0, 1);
  formated_ip_time += temp;
  request_uri += "&ip_time=";
  request_uri += formated_ip_time;

  // add the battery voltage
  request_uri += "&vcc=";
  request_uri += vcc;

  // print the final query string
  Serial.print("Request: ");
  Serial.println(request_uri);

  // create the http connection
  WiFiClient client;
  if (!client.connect("api.pushingbox.com", 80)) {
    Serial.println("-> Connection failed");
    shutdown(1, 3);
    return;
  }
  Serial.println("-> Done.");

  // if connected, send the GET request
  Serial.println("Sending request");
  client.print(String("GET /pushingbox?devid=") + devid + request_uri + " HTTP/1.1\r\n" +
               "Host: api.pushingbox.com\r\n" +
               "Connection: close\r\n\r\n");

  // print all lines from the response - TODO
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  Serial.println("-> Done. Connection closed.\n");
}

void shutdown(boolean error, int blink_count)
{
  // stop the activity led
  act_led_flipper.detach();
  digitalWrite(act_led, LOW);

  // either blink the 'error' or 'ok' led, depending on if something went wrong
  int led_to_blink;
  if (error == 0) {
    led_to_blink = ok_led;
  }
  else if (error == 1) {
    led_to_blink = error_led;
  }
  // blink the led 'blink_count' times
  for (int i = 0; i < blink_count; i++)
  {
    digitalWrite(led_to_blink, HIGH);
    delay(200);
    digitalWrite(led_to_blink, LOW);
    if (i < blink_count - 1)
      delay(200);
  }

  // shutdown the voltage regualtor by "releasing" the ldo enable pin
  Serial.printf(" Time elapsed: %i.%i Seconds\n", millis() / 1000, millis() % 1000);
  Serial.println(" Shutting down...");
  digitalWrite(power_pin, LOW);
  // also activate regular deepsleep so the sketch can be used without the enable pin shutdown circuit
  //ESP.deepSleep(0, WAKE_RF_DEFAULT);
  delay(500);
}

void toogle_act_led()
{
  digitalWrite(act_led, !digitalRead(act_led));
}

float get_battery_voltage()
{
  // read raw adc value (0-1023)
  int adc_value = analogRead(0);

  // calc input voltage (0-1V)
  float battery_voltage = vref / 1024.0 * adc_value;

  // calc battery voltage (0 - ~5.5V)
  battery_voltage = battery_voltage * (r1 + r2) / r2;

  return  battery_voltage;
}

void setup()
{
  // Hold the ldo enable pin HIGH
  pinMode(power_pin, OUTPUT);
  digitalWrite(power_pin, HIGH);

  // Init led pins
  pinMode(ok_led, OUTPUT);
  pinMode(error_led , OUTPUT);
  pinMode(act_led, OUTPUT);

  // Start blinking the activity led
  act_led_flipper.attach(blink_speed, toogle_act_led);

  // Setup the serial port
  Serial.begin(115200);
  Serial.println("");

  // Get the battery voltage
  vcc = get_battery_voltage();
  // Check for undervoltage
  if (vcc < undervoltage)
  {
    Serial.println(" Battery voltage ciritical!");
    Serial.println(" Recharge!");
    Serial.print(" Voltage: ");
    Serial.print(vcc);
    Serial.println("V\n");

    WiFi.disconnect();
    shutdown(1, 1);
  }
  
  // Connect to the wifi network
  Serial.print("Connecting to access point: ");
  Serial.println(ssid);
  connect_to_wifi();
  
  // Send the Pushingbox request
  Serial.print("Connecting to ");
  Serial.println("api.pushingbox.com");
  send_pushingbox_request();

  // Shutdown
  shutdown(0, 2);
}

void loop()
{
  // nothing in here
}

