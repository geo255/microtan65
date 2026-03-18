#include <ctype.h>
#include <string.h>

#include "ay8910.h"
#include "cpu_6502.h"
#include "display.h"
#include "eprom.h"
#include "function_return_codes.h"
#include "invaders_sound.h"
#include "keyboard.h"
#include "serial.h"
#include "system.h"
#include "via_6522.h"

#define MAX_DEVICES 32
memory_mapped_device_t devices[MAX_DEVICES];
static int device_count = 0;
static uint8_t system_memory[65536];
static uint8_t memory_read_only[65536];

static void read_and_ignore(void* ptr, size_t size, size_t count, FILE* file) {
  size_t items_read = fread(ptr, size, count, file);
  (void)items_read;
}

static bool write_and_check(const void* ptr, size_t size, size_t count, FILE* file) {
  return fwrite(ptr, size, count, file) == count;
}

static uint8_t intel_hex_checksum(const uint8_t* data, size_t count) {
  uint32_t sum = 0;

  for (size_t i = 0; i < count; i++) {
    sum += data[i];
  }

  return (uint8_t)(0U - (sum & 0xFFU));
}

static int hex_nibble(char value) {
  if ((value >= '0') && (value <= '9')) {
    return value - '0';
  }

  if ((value >= 'a') && (value <= 'f')) {
    return 10 + (value - 'a');
  }

  if ((value >= 'A') && (value <= 'F')) {
    return 10 + (value - 'A');
  }

  return -1;
}

static int parse_hex_byte(const char* ptr, uint8_t* value) {
  int hi = hex_nibble(ptr[0]);
  int lo = hex_nibble(ptr[1]);

  if ((hi < 0) || (lo < 0)) {
    return RV_INVALID_FILE;
  }

  *value = (uint8_t)((hi << 4) | lo);
  return RV_OK;
}

static bool file_name_has_extension(const char* file_name, const char* extension) {
  size_t file_name_length = strlen(file_name);
  size_t extension_length = strlen(extension);

  if (file_name_length < extension_length) {
    return false;
  }

  const char* file_extension = file_name + (file_name_length - extension_length);

  for (size_t i = 0; i < extension_length; i++) {
    if (tolower((unsigned char)file_extension[i]) != tolower((unsigned char)extension[i])) {
      return false;
    }
  }

  return true;
}
// List of system devices
device_configuration_t system_devices[] =
  {
    {keyboard_initialise, NULL, NULL, 0x00, 0xbff0, 0x0000, "keyboard"},
    {display_initialise, NULL, NULL, 0x00, 0x0200, 0xbff0, "main display"},
    {display_initialise, NULL, NULL, 0x01, 0x8000, 0x0000, "hires red"},
    {display_initialise, NULL, NULL, 0x02, 0x8000, 0x0000, "hires green"},
    {display_initialise, NULL, NULL, 0x03, 0x8000, 0x0000, "hires blue"},
    {display_initialise, NULL, NULL, 0x04, 0x8000, 0x0000, "hires intensity"},
    {display_initialise, NULL, display_close, 0x00, 0xbf00, 0x0000, "gpu"},
    {via_6522_initialise, via_6522_reset, NULL, 0x00, 0xbfc0, 0x0000, NULL},
    {via_6522_initialise, via_6522_reset, NULL, 0x00, 0xbfe0, 0x0000, NULL},
    {serial_initialise, serial_reset, NULL, 0x00, 0xbfd0, 0xbfd3, NULL},
    {eprom_initialise, NULL, NULL, 0x00, 0xc000, 0x0000, "microtan.rom"},
    {ay8910_initialise, ay8910_reset, ay8910_close, 0x00, 0xbc00, 0xbc03, "ay8910"},
    {invaders_sound_initialise, invaders_sound_reset, invaders_sound_close, 0x00, 0xbc80, 0xbc80, "invaders_sound"},
    {cpu_6502_initialise, cpu_6502_reset, NULL, 0x00, 0x0000, 0x0000, NULL},
    {NULL, NULL, NULL, 0x00, 0x0000, 0x0000, NULL}};

