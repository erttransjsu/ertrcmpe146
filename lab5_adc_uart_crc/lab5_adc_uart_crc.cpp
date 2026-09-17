#include <cstdint>
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

#include "../hal/adc.hpp"
#include "../hal/crc.hpp"
#include "../hal/gpio.hpp"
#include "../hal/serial.hpp"
#include "../hal/timer.hpp"

// TODO(lab, step 1): Make this class inherit from and implement the
// `lab5::adc16_stream` interface (`adc.hpp`), wired to the on-board internal
// temperature sensor channel, written register-level - no DriverLib. Check
// the MSPM0G350x Technical Reference Manual and datasheet for how to enable
// and select that channel, and how to read back the factory calibration
// constants used to convert a raw sample into a temperature. read() must
// configure a single DMA transfer that fills the entire given buffer with
// consecutive conversions, put the calling FreeRTOS task to sleep (e.g.
// block on a semaphore or task notification) for the duration of that
// transfer, and let the DMA transfer-complete interrupt wake it once the
// buffer is full - one sleep per buffer, not one per sample.
class adc_driver
{};

// TODO(lab, step 2): Make this class inherit from and implement the
// `lab6::serial` interface (`serial.hpp`), written register-level - no
// DriverLib. driver_receive_buffer()/driver_receive_cursor() must be backed
// by a circular buffer that a second, independent DMA transfer fills
// continuously in the background - this is a separate DMA channel from the
// one the ADC driver uses on the sensor board. See
// `lab6::serial::receive_cursor()`'s docs for how a caller is expected to
// consume it.
class serial_driver
{};

// TODO(lab, step 3): Make this class inherit from and implement the
// `lab6::crc32` interface (`crc.hpp`), written register-level against the
// on-chip CRC accelerator peripheral - no DriverLib. This is the
// hardware-accelerated implementation you'll benchmark against the
// `crc32_software` reference supplied below.
class crc32_hardware
{};

// Supplied reference implementation - do not modify. This is the standard
// reflected CRC-32 (IEEE 802.3 / zlib) algorithm, computed bit-by-bit in
// software. Your crc32_hardware driver's output must match this for the
// same input, and should be measurably faster.
class crc32_software : public lab6::crc32
{
private:
  void driver_reset() override
  {
    m_crc = initial_value;
  }

  void driver_update(std::span<std::uint8_t const> p_data) override
  {
    for (auto const byte : p_data) {
      m_crc ^= byte;
      for (int bit = 0; bit < 8; bit++) {
        std::uint32_t const mask = -(m_crc & 1U);
        m_crc = (m_crc >> 1) ^ (polynomial & mask);
      }
    }
  }

  std::uint32_t driver_checksum() override
  {
    return m_crc ^ initial_value;
  }

  static constexpr std::uint32_t polynomial = 0xEDB8'8320U;
  static constexpr std::uint32_t initial_value = 0xFFFF'FFFFU;

  std::uint32_t m_crc = initial_value;
};

