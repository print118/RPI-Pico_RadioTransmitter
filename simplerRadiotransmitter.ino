// Radio transmitter for the Raspberry Pi Pico

#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include <math.h>

int led = LED_BUILTIN;

uint vco_freq;
uint post_div1;
uint post_div2;

int returnVal;

// Maximum clock frequency that the RPI Pico is stable at.
// Larger values allow for higher radio frequencies, increase as needed.
uint32_t maxStableClockFrequencykHz = 300000;
uint32_t minStableClockFrequencykHz = 50000;

// How many kHz is the test clock freqyency iterated every time in for loop. Lower values mean higher accuracy, but longer processing times. Bigger values mean less accuracy, but much faster processing times.
// Default: 5
int clockFrequencyIteration = 1;

unsigned long startTime;
unsigned long endTime;
unsigned long startTimeReceive;
unsigned long endTimeReceive;

char endMark = '\n';

bool oneTimeArrayCheck = true;
bool userInput = false;
bool transmitting = false;
bool harmonicTransmit = false;
bool belowMinimumTransmit = false;

int PICO_PLL_VCO_MIN_FREQ_MHZ = 750;
int PICO_PLL_VCO_MAX_FREQ_MHZ = 1600;

// Array of working clock frequencies and their respective dividers to reach as close as possible to the frequency to transmit at.
// First field    = Clock frequency (kHz)
// Second field   = VCO frequency (Hz)
// Third field    = Post divider 1. (1-7)
// Fourth field   = Post divider 2. (1-7)
// Fifth field    = Integer 2.. clock divider
// Sixth field    = Decimal integer 0-255 clock divider
// Seventh field  = Achieved frequency (kHz)
// Eigth field    = Distance from resulting frequency to frequency to transmit at (kHz)
uint32_t workingFrequencies[1][8];  // = {0}?

void __not_in_flash_func(calculateClockDividers)(uint32_t testFrequency, uint32_t wantedFreq, int multiplier) {
  uint crystal_freq_khz = clock_get_hz(clk_ref) / 1000;
  for (uint fbdiv = 320; fbdiv >= 16; fbdiv--) {
    uint vco = fbdiv * crystal_freq_khz;
    if (vco < PICO_PLL_VCO_MIN_FREQ_MHZ * 1000 || vco > PICO_PLL_VCO_MAX_FREQ_MHZ * 1000) continue;
    for (uint postdiv1 = 7; postdiv1 >= 1; postdiv1--) {
      for (uint postdiv2 = postdiv1; postdiv2 >= 1; postdiv2--) {
        uint out = vco / (postdiv1 * postdiv2);
        if (out == testFrequency && !(vco % (postdiv1 * postdiv2))) {
          for (int i = 2; i < 5; i++) {
            int divider = abs(int(((out * 256) / wantedFreq) - (256 * i)));
            if (divider >= 256) {
              divider = divider - 256;
              i += 1;
            }
            uint32_t dividedFreq = (256 * out) / (256 * i + divider);
            if (abs(int(wantedFreq * multiplier - dividedFreq * multiplier)) < workingFrequencies[0][7]) {
              vco_freq = vco * 1000;
              post_div1 = postdiv1;
              post_div2 = postdiv2;
              workingFrequencies[0][0] = out;
              workingFrequencies[0][1] = vco_freq;
              workingFrequencies[0][2] = post_div1;
              workingFrequencies[0][3] = post_div2;
              workingFrequencies[0][4] = i;
              workingFrequencies[0][5] = divider;
              workingFrequencies[0][6] = dividedFreq;
              workingFrequencies[0][7] = abs(int(wantedFreq * multiplier - dividedFreq * multiplier));
            }
          }
          return;
        }
      }
    }
  }
  return;
}


