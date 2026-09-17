#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "ti_msp_dl_config.h"
#include "../hal/gpio.hpp"

namespace {

constexpr std::uintptr_t gpio_a_base = 0x400A'0000;
constexpr std::uintptr_t gpio_b_base = 0x400A'2000;
constexpr std::uintptr_t iomux_base = 0x4042'8000;
constexpr std::uint32_t gpio_power_enable = 0x2600'0001;
constexpr std::uint32_t gpio_function = 0x0000'0001;
constexpr std::uint32_t peripheral_connected = 1U << 7U;
constexpr std::uint32_t pull_down_enabled = 1U << 16U;
constexpr std::uint32_t pull_up_enabled = 1U << 17U;
constexpr std::uint32_t input_enabled = 1U << 18U;

//preserve offsets, represent power, direction, input mapping, etc.
struct gpio_register_map
{
  std::uint32_t reserved0[0x800 / sizeof(std::uint32_t)];
  volatile std::uint32_t power_enable;
  std::uint32_t reserved1[(0x1290 - 0x804) / sizeof(std::uint32_t)];
  volatile std::uint32_t data_output_set;
  std::uint32_t reserved2[3];
  volatile std::uint32_t data_output_clear;
  std::uint32_t reserved3[(0x12D0 - 0x12A4) / sizeof(std::uint32_t)];
  volatile std::uint32_t data_output_enable_set;
  std::uint32_t reserved4[3];
  volatile std::uint32_t data_output_enable_clear;
  std::uint32_t reserved5[(0x1380 - 0x12E4) / sizeof(std::uint32_t)];
  volatile std::uint32_t data_input;
};

//iomux and pincm
struct iomux_register_map
{
  std::uint32_t reserved0;
  volatile std::uint32_t pin_control[251];
};

static_assert(offsetof(gpio_register_map, power_enable) == 0x800);
static_assert(offsetof(gpio_register_map, data_output_set) == 0x1290);
static_assert(offsetof(gpio_register_map, data_output_clear) == 0x12A0);
static_assert(offsetof(gpio_register_map, data_output_enable_set) == 0x12D0);
static_assert(offsetof(gpio_register_map, data_output_enable_clear) == 0x12E0);
static_assert(offsetof(gpio_register_map, data_input) == 0x1380);
static_assert(offsetof(iomux_register_map, pin_control) == 0x4);

enum class port : std::uint8_t
{
  a,
  b,
};

//port to register
gpio_register_map* select_port(port p_port)
{
  const auto address = p_port == port::a ? gpio_a_base : gpio_b_base;
  return reinterpret_cast<gpio_register_map*>(address);
}

//return pincm number; debug
volatile std::uint32_t* select_pin_control(
  std::uint8_t p_pincm_number)
{
  auto* iomux = reinterpret_cast<iomux_register_map*>(iomux_base);
  return &iomux->pin_control[p_pincm_number - 1U];
}

constexpr std::uint32_t resistor_bits(
  lab1::pin_resistor p_resistor)
{
  switch (p_resistor) {
    case lab1::pin_resistor::none:
      return 0U;
    case lab1::pin_resistor::pull_down:
      return pull_down_enabled;
    case lab1::pin_resistor::pull_up:
      return pull_up_enabled;
  }

  return 0U;
}

void enable_port(gpio_register_map& p_registers)
{
  p_registers.power_enable = gpio_power_enable;

  for (std::uint8_t cycle = 0; cycle < 4U; ++cycle) {
    (void)p_registers.power_enable;
  }
}

//output driver
class mspm0g3507_output_pin final : public lab1::output_pin
{
public:
  mspm0g3507_output_pin(port p_port, std::uint8_t p_pin, std::uint8_t p_pincm_number)
    : m_registers(select_port(p_port))
    , m_pin_control(select_pin_control(p_pincm_number))
    , m_mask(1U << p_pin)
  {
    configure({});
  }

private:
  bool driver_configure(settings const&) override
  {
    enable_port(*m_registers);

    //output pull before enable pin
    m_registers->data_output_clear = m_mask;
    *m_pin_control = peripheral_connected | gpio_function;
    m_registers->data_output_enable_set = m_mask;
    return true;
  }

