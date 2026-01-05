/*
 * Arduino Oscillometric Blood Pressure Monitor
 *
 * This program inflates a blood pressure cuff, then samples
 * pressure and oscillometric signals during deflation to estimate:
 *  - Systolic BP
 *  - Diastolic BP
 *  - Mean Arterial Pressure (MAP)
 *  - Heart Rate
 *
 * This is an educational prototype, not a medical device.
 */

#include <Arduino.h>

/* -------- Pin definitions -------- */
#define buttonPin 8        // Start button
#define transistorPin 12   // Pump control (via transistor)
#define INAPin A0          // Pressure signal used for pump cutoff

const int PRESSURE_PIN = A0;  // DC pressure signal
const int OSCILLO_PIN  = A1;  // AC oscillometric signal

/* -------- Constants -------- */
const int SAMPLE_RATE = 30;          // 30 Hz sampling
const int WINDOW_SIZE = 30;          // 1 second window
const int TOTAL_SECONDS = 30;        // Total measurement time
const float VOLTS_TO_MMHG = 50.0;    // Voltage to pressure conversion
const unsigned long INITIAL_DELAY = 1000;

const unsigned long SAMPLE_INTERVAL = 1000 / SAMPLE_RATE;

/* -------- Pump control -------- */
bool pumpOn = false;
bool thresholdReached = false;

/* -------- Data storage -------- */
struct BPReading {
  float amplitude;   // Peak-to-peak oscillation amplitude
  float pressure;    // Average pressure during this window
};

BPReading readings[TOTAL_SECONDS];

/* -------- Buffers -------- */
float oscillometricBuffer[WINDOW_SIZE];
float pressureBuffer[WINDOW_SIZE];

int bufferIndex = 0;
int secondCounter = 0;

float maxVal = -999;
float minVal = 999;
float maxPressure = 0;
float minPressure = 0;

/* -------- MAP tracking -------- */
float maxAmplitude = 0;
int maxAmplitudeIndex = 0;

/* -------- State machine -------- */
enum State {
  WAITING_FOR_SIGNAL,
  DELAY_AFTER_THRESHOLD,
  PROCESSING_SIGNAL,
  COMPLETE
};

State currentState = WAITING_FOR_SIGNAL;

unsigned long stateTimer = 0;
unsigned long sampleTimer = 0;

/* -------- Heart rate detection -------- */
enum OscillationState {
  RISING_STATE,
  FALLING_STATE
};

OscillationState oscillationState = FALLING_STATE;

float prevOscillometric = 0;
unsigned int numPeaks = 0;
unsigned int pulseStart = 0;
unsigned int pulseEnd = 0;

/* -------- Function prototypes -------- */
void calculateBloodPressure();
float findPressureAtAmplitude(float targetAmplitude, bool searchBackward);

void setup() {
  pinMode(buttonPin, INPUT);
  pinMode(transistorPin, OUTPUT);

  Serial.begin(115200);
  Serial.println("Blood Pressure Monitor Starting...");
  Serial.println("Waiting for pressure threshold...");
}