void __not_in_flash_func(calculatePrimaryClocks)(uint32_t testFrequency, uint32_t wantedFreq) {
  uint crystal_freq_khz = clock_get_hz(clk_ref) / 1000;
  for (uint fbdiv = 320; fbdiv >= 16; fbdiv--) {
    uint vco = fbdiv * crystal_freq_khz;
    if (vco < PICO_PLL_VCO_MIN_FREQ_MHZ * 1000 || vco > PICO_PLL_VCO_MAX_FREQ_MHZ * 1000) continue;
    for (uint postdiv1 = 7; postdiv1 >= 1; postdiv1--) {
      for (uint postdiv2 = postdiv1; postdiv2 >= 1; postdiv2--) {
        uint out = vco / (postdiv1 * postdiv2);
        if (out == testFrequency && !(vco % (postdiv1 * postdiv2))) {
          vco_freq = vco * 1000;
          post_div1 = postdiv1;
          post_div2 = postdiv2;
          if (oneTimeArrayCheck || abs(int(wantedFreq - out)) < workingFrequencies[0][7]) {
            /*Serial.println("a");
            Serial.println(wantedFreq);
            Serial.println(out);
            Serial.println(abs(int(wantedFreq - out)));*/
            workingFrequencies[0][0] = out;
            workingFrequencies[0][1] = vco_freq;
            workingFrequencies[0][2] = post_div1;
            workingFrequencies[0][3] = post_div2;
            workingFrequencies[0][4] = 1;
            workingFrequencies[0][5] = 0;
            workingFrequencies[0][6] = out;
            workingFrequencies[0][7] = abs(int(wantedFreq - out));
            oneTimeArrayCheck = false;
            return;
          }
        }
      }
    }
  }
  return;
}

