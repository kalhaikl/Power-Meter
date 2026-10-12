#define SENSOR_PIN 33
const int samples = 1000;
float calibration_factor = 1; // Set after manual calibration

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // 12-bit ADC (0-4095)
}

void loop() {
  float maxValue = 0;
  float minValue = 4095;
  for (int i = 0; i < samples; i++) {
    int val = analogRead(SENSOR_PIN);
    if (val > maxValue) maxValue = val;
    if (val < minValue) minValue = val;
  }

  float peakToPeak = maxValue - minValue;
  float voltage = (peakToPeak / 2.0) * (3.3 / 4095.0);
  float acRMS = voltage * 0.707 * calibration_factor;

  Serial.print("AC RMS Voltage: ");
  Serial.print(acRMS);
  Serial.println(" V");

  delay(1000);
}