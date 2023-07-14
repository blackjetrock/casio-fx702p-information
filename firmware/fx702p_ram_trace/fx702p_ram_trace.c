////////////////////////////////////////////////////////////////////////////////
//
// Casio FX702P RAM replacement
//
// Emulates four uPD444G RAM chips.
// Four CE lines, a WE and 10 address lines.
// Four bit data
//
////////////////////////////////////////////////////////////////////////////////

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"

#include "f_util.h"

#include "ff.h"
#include "ff_stdio.h"
#include "hw_config.h"
#include "my_debug.h"
#include "rtc.h"
#include "sd_card.h"

// Use this if breakpoints don't work
#define DEBUG_STOP {volatile int x = 1; while(x) {} }

#define TEST_SINGLE_ADDRESS  0
#define TEST_ALL_ADDRESS     0

//-----------------------------------------------------------------------------
//
// ROM Emulator Flags

// OE pin used to switch data direction
// If 0 then DDIR_A15 used, under processor conrol

#define USE_OE_FOR_DATA_DIRECTION 1

// The address lines we are looking at

// Do we run emulation on second core?
#define EMULATE_ON_CORE1   1

// RAM chip is 4K

#define ROM_SIZE  5*1024
#define ADDRESS_MASK 0x03FF

#define RAM_CE_SIZE  1024

// Map from memory space to ROM address space
#define MAP_ROM(X) (X & ADDRESS_MASK)

volatile uint8_t rom_data[ROM_SIZE] =
  {
   // ASSEMBLER_EMBEDDED_CODE_START

   // ASSEMBLER_EMBEDDED_CODE_END
  };

//------------------------------------------------------------------------------


//
// The address lines (always inputs)
//
const int  A0_PIN  =  0;
const int  A1_PIN  =  1;
const int  A2_PIN  =  2;
const int  A3_PIN  =  3;
const int  A4_PIN  =  4;
const int  A5_PIN  =  5;
const int  A6_PIN  =  6;
const int  A7_PIN  =  7;
const int  A8_PIN  =  8;
const int  A9_PIN  =  9;

const int  D0_PIN = 10;
const int  D1_PIN = 11;
const int  D2_PIN = 12;
const int  D3_PIN = 13;

const int INPUT1_PIN   = 26;
const int INPUT0_PIN   = 27;

// 444 has a select pin and an write pin, both active low
const int CE0_PIN       = 14;
const int CE1_PIN       = 15;
const int CE2_PIN       = 16;
const int CE3_PIN       = 17;
const int CE4_PIN       = 18;

#define CE_MASK  0x1F

const int W_PIN       = 19;

// Arrays for setting GPIOs up
#define NUM_ADDR 10
#define NUM_DATA 4

#define DATA_MASK 0x0F

const int address_pins[NUM_ADDR] =
  {
   A0_PIN,
   A1_PIN,
   A2_PIN,
   A3_PIN,
   A4_PIN,
   A5_PIN,
   A6_PIN,
   A7_PIN,
   A8_PIN,
   A9_PIN,
  };

const int data_pins[NUM_DATA] =
  {
   D0_PIN,
   D1_PIN,
   D2_PIN,
   D3_PIN,
  };

////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//
// Put a value on the data out lines. Value is 4 LS bits of the rom array.

inline void set_data(BYTE data)
{
  int states;
  int dat = data & DATA_MASK;
  
  // Direct register access to make things faster
  sio_hw->gpio_set = (  dat  << D0_PIN);
  sio_hw->gpio_clr = ((dat ^ DATA_MASK) << D0_PIN);
}

// Only deals with DOUT pin
inline void set_data_inputs(void)
{
  sio_hw->gpio_oe_clr = (DATA_MASK << D0_PIN);
}

inline void set_data_outputs(void)
{
  sio_hw->gpio_oe_set = (DATA_MASK<<D0_PIN);
}


////////////////////////////////////////////////////////////////////////////////
//
// Set things up then sit in a loop waiting for the emulated device to
// be selected
//
////////////////////////////////////////////////////////////////////////////////

#define MAX_ADDR_TRACE 1024*64
int trace_on = 0;
volatile int addr_trace_index = 0;
volatile uint16_t addr_trace[MAX_ADDR_TRACE];
volatile unsigned int number_ce_assert = 0;


////////////////////////////////////////////////////////////////////////////////
//
// Emulate a RAM chip
//
////////////////////////////////////////////////////////////////////////////////

