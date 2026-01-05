/*
 * Arduino Oscillometric Blood Pressure Monitor 
 *
 * - Inflates cuff using pump (via transistor) until pressure threshold reached
 * - Waits 1 second
 * - Samples pressure (DC) + oscillometric (AC) signals at 30 Hz for 30 seconds
 * - Computes peak-to-peak amplitude per 1-second window
 * - MAP = pressure at max oscillation amplitude
 * - SBP ~ 0.55 * max amplitude (search backward)
 * - DBP ~ 0.75 * max amplitude (search forward)
 * - HR estimated from detected peaks timing
 *
 */

#include <Arduino.h>

/* =======================
   Pins (matches your code)
   ======================= */
#define buttonPin 8
#define transistorPin 12
#define INAPin A0

const int PRESSURE_PIN = A0; // Pressure (DC) signal input
const int OSCILLO_PIN  = A1; // Oscillometric (AC) signal input

/* =======================
   Constants
   ======================= */
const int SAMPLE_RATE = 30;              // 30 Hz sampling
const int WINDOW_SIZE = 30;              // 1 second window (30 samples)
const int TOTAL_SECONDS = 30;            // 30 seconds total
const float VOLTS_TO_MMHG = 50.0;        // (200 mmHg / 4 V = 50 mmHg/V)
const unsigned long INITIAL_DELAY = 1000;

const unsigned long SAMPLE_INTERVAL = 1000 / SAMPLE_RATE; // ~33 ms

/* =======================
   Pump control flags
   ======================= */
bool pumpOn = false;
bool thresholdReached = false;

/* =======================
   Data structure
   ======================= */
struct BPReading {
  float amplitude;   // peak-to-peak oscillation amplitude (V)
  float pressure;    // avg pressure at this window (mmHg)
};

BPReading readings[TOTAL_SECONDS];

/* =======================
   Buffers + window tracking
   ======================= */
float oscillometricBuffer[WINDOW_SIZE];
float pressureBuffer[WINDOW_SIZE];
int bufferIndex = 0;
int secondCounter = 0;

float maxVal = -999;
float minVal = 999;
float maxPressure = 0;
float minPressure = 0;

/* =======================
   MAP tracking
   ======================= */
float maxAmplitude = 0;
int maxAmplitudeIndex = 0;

/* =======================
   State machine
   ======================= */
enum State {
  WAITING_FOR_SIGNAL,
  DELAY_AFTER_THRESHOLD,
  PROCESSING_SIGNAL,
  COMPLETE
};
State currentState = WAITING_FOR_SIGNAL;

unsigned long stateTimer = 0;
unsigned long sampleTimer = 0;

/* =======================
   Peak detection for HR
   ======================= */
enum OscillationState {
  RISING_STATE,
  FALLING_STATE
};
OscillationState oscillationState = FALLING_STATE;

float prevOscillometric = 0;
unsigned int numPeaks = 0;
unsigned int pulseStart = 0;
unsigned int pulseEnd = 0;

/* =======================
   Function prototypes
   ======================= */
void calculateBloodPressure();
float findPressureAtAmplitude(float targetAmplitude, bool searchBackward);

void setup() {
  pinMode(buttonPin, INPUT);         // keep as-is (your wiring)
  pinMode(transistorPin, OUTPUT);

  Serial.begin(115200);
  Serial.println("Blood Pressure Monitor Starting...");
  Serial.println("Waiting for pressure threshold...");
}