// Register a memory-mapped device
int system_register_memory_mapped_device(uint16_t start, uint16_t end, memory_read_callback read_cb, memory_write_callback write_cb, bool use_main_ram) {
  if (device_count == MAX_DEVICES) {
    printf("system_register_device(%04x, %04x): failed\r\n", start, end);
    return RV_DEVICE_NOT_ADDED;
  }

  devices[device_count].start_address = start;
  devices[device_count].end_address = end;
  devices[device_count].read = read_cb;
  devices[device_count].write = write_cb;
  devices[device_count].use_main_ram = use_main_ram;
  device_count++;
  return RV_OK;
}

// Memory read function
uint8_t system_read_memory(uint16_t address) {
  bool registered = false;
  uint8_t rv = 0;

  for (int i = 0; i < device_count; i++) {
    if ((address >= devices[i].start_address) && (address <= devices[i].end_address)) {
      if (NULL != devices[i].read) {
        rv |= devices[i].read(address);
      }

      if (devices[i].use_main_ram) {
        rv |= system_memory[address];
      }

      registered = true;
    }
  }

  if (registered) {
    return rv;
  }

  return system_memory[address];
}

// Memory write function
void system_write_memory(uint16_t address, uint8_t value) {
  for (int i = 0; i < device_count; i++) {
    if (address >= devices[i].start_address && address <= devices[i].end_address) {
      if (devices[i].use_main_ram) {
        system_memory[address] = value;
      }

      if (NULL != devices[i].write) {
        devices[i].write(address, value);
      }
    }
  }

  if (!memory_read_only[address]) {
    system_memory[address] = value;
  }
}

uint8_t* system_get_memory_pointer(uint16_t address) {
  return system_memory + address;
}

void system_set_read_only(uint16_t start_address, uint16_t end_address) {
  memset(memory_read_only + start_address, 0x01, end_address - start_address + 1);
}

int system_load_m65_file(char* file_name) {
  FILE* m65_file = fopen(file_name, "rb");

  if (!m65_file) {
    printf("Error opening [%s]\r\n", file_name);
    return RV_FILE_OPEN_ERROR;
  }

  fseek(m65_file, 0, SEEK_END);
  size_t m65_file_size = ftell(m65_file);
  fseek(m65_file, 0, SEEK_SET);
  uint8_t* system_memory = system_get_memory_pointer(0x0000);

  if (m65_file_size != 8263) {
    uint8_t ay8910_registers[32];
    uint8_t chunky_state;
    uint16_t file_version;
    uint16_t memory_size;
    read_and_ignore(&file_version, 1, sizeof(file_version), m65_file);
    read_and_ignore(&memory_size, 1, sizeof(memory_size), m65_file);
    read_and_ignore(system_memory, 1, memory_size, m65_file);
    read_and_ignore(system_memory + 0xbfc0, 1, 16, m65_file);
    read_and_ignore(system_memory + 0xbfe0, 1, 16, m65_file);
    read_and_ignore(system_memory + 0xbff0, 1, 16, m65_file);
    read_and_ignore(system_memory + 0xbc04, 1, 1, m65_file);
    read_and_ignore(&chunky_state, 1, 1, m65_file);
    read_and_ignore(ay8910_registers, 1, 32, m65_file);

    if (chunky_state) {
      system_read_memory(0xbff0);
    } else {
      system_write_memory(0xbff3, 0);
    }

    if (file_version >= 1) {
      read_and_ignore(display_get_hires_memory_pointer(0), 1, 8192, m65_file);
      read_and_ignore(display_get_hires_memory_pointer(1), 1, 8192, m65_file);
      read_and_ignore(display_get_hires_memory_pointer(2), 1, 8192, m65_file);
      read_and_ignore(display_get_hires_memory_pointer(3), 1, 8192, m65_file);
    }

    // Write AY8910 registers here
    via_6522_reload();
  } else {
    read_and_ignore(system_memory, 1, 0x2000, m65_file);
  }

  uint8_t chunky_graphics_memory[512];
  uint8_t* chunky = chunky_graphics_memory;
  uint8_t b;

  for (int i = 0; i < 0x40; i++) {
    read_and_ignore(&b, 1, 1, m65_file);

    for (int bit = 0; bit < 8; b >>= 1, bit++) {
      *chunky++ = (b & 1);
    }
  }

  display_load_chunky_memory(chunky_graphics_memory);
  uint8_t status[7];
  read_and_ignore(status, 1, sizeof(status), m65_file);
  fclose(m65_file);
  cpu_6502_continue((uint16_t)status[0] | (uint16_t)status[1] << 8, status[3], status[4], status[5], status[6], status[2]);
  return RV_OK;
}