bool __not_in_flash_func(transmitFrequency)(float frequencyToTransmitMHz) {

  uint32_t frequencyToTransmitkHz = frequencyToTransmitMHz * 1000;  // Turn MHz to kHz

  Serial.print("freqkHz: ");
  Serial.println(frequencyToTransmitkHz);
  Serial.print("freqMHz: ");
  Serial.println(frequencyToTransmitMHz);

  uint32_t clockSpeedkHz = clock_get_hz(clk_sys) / 1000;

  harmonicTransmit = false;
  belowMinimumTransmit = false;

  // Check if the wanted frequency can be matched by clock exactly
  if (frequencyToTransmitkHz < minStableClockFrequencykHz) {
    Serial.println("Transmit frequency below 50MHz, transmitting with dividers instead of direct match");
    Serial.println("");
    belowMinimumTransmit = true;
  } else if (frequencyToTransmitkHz > maxStableClockFrequencykHz) {
    Serial.print("Transmit frequency above maximum stable clock frequency: ");
    Serial.print(maxStableClockFrequencykHz);
    Serial.println("kHz");
    Serial.println("Transmitting with harmonics");
    Serial.println("");
    harmonicTransmit = true;
  } else {
    if (set_sys_clock_khz(frequencyToTransmitkHz, false)) {
      workingFrequencies[0][0] = frequencyToTransmitkHz;
      set_sys_clock_khz(clockSpeedkHz, true);
      Serial.println("Transmit frequency matched by clock exactly");
      Serial.println("");
      return 0;
    }
  }

  // Increase clock speed to speed up calculation
  set_sys_clock_khz(250000, true);
  delay(100);

  Serial.println("Transmit frequency not matched by clock exactly, finding dividers...");
  oneTimeArrayCheck = true;
  int harmonicMultiplierMax = 0;
  int minFreqMult = 0;
  int harmonicMultiplierDouble = 0;
  int harmonicMult = 0;
  uint32_t baseHarmonicFrequency = 0;
  uint32_t baseMultipleHarmonicFrequency = 0;

  // Calculate clock frequency and dividers
  if (harmonicTransmit) {
    harmonicMultiplierMax = ceilf(1.0 * frequencyToTransmitkHz / maxStableClockFrequencykHz);
    harmonicMultiplierDouble = ceilf(2.0 * (1.0 * frequencyToTransmitkHz / maxStableClockFrequencykHz));
    baseHarmonicFrequency = round(1.0 * frequencyToTransmitkHz / harmonicMultiplierMax);
    baseMultipleHarmonicFrequency = round(1.0 * frequencyToTransmitkHz / harmonicMultiplierDouble);
    minFreqMult = floorf(1.0 * frequencyToTransmitkHz / minStableClockFrequencykHz);
    Serial.println(harmonicMultiplierMax);
    Serial.println(harmonicMultiplierDouble);
    Serial.println(baseHarmonicFrequency);
    Serial.println(baseMultipleHarmonicFrequency);
    workingFrequencies[0][0] = maxStableClockFrequencykHz;
    workingFrequencies[0][1] = 1500000000;
    workingFrequencies[0][2] = 5;
    workingFrequencies[0][3] = 1;
    workingFrequencies[0][4] = 1;
    workingFrequencies[0][5] = 0;
    workingFrequencies[0][6] = maxStableClockFrequencykHz;
    workingFrequencies[0][7] = abs(int(frequencyToTransmitkHz - maxStableClockFrequencykHz));

    for (uint32_t testClockFrequency = baseHarmonicFrequency - 500; testClockFrequency < (baseHarmonicFrequency + 500) && testClockFrequency >= minStableClockFrequencykHz && testClockFrequency < maxStableClockFrequencykHz; testClockFrequency += clockFrequencyIteration) {
      calculatePrimaryClocks(testClockFrequency, baseHarmonicFrequency);
      //Serial.println("b");
    }

    for (harmonicMult = harmonicMultiplierDouble; harmonicMult < 5; harmonicMult++) {
      for (uint32_t testClockFrequency = round((1.0*frequencyToTransmitkHz/harmonicMult) - 200)*2; testClockFrequency < maxStableClockFrequencykHz && testClockFrequency >= minStableClockFrequencykHz; testClockFrequency += clockFrequencyIteration) {
        calculateClockDividers(testClockFrequency, round((1.0*frequencyToTransmitkHz/harmonicMult)), harmonicMult);
        if (workingFrequencies[0][7] <= 5) {
          break;
        }
        //Serial.println("c");
      }
      if (workingFrequencies[0][7] <= 5) {
          break;
        }
    }

  } else if (belowMinimumTransmit) {
    int belowMinimumMinMultiplier = ceilf(1.0 * minStableClockFrequencykHz / frequencyToTransmitkHz);
    //int belowMinimumMaxMultiplier = floorf(maxStableClockFrequencykHz/frequencyToTransmitkHz);
    workingFrequencies[0][0] = 50000;
    workingFrequencies[0][1] = 1500000000;
    workingFrequencies[0][2] = 6;
    workingFrequencies[0][3] = 5;
    workingFrequencies[0][4] = 1;
    workingFrequencies[0][5] = 0;
    workingFrequencies[0][6] = 50000;
    workingFrequencies[0][7] = abs(int(frequencyToTransmitkHz - 50000));

    if ((belowMinimumMinMultiplier * frequencyToTransmitkHz) - 200 < 50000) {
      for (uint32_t testClockFrequency = 50000; testClockFrequency < maxStableClockFrequencykHz && testClockFrequency >= minStableClockFrequencykHz; testClockFrequency += clockFrequencyIteration) {
        calculateClockDividers(testClockFrequency, frequencyToTransmitkHz, 1);
      }
    } else {
      for (uint32_t testClockFrequency = (belowMinimumMinMultiplier * frequencyToTransmitkHz) - 200; testClockFrequency < maxStableClockFrequencykHz && testClockFrequency >= minStableClockFrequencykHz; testClockFrequency += clockFrequencyIteration) {
        calculateClockDividers(testClockFrequency, frequencyToTransmitkHz, 1);
      }
    }
  } else {
    for (uint32_t testClockFrequency = (frequencyToTransmitkHz - 500); testClockFrequency < (frequencyToTransmitkHz + 500) && testClockFrequency >= minStableClockFrequencykHz; testClockFrequency += clockFrequencyIteration) {
      calculatePrimaryClocks(testClockFrequency, frequencyToTransmitkHz);
    }

    for (uint32_t testClockFrequency = (2 * frequencyToTransmitkHz) - 200; testClockFrequency < maxStableClockFrequencykHz && testClockFrequency >= minStableClockFrequencykHz; testClockFrequency += clockFrequencyIteration) {
      calculateClockDividers(testClockFrequency, frequencyToTransmitkHz, 1);
    }
  }

  // Revert clock speed back
  set_sys_clock_khz(clockSpeedkHz, false);
  delay(100);


  // Print all values of least frequency difference to serial
  Serial.print("Clock frequency: ");
  Serial.println(workingFrequencies[0][0]);
  Serial.print("VCO frequency: ");
  Serial.println(workingFrequencies[0][1]);
  Serial.print("Post divider 1: ");
  Serial.println(workingFrequencies[0][2]);
  Serial.print("Post divider 2: ");
  Serial.println(workingFrequencies[0][3]);
  Serial.print("Integer part clock divider: ");
  Serial.println(workingFrequencies[0][4]);
  Serial.print("Decimal part clock divider: ");
  Serial.println(workingFrequencies[0][5]);
  if (harmonicTransmit) {
    Serial.print("Maximum harmonic multiplier: ");
    Serial.println(harmonicMultiplierMax);
    Serial.print("Double harmonic multiplier: ");
    Serial.println(harmonicMultiplierDouble);
    Serial.print("Achieved frequency: ");
    Serial.println(workingFrequencies[0][6] * harmonicMult);
  } else {
    Serial.print("Achieved frequency: ");
    Serial.println(workingFrequencies[0][6]);
  }
  Serial.print("Frequency difference: ");
  Serial.println(workingFrequencies[0][7]);
  Serial.println("");

  return 1;

  //set_sys_clock_pll(vco_freq, post_div1, post_div2)
}