#define EM_USB 1


void ram_emulate(void)
{
  //printf("\nEmulating RAM...");

  irq_set_mask_enabled( 0xFFFFFFFF, 0 );

  while(1)
    {
      uint32_t gpio_states;
      BYTE db;
      unsigned int addr;
      
      // We look for CE low
      if( ((gpio_states = sio_hw->gpio_in) & (CE_MASK << CE0_PIN)) == (CE_MASK << CE0_PIN) )
      	{
	  // S high, we are not selected
	  // Data lines inputs
	  set_data_inputs();
      	}
      else
      	{
	  // S low, we are selected

	  // Get the select number
	  int selbits = (gpio_states & (CE_MASK << CE0_PIN)) >> CE0_PIN;
	  int selnum = 0;
#if 0
	  rom_data[0] = selbits;
	  rom_data[1] = selbits;
	  rom_data[2] = selbits;
#endif	  
	  switch(selbits)
	    {
	    case 0x1e:
	      selnum = 0;
	      break;

	    case 0x1D:
	      selnum = 1;
	      break;

	    case 0x1B:
	      selnum = 2;
	      break;

	    case 0x17:
	      selnum = 3;
	      break;

	    case 0x0f:
	      selnum = 4;
	      break;
	    }

#if EM_USB
	  printf("\nSEL %d", selnum);
#endif
	  // We have to monitor W for a write pulse
	  // if we see it then we write the data on the rising edge of W
	  // While W is high we treat this as a read and present data

	  while(1)
	    {
	      gpio_states = sio_hw->gpio_in;

	      addr = (gpio_states >> A0_PIN) & ADDRESS_MASK;
	      
	      // Is this a read or a write?
	      if( (gpio_states & ( 1<< W_PIN))==0 )
		{

		  // Write
		  // data lines inputs
		  set_data_inputs();
		  
		  // Wait for W to go high then latch data

		  while( ((gpio_states = sio_hw->gpio_in) & (1 << W_PIN))==0 )
		    {
		    }
		  
		  // We have 4 bits of data to store, they are read from the Dn pins
		  rom_data[addr+selnum*RAM_CE_SIZE] = ((gpio_states & (DATA_MASK << D0_PIN))>>D0_PIN);

		  while( ((gpio_states = sio_hw->gpio_in) & (CE_MASK << CE0_PIN)) != (CE_MASK << CE0_PIN) )
		    {
		    }

#if EM_USB
		  printf("\nWR %04X %01X", addr, (gpio_states & (DATA_MASK << D0_PIN))>>D0_PIN);
#endif
		}
	      else
		{

		  // Read
		  // make DOUT an output
		  set_data_outputs();
		  
		  // ROM emulation so always a read of us
		  // get address
		  //addr = (gpio_states >> A0_PIN) & ADDRESS_MASK;


		  // Get data and present it on bus (single bit)
		  set_data(rom_data[addr+selnum*RAM_CE_SIZE]);
		  
		  while( ((gpio_states = sio_hw->gpio_in) & (CE_MASK << CE0_PIN)) != (CE_MASK << CE0_PIN) )
		    {
		    }
		  
#if EM_USB
		  printf("\nRD %04X %01X", addr, rom_data[addr+selnum*RAM_CE_SIZE]);
#endif		  
		  //set_data(0xF8);
		}

	      if( ((gpio_states = sio_hw->gpio_in) & (CE_MASK << CE0_PIN)) == (CE_MASK << CE0_PIN) )
		//if( gpio_states & (1 << CE_PIN) )
		{
		  // S gone high, exit the loop
		  // Delay for 100ns or so as the real 4044 does this
		  // and data is available for up to 100ns after CE rises.
#if 1
		  for(volatile int d=0; d<100; d++)
		    {
		    }
#endif		  
		  set_data_inputs();
#if EM_USB
		  printf("\nDesel");
#endif
		  break;
		}

	    }
#if 0	  
	  // Trace address
	  if( addr == 3 )
	    {
	      trace_on = 1;
	    }
	  
	  if( trace_on)
	    {
	      if( addr_trace_index < MAX_ADDR_TRACE )
		{
		  int tv = addr;
		  if( addr_trace[addr_trace_index] == tv )
		    {
		    }
		  else
		    {
		      addr_trace_index++;
		      addr_trace[addr_trace_index] = tv;
		    }
		  //addr_trace_index %= MAX_ADDR_TRACE;
		  
		}
	    }
	  number_ce_assert++;

#endif
#if 0
	  
	  // Wait for CE to be de-asserted
	  while(1)
	    {
	      // S high, we are not selected
	      // data lines inputs
	      gpio_states = sio_hw->gpio_in;
	      //printf("\nWAIT %08X", gpio_states);
	      //printf("  %02X", gpio_get(CE_PIN));
	      // We look for S
	      if( (gpio_states & (CE_MASK << CE0_PIN)) == (CE_MASK << CE0_PIN) )
		//if( gpio_states & (1 << CE_PIN) )
		{
		  // S high, we are not selected
		  // data lines inputs
		  set_data_inputs();
		  //printf("\nDONE WAIT");
		  break;
		}
	    }
#endif
	}
    }
}

