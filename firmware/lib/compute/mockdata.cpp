
#include "mockdata.h"

namespace mockdata {

// ===================== Mock Sensor Reads (replace these) =====================
// IMU example (replace with your driver e.g., LSM6DS, ICM-20948, MPU-6050, etc.)
bool mockReadIMU(int16_t& ax, int16_t& ay, int16_t& az,
                 int16_t& gx, int16_t& gy, int16_t& gz)
{
  // Generate simple waveform so you can see variation
  static int t = 0; t++;
  ax = (int16_t)(1000 * sinf(t * 0.02f));
  ay = (int16_t)(1000 * cosf(t * 0.02f));
  az = 9800; // ~1 g in mg if you pick that convention

  gx = (int16_t)( 10 * sinf(t * 0.05f));
  gy = (int16_t)( 10 * cosf(t * 0.05f));
  gz = (int16_t)(350); // constant-ish

  // return true if read succeeded
  return true;
}

// Heart rate sensor (replace with your driver, e.g., MAX3010x -> compute bpm)
bool mockReadHR(uint16_t& hr_x10) {
  // oscillate ~72.0 bpm +- 2 bpm
  static int t = 0; t++;
  float bpm = 72.0f + 2.0f * sinf(t * 0.01f);
  hr_x10 = (uint16_t)lroundf(bpm * 10.0f);
  return true;
}

// Temperature (replace with your I2C temp sensor)
bool mockReadTemp(int16_t& temp_x100) {
  // ~32.00C +- 0.5C
  static int t = 0; t++;
  float c = 32.00f + 0.5f * sinf(t * 0.015f);
  temp_x100 = (int16_t)lroundf(c * 100.0f);
  return true;
}

}  // namespace mockdata