void loop() {
  /* -----------------------
     Pump control (inflation)
     ----------------------- */
  if (digitalRead(buttonPin)) {
    pumpOn = true;
  }

  if (pumpOn) {
    digitalWrite(transistorPin, HIGH); // pump ON
    if (analogRead(INAPin) > 635) {    // ~3.0 V threshold (approx 150 mmHg)
      pumpOn = false;
      thresholdReached = true;
    }
  } else {
    digitalWrite(transistorPin, LOW);  // pump OFF
  }

  /* -----------------------
     Read signals
     ----------------------- */
  float pressureValue = analogRead(PRESSURE_PIN) * (5.0 / 1023.0);
  float oscillometricValue = analogRead(OSCILLO_PIN) * (5.0 / 1023.0);
  float pressure_mmHg = pressureValue * VOLTS_TO_MMHG;

  /* -----------------------
     State machine
     ----------------------- */
  switch (currentState) {

    case WAITING_FOR_SIGNAL:
      if (thresholdReached) {
        Serial.println("Threshold reached! Starting 1-second delay...");
        currentState = DELAY_AFTER_THRESHOLD;
        stateTimer = millis();
      }
      break;

    case DELAY_AFTER_THRESHOLD:
      if (millis() - stateTimer >= INITIAL_DELAY) {
        Serial.println("Delay complete! Starting processing...");
        currentState = PROCESSING_SIGNAL;
        sampleTimer = millis();
      }
      break;

    case PROCESSING_SIGNAL:
      if (millis() - sampleTimer >= SAMPLE_INTERVAL) {
        /* Peak counting for HR (simple slope-change method) */
        switch (oscillationState) {
          case RISING_STATE:
            if (prevOscillometric > oscillometricValue) {
              oscillationState = FALLING_STATE;
              numPeaks++;
              if (numPeaks == 15) pulseStart = millis();
              if (numPeaks == 16) pulseEnd = millis();
            }
            prevOscillometric = oscillometricValue;
            break;

          case FALLING_STATE:
            if (prevOscillometric < oscillometricValue) {
              oscillationState = RISING_STATE;
              if (numPeaks == 15) pulseEnd = millis();
            }
            prevOscillometric = oscillometricValue;
            break;
        }

        sampleTimer = millis();

        /* Debug print */
        Serial.print("Sample: ");
        Serial.print(secondCounter * WINDOW_SIZE + bufferIndex);
        Serial.print(" - Pressure: ");
        Serial.print(pressureValue);
        Serial.print("V (");
        Serial.print(pressure_mmHg);
        Serial.print(" mmHg), Oscillometric: ");
        Serial.print(oscillometricValue);
        Serial.print(", Peaks: ");
        Serial.println(numPeaks);

        /* Store in buffers */
        pressureBuffer[bufferIndex] = pressure_mmHg;
        oscillometricBuffer[bufferIndex] = oscillometricValue;

        /* Window max/min tracking */
        if (oscillometricValue > maxVal) {
          maxVal = oscillometricValue;
          maxPressure = pressure_mmHg;
        }
        if (oscillometricValue < minVal) {
          minVal = oscillometricValue;
          minPressure = pressure_mmHg;
        }

        bufferIndex++;

        /* Process window (1 second) */
        if (bufferIndex >= WINDOW_SIZE) {
          float peakToPeak = maxVal - minVal;
          float avgPressure = (maxPressure + minPressure) / 2.0;

          readings[secondCounter].amplitude = peakToPeak;
          readings[secondCounter].pressure = avgPressure;

          Serial.print("Second ");
          Serial.print(secondCounter + 1);
          Serial.print(" - Peak-to-Peak: ");
          Serial.print(peakToPeak);
          Serial.print("V, Avg Pressure: ");
          Serial.print(avgPressure);
          Serial.println(" mmHg");

          if (peakToPeak > maxAmplitude && secondCounter > 0) {
            maxAmplitude = peakToPeak;
            maxAmplitudeIndex = secondCounter;
          }

          /* Reset window */
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

/* =======================
   Blood pressure results
   ======================= */
void calculateBloodPressure() {
  Serial.println("\n--- PROCESSED DATA ---");
  Serial.println("Index\tAmplitude\tPressure (mmHg)");

  for (int i = 0; i < TOTAL_SECONDS; i++) {
    Serial.print(i);
    Serial.print("\t");
    Serial.print(readings[i].amplitude, 4);
    Serial.print("\t\t");
    Serial.println(readings[i].pressure, 4);
  }

  float map = readings[maxAmplitudeIndex].pressure;

  float systolicThreshold = 0.55 * maxAmplitude;
  float systolic = findPressureAtAmplitude(systolicThreshold, true);

  float diastolicThreshold = 0.75 * maxAmplitude;
  float diastolic = findPressureAtAmplitude(diastolicThreshold, false);

  Serial.println("\n--- BLOOD PRESSURE RESULTS ---");
  Serial.print("Maximum Amplitude: ");
  Serial.print(maxAmplitude, 4);
  Serial.print("V at index ");
  Serial.println(maxAmplitudeIndex);

  Serial.print("Systolic Threshold (0.55 * max): ");
  Serial.println(systolicThreshold, 4);

  Serial.print("Diastolic Threshold (0.75 * max): ");
  Serial.println(diastolicThreshold, 4);

  Serial.print("Systolic: ");
  Serial.print(systolic, 1);
  Serial.println(" mmHg");

  Serial.print("Diastolic: ");
  Serial.print(diastolic, 1);
  Serial.println(" mmHg");

  Serial.print("MAP: ");
  Serial.print(map, 1);
  Serial.println(" mmHg");

  /* HR estimate: uses time between peak #15 and #16 */
  float dt_s = (pulseEnd - pulseStart) * 0.001;
  if (dt_s > 0.0) {
    Serial.print("Heart Rate: ");
    Serial.print(60.0 / dt_s, 1);
    Serial.println(" BPM");
  } else {
    Serial.println("Heart Rate: N/A (insufficient peak timing)");
  }
}

/* =======================
   Helper search function
   ======================= */
float findPressureAtAmplitude(float targetAmplitude, bool searchBackward) {
  float closestDifference = 999;
  int closestIndex = maxAmplitudeIndex;

  if (searchBackward) {
    for (int i = maxAmplitudeIndex; i >= 0; i--) {
      float difference = abs(readings[i].amplitude - targetAmplitude);
      if (difference < closestDifference) {
        closestDifference = difference;
        closestIndex = i;
      }
    }
  } else {
    for (int i = maxAmplitudeIndex; i < TOTAL_SECONDS; i++) {
      float difference = abs(readings[i].amplitude - targetAmplitude);
      if (difference < closestDifference) {
        closestDifference = difference;
        closestIndex = i;
      }
    }
  }

  Serial.print("Target amplitude: ");
  Serial.print(targetAmplitude, 4);
  Serial.print(" found at index ");
  Serial.print(closestIndex);
  Serial.print(" with actual amplitude ");
  Serial.print(readings[closestIndex].amplitude, 4);
  Serial.print(", pressure ");
  Serial.print(readings[closestIndex].pressure, 1);
  Serial.println(" mmHg");

  return readings[closestIndex].pressure;
}