int system_load_intel_hex_file(char* file_name) {
  FILE* hex_file = fopen(file_name, "r");

  if (!hex_file) {
    printf("Error opening [%s]\r\n", file_name);
    return RV_FILE_OPEN_ERROR;
  }

  uint8_t* memory = system_get_memory_pointer(0x0000);
  uint32_t upper_address = 0;
  int line_number = 0;
  bool saw_eof = false;
  char line[1024];

  while (fgets(line, sizeof(line), hex_file) != NULL) {
    line_number++;

    size_t line_length = strlen(line);
    while ((line_length > 0) && isspace((unsigned char)line[line_length - 1])) {
      line[--line_length] = '\0';
    }

    char* cursor = line;
    while ((*cursor != '\0') && isspace((unsigned char)*cursor)) {
      cursor++;
    }

    if (*cursor == '\0') {
      continue;
    }

    if (*cursor != ':') {
      printf("Intel HEX parse error line %d: missing colon\r\n", line_number);
      fclose(hex_file);
      return RV_INVALID_FILE;
    }

    cursor++;
    uint8_t byte_count;
    uint8_t address_hi;
    uint8_t address_lo;
    uint8_t record_type;

    if ((parse_hex_byte(cursor + 0, &byte_count) != RV_OK) ||
        (parse_hex_byte(cursor + 2, &address_hi) != RV_OK) ||
        (parse_hex_byte(cursor + 4, &address_lo) != RV_OK) ||
        (parse_hex_byte(cursor + 6, &record_type) != RV_OK)) {
      printf("Intel HEX parse error line %d: invalid header\r\n", line_number);
      fclose(hex_file);
      return RV_INVALID_FILE;
    }

    size_t expected_hex_chars = (size_t)(10 + (byte_count * 2));
    if (strlen(cursor) != expected_hex_chars) {
      printf("Intel HEX parse error line %d: invalid line length\r\n", line_number);
      fclose(hex_file);
      return RV_INVALID_FILE;
    }

    uint8_t data[256];
    for (int i = 0; i < byte_count; i++) {
      if (parse_hex_byte(cursor + 8 + (i * 2), &data[i]) != RV_OK) {
        printf("Intel HEX parse error line %d: invalid data\r\n", line_number);
        fclose(hex_file);
        return RV_INVALID_FILE;
      }
    }

    uint8_t checksum;
    if (parse_hex_byte(cursor + 8 + (byte_count * 2), &checksum) != RV_OK) {
      printf("Intel HEX parse error line %d: invalid checksum\r\n", line_number);
      fclose(hex_file);
      return RV_INVALID_FILE;
    }

    uint32_t sum = byte_count + address_hi + address_lo + record_type;
    for (int i = 0; i < byte_count; i++) {
      sum += data[i];
    }

    if ((uint8_t)(sum + checksum) != 0) {
      printf("Intel HEX parse error line %d: checksum mismatch\r\n", line_number);
      fclose(hex_file);
      return RV_INVALID_FILE;
    }

    uint32_t base_address = upper_address + (((uint16_t)address_hi << 8) | address_lo);

    switch (record_type) {
      case 0x00:
        for (int i = 0; i < byte_count; i++) {
          uint32_t target_address = base_address + i;
          if (target_address > 0xffff) {
            printf("Intel HEX parse error line %d: address out of range\r\n", line_number);
            fclose(hex_file);
            return RV_INVALID_FILE;
          }
          memory[target_address] = data[i];
        }
        break;

      case 0x01:
        saw_eof = true;
        break;

      case 0x02:
        if (byte_count != 2) {
          printf("Intel HEX parse error line %d: invalid segment record\r\n", line_number);
          fclose(hex_file);
          return RV_INVALID_FILE;
        }
        upper_address = (((uint32_t)data[0] << 8) | data[1]) << 4;
        break;

      case 0x03:
      case 0x05:
        break;

      case 0x04:
        if (byte_count != 2) {
          printf("Intel HEX parse error line %d: invalid linear record\r\n", line_number);
          fclose(hex_file);
          return RV_INVALID_FILE;
        }
        upper_address = (((uint32_t)data[0] << 8) | data[1]) << 16;
        break;

      default:
        printf("Intel HEX parse error line %d: unsupported record type %02x\r\n", line_number, record_type);
        fclose(hex_file);
        return RV_INVALID_FILE;
    }

    if (saw_eof) {
      break;
    }
  }

  fclose(hex_file);

  if (!saw_eof) {
    printf("Intel HEX warning: no EOF record in [%s]\r\n", file_name);
  }

  return RV_OK;
}

