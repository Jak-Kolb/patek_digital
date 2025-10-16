
#include <stdint.h>

#include <cmath>
#include <cstdlib>


namespace mockdata {
bool mockReadIMU(int16_t& ax, int16_t& ay, int16_t& az,
                 int16_t& gx, int16_t& gy, int16_t& gz);

bool mockReadHR(uint16_t& hr_x10);

bool mockReadTemp(int16_t& temp_x100);

}  // namespace mockdata