void loop() {

  /* ---- Pump control ---- */
  if (digitalRead(buttonPin)) {
    pumpOn = true;
  }

  if (pumpOn) {
    digitalWrite(transistorPin, HIGH);   // Turn pump on
    if (analogRead(INAPin) > 635) {      // ~3V cutoff
      pumpOn = false;
      thresholdReached = true;
    }
  } else {
    digitalWrite(transistorPin, LOW);    // Turn pump off
  }

  /* ---- Read signals ---- */
  float pressureValue =
    analogRead(PRESSURE_PIN) * (5.0 / 1023.0);
  float oscillometricValue =
    analogRead(OSCILLO_PIN) * (5.0 / 1023.0);

  float pressure_mmHg = pressureValue * VOLTS_TO_MMHG;

  /* ---- State machine ---- */
  switch (currentState) {

    case WAITING_FOR_SIGNAL:
      if (thresholdReached) {
        Serial.println("Threshold reached, waiting...");
        currentState = DELAY_AFTER_THRESHOLD;
        stateTimer = millis();
      }
      break;

    case DELAY_AFTER_THRESHOLD:
      if (millis() - stateTimer >= INITIAL_DELAY) {
        Serial.println("Starting data collection...");
        currentState = PROCESSING_SIGNAL;
        sampleTimer = millis();
      }
      break;

    case PROCESSING_SIGNAL:
      if (millis() - sampleTimer >= SAMPLE_INTERVAL) {

        /* ---- Simple peak detection for heart rate ---- */
        if (oscillationState == RISING_STATE) {
          if (prevOscillometric > oscillometricValue) {
            oscillationState = FALLING_STATE;
            numPeaks++;
            if (numPeaks == 15) pulseStart = millis();
            if (numPeaks == 16) pulseEnd = millis();
          }
        } else {
          if (prevOscillometric < oscillometricValue) {
            oscillationState = RISING_STATE;
          }
        }

        prevOscillometric = oscillometricValue;
        sampleTimer = millis();

        /* ---- Store samples ---- */
        pressureBuffer[bufferIndex] = pressure_mmHg;
        oscillometricBuffer[bufferIndex] = oscillometricValue;

        /* ---- Track min/max for this second ---- */
        if (oscillometricValue > maxVal) {
          maxVal = oscillometricValue;
          maxPressure = pressure_mmHg;
        }
        if (oscillometricValue < minVal) {
          minVal = oscillometricValue;
          minPressure = pressure_mmHg;
        }

        bufferIndex++;

        /* ---- Process 1-second window ---- */
        if (bufferIndex >= WINDOW_SIZE) {
          float peakToPeak = maxVal - minVal;
          float avgPressure = (maxPressure + minPressure) / 2.0;

          readings[secondCounter].amplitude = peakToPeak;
          readings[secondCounter].pressure = avgPressure;

          if (peakToPeak > maxAmplitude && secondCounter > 0) {
            maxAmplitude = peakToPeak;
            maxAmplitudeIndex = secondCounter;
          }

          bufferIndex = 0;
          maxVal = -999;
          minVal = 999;
          secondCounter++;

          if (secondCounter >= TOTAL_SECONDS) {
            calculateBloodPressure();
            currentState = COMPLETE;
          }
        }
      }
      break;

    case COMPLETE:
      delay(3000);
      break;
  }
}

/* -------- Final BP calculations -------- */
void calculateBloodPressure() {
  float map = readings[maxAmplitudeIndex].pressure;

  float systolic =
    findPressureAtAmplitude(0.55 * maxAmplitude, true);

  float diastolic =
    findPressureAtAmplitude(0.75 * maxAmplitude, false);

  Serial.println("\n--- RESULTS ---");
  Serial.print("Systolic: ");  Serial.print(systolic);  Serial.println(" mmHg");
  Serial.print("Diastolic: "); Serial.print(diastolic); Serial.println(" mmHg");
  Serial.print("MAP: ");       Serial.print(map);       Serial.println(" mmHg");

  float dt = (pulseEnd - pulseStart) * 0.001;
  if (dt > 0) {
    Serial.print("Heart Rate: ");
    Serial.print(60.0 / dt);
    Serial.println(" BPM");
  }
}

/* -------- Helper function -------- */
float findPressureAtAmplitude(float targetAmplitude, bool searchBackward) {
  float bestDiff = 999;
  int bestIndex = maxAmplitudeIndex;

  if (searchBackward) {
    for (int i = maxAmplitudeIndex; i >= 0; i--) {
      float diff = abs(readings[i].amplitude - targetAmplitude);
      if (diff < bestDiff) {
        bestDiff = diff;
        bestIndex = i;
      }
    }
  } else {
    for (int i = maxAmplitudeIndex; i < TOTAL_SECONDS; i++) {
      float diff = abs(readings[i].amplitude - targetAmplitude);
      if (diff < bestDiff) {
        bestDiff = diff;
        bestIndex = i;
      }
    }
  }

  return readings[bestIndex].pressure;
}