int system_load_program_file(char* file_name) {
  if (file_name_has_extension(file_name, ".hex") ||
      file_name_has_extension(file_name, ".ihx") ||
      file_name_has_extension(file_name, ".ihex")) {
    return system_load_intel_hex_file(file_name);
  }

  if (file_name_has_extension(file_name, ".m65")) {
    return system_load_m65_file(file_name);
  }

  FILE* file = fopen(file_name, "r");
  if (file) {
    int first_non_whitespace = fgetc(file);
    while ((first_non_whitespace != EOF) && isspace(first_non_whitespace)) {
      first_non_whitespace = fgetc(file);
    }
    fclose(file);

    if (first_non_whitespace == ':') {
      return system_load_intel_hex_file(file_name);
    }
  }

  return system_load_m65_file(file_name);
}

int system_save_m65_file(char* file_name) {
  FILE* m65_file = fopen(file_name, "wb");

  if (!m65_file) {
    printf("Error opening [%s]\r\n", file_name);
    return RV_FILE_OPEN_ERROR;
  }

  uint8_t* memory = system_get_memory_pointer(0x0000);
  uint8_t chunky_graphics_memory[64];
  uint8_t status[7];
  uint16_t pc = cpu_6502_get_pc();

  display_save_chunky_memory(chunky_graphics_memory);
  status[0] = (uint8_t)(pc & 0xFF);
  status[1] = (uint8_t)(pc >> 8);
  status[2] = cpu_6502_get_psw();
  status[3] = cpu_6502_get_a();
  status[4] = cpu_6502_get_x();
  status[5] = cpu_6502_get_y();
  status[6] = cpu_6502_get_sp();

  bool ok = write_and_check(memory, 1, 0x2000, m65_file) &&
            write_and_check(chunky_graphics_memory, 1, sizeof(chunky_graphics_memory), m65_file) &&
            write_and_check(status, 1, sizeof(status), m65_file);

  fclose(m65_file);

  if (!ok) {
    return RV_FILE_READ_ERROR;
  }

  return RV_OK;
}

int system_save_intel_hex_range(char* file_name, uint16_t start_address, uint16_t end_address) {
  if (start_address > end_address) {
    return RV_INVALID_FILE;
  }

  FILE* hex_file = fopen(file_name, "w");

  if (!hex_file) {
    printf("Error opening [%s]\r\n", file_name);
    return RV_FILE_OPEN_ERROR;
  }

  uint8_t* memory = system_get_memory_pointer(0x0000);
  uint32_t current = start_address;

  while (current <= end_address) {
    uint8_t byte_count = (uint8_t)((end_address - current + 1) > 16 ? 16 : (end_address - current + 1));
    uint8_t record[4 + 16];

    record[0] = byte_count;
    record[1] = (uint8_t)(current >> 8);
    record[2] = (uint8_t)(current & 0xFF);
    record[3] = 0x00;

    for (int i = 0; i < byte_count; i++) {
      record[4 + i] = memory[current + i];
    }

    fprintf(hex_file, ":%02X%04X00", byte_count, (unsigned int)current & 0xFFFF);

    for (int i = 0; i < byte_count; i++) {
      fprintf(hex_file, "%02X", record[4 + i]);
    }

    fprintf(hex_file, "%02X\n", intel_hex_checksum(record, (size_t)(4 + byte_count)));
    current += byte_count;
  }

  fprintf(hex_file, ":00000001FF\n");
  fclose(hex_file);
  return RV_OK;
}

void system_reset() {
  device_configuration_ptr_t device = system_devices;

  while (NULL != device->initialiser) {
    if (NULL != device->reset) {
      device->reset(device->bank, device->address);
    }

    device++;
  }
}

int system_initialise() {
  memset(memory_read_only, 0, sizeof(memory_read_only));
  device_configuration_ptr_t device = system_devices;

  while (NULL != device->initialiser) {
    device->initialiser(device->bank, device->address, device->param, device->identifier);
    device++;
  }

  system_reset();
  return RV_OK;
}

void system_close() {
  device_configuration_ptr_t device = system_devices;

  while (NULL != device->initialiser) {
    if (NULL != device->close) {
      device->close();
    }

    device++;
  }
}







