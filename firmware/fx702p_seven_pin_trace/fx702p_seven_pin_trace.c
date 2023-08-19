////////////////////////////////////////////////////////////////////////////////
//
// Casio FX702P Seven Pin connector Tracer
//
// Traces the seven pin connector on the FX702P and family.
//
// GPIO0:  CE
// GPIO1:  DATA
// GPIO2:  CONT
// GPIO3:  OP
// GPIO4:  SP
//
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
#include "pico/bootrom.h"

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

const int PIN_CE    = 0;
const int PIN_DATA  = 1;
const int PIN_CONT  = 2;
const int PIN_OP    = 3;
const int PIN_SP    = 4;

int address     = 0;

////////////////////////////////////////////////////////////////////////////////

// Serial loop command structure

typedef void (*FPTR)(void);

typedef struct
{
  char key;
  char *desc;
  FPTR fn;
} SERIAL_COMMAND;

int keypress = 0;
int parameter = 0;
int auto_increment_parameter = 0;
int auto_increment_address   = 0;


////////////////////////////////////////////////////////////////////////////////
//
// Set things up then sit in a loop waiting for the emulated device to
// be selected
//
////////////////////////////////////////////////////////////////////////////////

#define MAX_CONN_TRACE 1024*40
volatile int conn_trace_index = 0;
volatile uint16_t conn_trace_data[MAX_CONN_TRACE];
volatile uint8_t  conn_trace_flags[MAX_CONN_TRACE];
volatile int trace_on = 0;

#define CONN_TRACE_FLAG_OP  0x01

volatile unsigned int number_ce_assert = 0;

////////////////////////////////////////////////////////////////////////////////
//
// Prototypes
//
////////////////////////////////////////////////////////////////////////////////

void serial_help(void);

////////////////////////////////////////////////////////////////////////////////
//
// Emulate a RAM chip
//
////////////////////////////////////////////////////////////////////////////////

int replybits = 0;
int numbits = 0;