  void driver_level(bool p_high) override
  {
    if (p_high) {
      m_registers->data_output_set = m_mask;
    } else {
      m_registers->data_output_clear = m_mask;
    }
  }

  bool driver_level() override
  {
    return (m_registers->data_input & m_mask) != 0U;
  }

  gpio_register_map* m_registers;
  volatile std::uint32_t* m_pin_control;
  std::uint32_t m_mask;
};

//input driver
class mspm0g3507_input_pin final : public lab1::input_pin
{
public:
  mspm0g3507_input_pin(port p_port, std::uint8_t p_pin, std::uint8_t p_pincm_number, lab1::pin_resistor p_resistor = lab1::pin_resistor::none)
    : m_registers(select_port(p_port))
    , m_pin_control(select_pin_control(p_pincm_number))
    , m_mask(1U << p_pin)
  {
    configure({p_resistor});
  }

private:
  bool driver_configure(settings const& p_settings) override
  {
    enable_port(*m_registers);

    m_registers->data_output_enable_clear = m_mask;
    *m_pin_control = input_enabled | peripheral_connected | gpio_function | resistor_bits(p_settings.resistor);
    return true;
  }

  bool driver_level() override
  {
    return (m_registers->data_input & m_mask) != 0U;
  }

  gpio_register_map* m_registers;
  volatile std::uint32_t* m_pin_control;
  std::uint32_t m_mask;
};


enum class led_color : std::uint8_t
{
  off,
  red,
  green,
  blue,
  yellow,
  magenta,
  cyan,
  white,
};

void set_rgb_led(led_color p_color, lab1::output_pin& p_red, lab1::output_pin& p_green, lab1::output_pin& p_blue)
{
  const bool red_on =
    p_color == led_color::red ||
    p_color == led_color::yellow ||
    p_color == led_color::magenta ||
    p_color == led_color::white;

  const bool green_on =
    p_color == led_color::green ||
    p_color == led_color::yellow ||
    p_color == led_color::cyan ||
    p_color == led_color::white;

  const bool blue_on =
    p_color == led_color::blue ||
    p_color == led_color::magenta ||
    p_color == led_color::cyan ||
    p_color == led_color::white;

  p_red.level(red_on);
  p_green.level(green_on);
  p_blue.level(blue_on);
}

}

int main()
{
  SYSCFG_DL_init();

  std::printf("Hello, World\n");
  std::fflush(stdout);

  //PB22=50, PB26=57, PB27=58.
  mspm0g3507_output_pin blue(port::b, 22, 50);
  mspm0g3507_output_pin red(port::b, 26, 57);
  mspm0g3507_output_pin green(port::b, 27, 58);

  //red, green, blue, yellow, magenta, cyan, white
  constexpr led_color s1_color = led_color::blue;
  constexpr led_color s2_color = led_color::red;
  constexpr led_color both_switches_color = led_color::white;

  //target switches
  mspm0g3507_input_pin s1(port::a, 18, 40);
  mspm0g3507_input_pin s2(port::b, 21, 49);

  //s1 pulldown s2 pullup
  s1.configure({ lab1::pin_resistor::none });
  s2.configure({ lab1::pin_resistor::pull_up });

  while (true) {

    const bool s1_pressed = s1.level();
    const bool s2_pressed = !s2.level();

    //both switch logic
    led_color selected_color = led_color::off;
    if (s1_pressed && s2_pressed) {
      selected_color = both_switches_color;
    } else if (s1_pressed) {
      selected_color = s1_color;
    } else if (s2_pressed) {
      selected_color = s2_color;
    }

    set_rgb_led(selected_color, red, green, blue);
  }
}