void __not_in_flash_func(setup)() {
  vreg_set_voltage(VREG_VOLTAGE_1_30);
  delay(100);
  set_sys_clock_khz(maxStableClockFrequencykHz, true);
  delay(100);
  Serial.begin(115200);
  delay(5000);
  Serial.println("Program started");
  pinMode(led, OUTPUT);
}

void __not_in_flash_func(loop)() {

  startTimeReceive = micros();

  float rxDataFloat = 0;
  byte rxByte;
  String rxData;
  if (Serial.available()) {
    while (char(rxByte) != endMark) {
      if (Serial.available()) {
        rxByte = Serial.read();
        rxData += char(rxByte);
      }
    }
  }
  rxData.trim();
  rxDataFloat = rxData.toFloat();

  endTimeReceive = micros();

  if (rxDataFloat != 0 && !userInput) {
    Serial.print("Received frequency: ");
    Serial.println(rxDataFloat);
    Serial.println("");
    startTime = micros();
    returnVal = transmitFrequency(rxDataFloat);
    endTime = micros();
    Serial.print("Time taken for operation in microseconds: ");
    Serial.println(endTime - startTime);
    Serial.println("");
    Serial.print("Time taken for operation in seconds: ");
    Serial.println((endTime - startTime) / 1000000);
    Serial.println("");
    Serial.print("Serial receive time in microseconds: ");
    Serial.println(endTimeReceive - startTimeReceive);
    Serial.println("");
    Serial.print("Transmit at frequency? (y/n) ");
    userInput = true;
  }
  if (userInput && rxData == "y") {
    Serial.println("");
    Serial.println("");
    userInput = false;
    if (returnVal == 0) {
      set_sys_clock_khz(workingFrequencies[0][0], false);
      clock_gpio_init_int_frac(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 1, 0);
    }
    if (returnVal == 1) {
      set_sys_clock_pll(workingFrequencies[0][1], workingFrequencies[0][2], workingFrequencies[0][3]);
      clock_gpio_init_int_frac(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, workingFrequencies[0][4], workingFrequencies[0][5]);
    }
    Serial.println("Transmitting");
    transmitting = true;
  } else if (userInput && rxData == "n") {
    userInput = false;
  }
  if (transmitting && !userInput && rxData == "stop") {
    clock_stop(clk_gpout0);
    transmitting = false;
    Serial.println("Transmitting stopped");
    Serial.println("");
  }

  digitalWrite(led, HIGH);
  delay(500);
  digitalWrite(led, LOW);
  delay(500);
}