void connector_trace(void)
{
  int sp;
  int last_sp;

  irq_set_mask_enabled(0xffffffff, false);

  while(1)
    {
      while(!trace_on)
	{
	}
      
      // Look for rising clock edge, qualify with CE
      
      // Wait for CE to go active
      //while( !gpio_get(PIN_CE) )
      //	{
      //	}

      conn_trace_data[conn_trace_index] = 0;

      if( gpio_get(PIN_CE) )
	{
	  // Clock data in on rising edges of SP
	  numbits = 6;

	  // If we have a reply coming, clock that number of bits in
	  if( replybits != 0 )
	    {
	      numbits = replybits;
	    }
	  
	  while( numbits > 0 )
	    {
	      sp = gpio_get(PIN_SP);
	      
	      if( (sp == 1) && (last_sp == 0) )
		{
		  if( numbits == 1 )
		    {
		      // Store OP
		      if( gpio_get(PIN_OP) )
			{
			  conn_trace_flags[conn_trace_index] |= CONN_TRACE_FLAG_OP;
			}
		      else
			{
			  conn_trace_flags[conn_trace_index] &= ~CONN_TRACE_FLAG_OP;
			}
		    }
		  
		  // SP edge, capture data
		  conn_trace_data[conn_trace_index] <<= 1;
		  conn_trace_data[conn_trace_index] |=  !gpio_get(PIN_DATA);
		  
		  numbits--;
		  
		  if( numbits == 0 )
		    {
		      // We have a command, check and see what should be done
		      switch(conn_trace_data[conn_trace_index])
			{
			case 0x04:
			  // read status, one bit reply
			  replybits = 1;
			  break;

			default:
			  replybits = 0;
			  break;
			}
		    }
		  
		}
	      last_sp = sp;
	    }
	  
	  conn_trace_index++;
	  
	  if( conn_trace_index >= MAX_CONN_TRACE )
	    {
	      trace_on = 0;
	    }
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

void cli_start_trace(void)
{
  conn_trace_index = 0;
  trace_on = 1;
}

void cli_display_trace(void)
{
  for(int i=0; i<MAX_CONN_TRACE; i++)
    {
      printf("\n%05d: %c OP=%d %016X", i, (i==conn_trace_index)?'*':' ', (conn_trace_flags[i] & CONN_TRACE_FLAG_OP), conn_trace_data[i]);
    }

  printf("\nIndex:%05d", conn_trace_index);
  printf("\n");
}

// Grab GPIOs for a bit to try to work out what is going on with them.

#define NUM_GPIO_GRAB  10000

uint8_t gpio_grab[NUM_GPIO_GRAB];

void cli_gpio_grab(void)
{
  for(int i=0; i<NUM_GPIO_GRAB; i++)
    {
      
      gpio_grab[i] = (sio_hw->gpio_in) & 0x1F;
    }
}

void cli_dump_gpio_grab(void)
{
  for(int i=0; i<NUM_GPIO_GRAB; i++)
    {
      int ce   = (gpio_grab[i] & (1<<PIN_CE  )) >> PIN_CE;
      int data = (gpio_grab[i] & (1<<PIN_DATA)) >> PIN_DATA;
      int cont = (gpio_grab[i] & (1<<PIN_CONT)) >> PIN_CONT;
      int op   = (gpio_grab[i] & (1<<PIN_OP  )) >> PIN_OP;
      int sp   = (gpio_grab[i] & (1<<PIN_SP  )) >> PIN_SP;

      // Data inverted
      data = 1-data;
      
      printf("\n%05d: CE:%d DATA:%d CONT:%d OP:%d SP:%d", i, ce, data, cont, op, sp);
    }

  printf("\n");
}


void cli_follow(void)
{

  int data_bits_to_go = 10;
  int dataword = 0;

  int done = 0;

  int gpio_grab = (sio_hw->gpio_in) & 0x1F;
  
  int ce   = (gpio_grab & (1<<PIN_CE  )) >> PIN_CE;
  int data = (gpio_grab & (1<<PIN_DATA)) >> PIN_DATA;
  int cont = (gpio_grab & (1<<PIN_CONT)) >> PIN_CONT;
  int op   = (gpio_grab & (1<<PIN_OP  )) >> PIN_OP;
  int sp   = (gpio_grab & (1<<PIN_SP  )) >> PIN_SP;

  int last_sp = sp;
  
  while(!done)
    {
      gpio_grab = (sio_hw->gpio_in) & 0x1F;
      
      ce   = (gpio_grab & (1<<PIN_CE  )) >> PIN_CE;
      data = (gpio_grab & (1<<PIN_DATA)) >> PIN_DATA;
      cont = (gpio_grab & (1<<PIN_CONT)) >> PIN_CONT;
      op   = (gpio_grab & (1<<PIN_OP  )) >> PIN_OP;
      sp   = (gpio_grab & (1<<PIN_SP  )) >> PIN_SP;

      if( ((sp == 1) && (last_sp == 0)) && (ce == 1) )
	{
	  // Data inverted
	  data = 1-data;
	  
	  if( data_bits_to_go > 0 )
	    {
	      dataword <<= 1;
	      dataword += data;
	      
	      data_bits_to_go--;
	      
	      if(data_bits_to_go == 0 )
		{
		  printf("\n%04X", dataword);
		  switch(dataword)
		    {
		    case 0x04:
		      printf("  Read status");
		      dataword = 0;
		      break;
		      
		    case 0x02CD:
		      printf("  header");
		      
		      data_bits_to_go = 6;
		      dataword = 0;
		      break;
		    }
		}
	    }
	}
      
      last_sp = sp;
    }
  
  printf("\n");
}

int ce_edge_count = 0;

void cli_count_ce_edges(void)
{
  
  int gpios = (sio_hw->gpio_in) & 0x1F;
  
  int ce   = (gpios & (1<<PIN_CE  )) >> PIN_CE;
  int data = (gpios & (1<<PIN_DATA)) >> PIN_DATA;
  int cont = (gpios & (1<<PIN_CONT)) >> PIN_CONT;
  int op   = (gpios & (1<<PIN_OP  )) >> PIN_OP;
  int sp   = (gpios & (1<<PIN_SP  )) >> PIN_SP;

  int last_ce   = ce;
  int last_data = data;
  int last_cont = cont;
  int last_op   = op;
  int last_sp   = sp;

  
  int i = 0;
  
  while(i < NUM_GPIO_GRAB)
    {
      gpio_grab[i] = (sio_hw->gpio_in) & 0x1F;

      ce   = (gpio_grab[i] & (1<<PIN_CE  )) >> PIN_CE;
      data = (gpio_grab[i] & (1<<PIN_DATA)) >> PIN_DATA;
      cont = (gpio_grab[i] & (1<<PIN_CONT)) >> PIN_CONT;
      op   = (gpio_grab[i] & (1<<PIN_OP  )) >> PIN_OP;
      sp   = (gpio_grab[i] & (1<<PIN_SP  )) >> PIN_SP;

      if( ce == 1 )
	{
	  if( (sp == 1) && (last_sp == 0) )
	    {
	      // Rising edge of clock, capture data and OP
	      i++;
	    }
	}
      
      sleep_us(1);

      last_ce   = ce;
      last_data = data;
      last_cont = cont;
      last_op   = op;
      last_sp   = sp;

    }
  
  cli_dump_gpio_grab();
  
}

void cli_boot_mass(void)
{
  reset_usb_boot(0,0);
}

// Another digit pressed, update the parameter variable
void cli_digit(void)
{
  parameter *= 10;
  parameter += keypress-'0';
}

void cli_zero_parameter(void)
{
  parameter = 0;
}

void cli_set_address(void)
{
  address = parameter;
}

void cli_display_gpios(void)
{
  printf("\n%016X", sio_hw->gpio_in);
}

void cli_gpio_display_loop(void)
{
  while(1)
    {
      cli_display_gpios();
    }
}


////////////////////////////////////////////////////////////////////////////////

SERIAL_COMMAND serial_cmds[] =
  {
   {
    'h',
    "Serial command help",
    serial_help,
   },
   {
    '?',
    "Serial command help",
    serial_help,
   },
   {
    'z',
    "Zero parameter",
    cli_zero_parameter,
   },
   {
    'f',
    "Follow trace",
    cli_follow,
   },
   {
    '+',
    "Start Tracing",
    cli_start_trace,
   },
   {
    't',
    "Display Trace",
    cli_display_trace,
   },
   {
    '*',
    "GPIO Display Loop",
    cli_gpio_grab,
   },
   {
    '-',
    "Display GPIOs",
    cli_dump_gpio_grab,
   },
   {
    'A',
    "Set Address",
    cli_set_address,
   },
   {
    'e',
    "Count CE edges",
    cli_count_ce_edges,
   },
   {
    '0',
    "*Digit",
    cli_digit,
   },
   {
    '1',
    "*Digit",
    cli_digit,
   },
   {
    '2',
    "*Digit",
    cli_digit,
   },
   {
    '3',
    "*Digit",
    cli_digit,
   },
   {
    '4',
    "*Digit",
    cli_digit,
   },
   {
    '5',
    "*Digit",
    cli_digit,
   },
   {
    '6',
    "*Digit",
    cli_digit,
   },
   {
    '7',
    "*Digit",
    cli_digit,
   },
   {
    '8',
    "*Digit",
    cli_digit,
   },
   {
    '9',
    "*Digit",
    cli_digit,
   },
   {
    '!',
    "Boot to mass storage",
    cli_boot_mass,
   },
  };



void serial_help(void)
{
  printf("\n");
  
  for(int i=0; i<sizeof(serial_cmds)/sizeof(SERIAL_COMMAND);i++)
    {
      if( *(serial_cmds[i].desc) != '*' )
	{
	  printf("\n%c:   %s", serial_cmds[i].key, serial_cmds[i].desc);
	}
    }
  printf("\n0-9: Enter parameter digit");
}


void prompt(void)
{
  printf("\n(Parameter:%d (%04X) %c, Address:%d (%04X) %c) >",
	 parameter, parameter, auto_increment_parameter?'A':' ',
	 address,   address,   auto_increment_address?  'A':' ');
}

////////////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////////////

void serial_loop()
{
  int  key;
  
  if( ((key = getchar_timeout_us(1000)) != PICO_ERROR_TIMEOUT))
    {
      for(int i=0; i<sizeof(serial_cmds)/sizeof(SERIAL_COMMAND);i++)
	{
	  if( serial_cmds[i].key == key )
	    {

	      keypress = key;
	      (*serial_cmds[i].fn)();
	      prompt();
	      break;
	    }
	}
    }
  else
    {
      // I have found that I need to send something if the serial USB times out
      // otherwise I get lockups on the serial communications.
      // So, if we get a timeout we send a spoace and backspace it. And
      // flush the stdio, but that didn't fix the problem but seems like a good idea.
      stdio_flush();
      printf(" \b");
    }
}

////////////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////////////

int main()
{

  set_gpio_input(PIN_CE);
  set_gpio_input(PIN_DATA);
  set_gpio_input(PIN_CONT);
  set_gpio_input(PIN_OP);
  set_gpio_input(PIN_SP);

  //--------------------------------------------------------------------------------
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
  
  multicore_launch_core1(connector_trace);

  sleep_ms(2000);
  
  printf("\n\n");
  printf("\n/-------------------------------------\\");
  printf("\n| Casio Seven Pin Connector Trace     |");
  printf("\n/-------------------------------------/");
  printf("\n");
  
  printf("\nSetting GPIOs...");

  ////////////////////////////////////////////////////////////////////////////////
  //
  // Main loop 
  //
  ////////////////////////////////////////////////////////////////////////////////
  
  while(1)
    {
      serial_loop();
    }
}