void set_gpio_input(int gpio_pin)
{
  gpio_init(gpio_pin);
  gpio_set_dir(gpio_pin, GPIO_IN);
  //  gpio_set_pulls(gpio_pin, 0, 0);
}

void set_gpio_output(int gpio_pin)
{
  gpio_init(gpio_pin);
  gpio_set_dir(gpio_pin, GPIO_OUT);
}

////////////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////////////

int main()
{

  //DEBUG_STOP;
  
  char line[80];

#if TEST_SINGLE_ADDRESS
  int count = 0;
  
  for (int i=0; i<NUM_ADDR; i++)
    {
      set_gpio_output(address_pins[i]);
    }

  while(1)
    {
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 0);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
      gpio_put(A0_PIN, 1);
    }

#endif

  
#if TEST_ALL_ADDRESS
  int count = 0;

  // Set all GPIOs as outputs
  
  for (int i=0; i<NUM_ADDR; i++)
    {
      set_gpio_output(address_pins[i]);
    }

    for (int i=0; i< NUM_DATA; i++)
    {
      set_gpio_output(data_in_pins[i]);
      set_gpio_output(data_out_pins[i]);
    }

  while(1)
    {
      
      for (int i=0; i<NUM_ADDR; i++)
	{
	  gpio_put(address_pins[i], count & (1 <<i));
	}

      for (int i=0; i<NUM_DATA; i++)
	{
	  gpio_put(data_out_pins[i], count & (1 <<i));
	}

      for (int i=0; i<NUM_DATA; i++)
	{
	  gpio_put(data_in_pins[i], count & (1 <<i));
	}
      count++;
    }

#endif


  
  //#define OVERCLOCK 135000
//#define OVERCLOCK 200000
#define OVERCLOCK 270000
//#define OVERCLOCK 360000

  #if OVERCLOCK > 270000
  /* Above this speed needs increased voltage */
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(1);
#endif

  /* Overclock */
  set_sys_clock_khz( OVERCLOCK, 1 );

  stdio_init_all();
  
  for (int i=0; i<NUM_ADDR; i++)
    {
      set_gpio_input(address_pins[i]);
    }

  for( int i=0; i< NUM_DATA; i++)
    {
      set_gpio_input(data_pins[i]);
    }
  
  set_gpio_input(CE0_PIN);
  set_gpio_input(CE1_PIN);
  set_gpio_input(CE2_PIN);
  set_gpio_input(CE3_PIN);
  set_gpio_input(CE4_PIN);
  set_gpio_input(W_PIN);

  // We sit in a loop and capture the GPIOs

  
  //multicore_launch_core1(ram_emulate);



  sleep_ms(2000);
  
  printf("\n\n");
  printf("\n/------------------------------\\");
  printf("\n| Casio FX702P uPD444G RAM     |");
  printf("\n| Replacement                  |");
  printf("\n/------------------------------/");
  printf("\n");
  
  printf("\nSetting GPIOs...");

  for(int i=0; i<1024*4; i++)
    {
      rom_data[i] = 0xAA;
    }

#if 1
  uint32_t gpio_states;
  
  while(1)
    {
      gpio_states = sio_hw->gpio_in;

      printf("\nA:%04X", (gpio_states & ADDRESS_MASK)>> A0_PIN);
    }
#endif

  // Sit in a loop and do nothing on this core for now.
  
  while(1)
    {
      sleep_ms(5000);

      printf("\n\n");
      
      for(int i=0; i<1024*4; i++)
	{
	  if( (i % 32) == 0 )
	    {
	      printf("\n%04X: ", i);
	      
	    }
	  printf(" %02X", rom_data[i]);
	}
    }
}