namespace {
// The one-byte message the display board sends to ask the sensor board for
// a fresh batch of readings. Its value doesn't matter - it just needs to be
// a single recognizable byte the sensor board is watching for.
constexpr std::uint8_t sample_request_command = 0xA5;

constexpr std::size_t samples_per_batch = 64;

// TODO(lab, decide): Pick an encoding for a single temperature sample that
// can resolve at least 0.5 degC steps - a plain integer count of whole
// degrees cannot. A fixed-point scheme (e.g. int16_t in units of 0.1 degC)
// or a float both work. Whatever you choose, both boards must agree on it,
// since the sensor board encodes 64 of these and the display board decodes
// them back out.
using temperature_sample_t = std::int16_t;

void sensor_task(void*)
{
  // TODO(lab, sensor role): Construct an adc_driver, a serial_driver, a
  // crc32_hardware (crc32_software above needs no setup - it's ready to use
  // as-is), and a lab1::input_pin for S1 or S2 (your choice) - this button
  // is only used for step 5 below, doesn't need an interrupt, a plain level
  // read is enough. Loop forever:
  //   1. Block waiting to receive sample_request_command from the display
  //      board (poll serial_driver's receive_cursor()/receive_buffer()).
  //   2. Fill a `std::array<std::uint16_t, samples_per_batch>` buffer with
  //      one call to adc_driver::read() (one DMA transfer, one task sleep).
  //   3. Convert each raw sample to a temperature_sample_t. This is the
  //      conversion you write yourself - see the MSPM0G350x Technical
  //      Reference Manual's ADC and temperature sensor sections for the
  //      calibration constants and formula, and don't forget your ADC's
  //      actual reference voltage matters here.
  //   4. Compute a CRC-32 over the resulting
  //      `std::array<temperature_sample_t, samples_per_batch>` with both
  //      crc32_hardware and the supplied crc32_software - time each with a
  //      lab2::steady_clock and print both durations (crc32_hardware should
  //      win).
  //   5. Read your corruption-test button. If it's currently held down,
  //      flip one bit anywhere in the sample buffer (after computing the
  //      CRC in step 4, so the CRC no longer matches the corrupted data).
  //   6. Transmit the 64 samples with the CRC appended over UART.
  while (true) {
  }
}

void display_task(void*)
{
  // TODO(lab, display role): Construct a serial_driver, a crc32_hardware,
  // two lab1::input_pin (S1 and S2), and three lab1::output_pin (the RGB
  // LED). Loop forever:
  //   1. Wait for either S1 or S2 to be pressed (edge-triggered interrupt
  //      or polled, your choice).
  //   2. Transmit sample_request_command to the sensor board.
  //   3. Block waiting to receive 64 temperature_sample_t values plus a
  //      trailing CRC-32 from the sensor board.
  //   4. Recompute the CRC-32 over the received samples with your
  //      crc32_hardware and compare it to the one that arrived. If it
  //      doesn't match, drop the packet, print an appropriate message, and
  //      set the RGB LED to magenta (red + blue) instead of a
  //      temperature-derived color - this is the CRC check visibly failing,
  //      not just logging it.
  //   5. Otherwise, turn the 64 samples into a single representative
  //      reading (e.g. average them) and set the RGB LED's color based on
  //      it: blue for cold, yellow for warm, red for hot. Where exactly
  //      those bands fall is your call.
  while (true) {
  }
}

// Resize task_stack_words if necessary, if crashes are detected, that may be
// due to stack overflows.
constexpr std::uint32_t task_stack_words = 512;
StaticTask_t device_task_tcb;
StackType_t device_task_stack[task_stack_words];

// This lab uses two boards playing different roles: a "sensor" board with
// the internal temperature sensor wired to its ADC, and a "display" board
// with the RGB LED and S1/S2 buttons. Both roles live in this one file -
// `this_device` below picks which set of tasks main() creates.
//
// For this lab, flip this by hand and rebuild before flashing each board.
// A more automated setup (e.g. a CI job that builds and flashes both roles
// from a single invocation) would instead read this from a compiler define
// passed on the build system's command line - something like
// `-DDEVICE_ROLE=DEVICE_ROLE_SENSOR` added to the project's build flags -
// rather than a hardcoded constant, so the role never has to be hand-edited
// in source.
enum class device_role : std::uint8_t
{
  sensor,
  display,
};

constexpr device_role this_device = device_role::sensor;
}  // namespace

int main()
{
  std::printf("Hello, World\n");

  if constexpr (this_device == device_role::sensor) {
    xTaskCreateStatic(sensor_task,
                      "sensor_task",
                      task_stack_words,
                      nullptr,
                      tskIDLE_PRIORITY + 1,
                      device_task_stack,
                      &device_task_tcb);
  } else {
    xTaskCreateStatic(display_task,
                      "display_task",
                      task_stack_words,
                      nullptr,
                      tskIDLE_PRIORITY + 1,
                      device_task_stack,
                      &device_task_tcb);
  }

  vTaskStartScheduler();

  // Only reached if vTaskStartScheduler() itself fails to start. The
  // idle/timer task memory callbacks required by
  // configSUPPORT_STATIC_ALLOCATION are already supplied by the freertos_kernel
  // project's dpl/StaticAllocs_freertos.c - don't redefine them here.
  while (true) {
  }
}
