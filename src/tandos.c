#include "tandos.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_6502.h"
#include "external_filenames.h"
#include "function_return_codes.h"
#include "system.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TANDOS_ROM_BASE         0xA800
#define TANDOS_ROM_END          0xB7FF
#define TANDOS_RAM_BASE         0xB800
#define TANDOS_RAM_END          0xBBFF
#define TANDOS_FDC_BASE         0xBF90
#define TANDOS_FDC_END          0xBF94
#define TANDOS_ROM_SIZE         4096
#define TANDOS_RAM_SIZE         1024
#define TANDOS_SECTOR_SIZE      256
#define TANDOS_MIN_TRACKS       35
#define TANDOS_MAX_TRACKS       80
#define TANDOS_DEFAULT_TRACKS   80
#define TANDOS_DEFAULT_SECTORS  10

#define FDC_STATUS_BUSY             0x01
#define FDC_STATUS_DRQ              0x02
#define FDC_STATUS_TRACK_ZERO       0x04
#define FDC_STATUS_RECORD_NOT_FOUND 0x10
#define FDC_STATUS_WRITE_PROTECT    0x40
#define FDC_STATUS_NOT_READY        0x80

#define TANDOS_STATUS_INTRQ 0x01
#define TANDOS_STATUS_HLD   0x40
#define TANDOS_STATUS_DRQ   0x80

typedef struct {
  uint8_t* data;
  FILE* file;
  size_t size;
  int tracks;
  int sectors_per_track;
  bool write_protected;
  char file_name[PATH_MAX];
} tandos_disk_t;

typedef enum {
  FDC_TRANSFER_NONE,
  FDC_TRANSFER_READ,
  FDC_TRANSFER_WRITE,
  FDC_TRANSFER_READ_ADDRESS
} fdc_transfer_t;

static bool enabled;
static uint8_t rom[TANDOS_ROM_SIZE];
static uint8_t ram[TANDOS_RAM_SIZE];
static tandos_disk_t units[TANDOS_UNIT_COUNT];

static uint8_t fdc_status;
static uint8_t fdc_track;
static uint8_t fdc_sector;
static uint8_t fdc_data;
static uint8_t control;
static bool intrq;
static bool drq;
static bool head_loaded;
static fdc_transfer_t transfer;
static uint8_t transfer_buffer[TANDOS_SECTOR_SIZE];
static size_t transfer_position;
static size_t transfer_length;
static uint32_t completion_cycles;

static bool valid_unit(int unit) {
  return (unit >= 0) && (unit < TANDOS_UNIT_COUNT);
}

static int selected_unit(void) {
  int drive = (control >> 2) & 0x03;
  int side = (control >> 4) & 0x01;
  return drive * 2 + side;
}

static void set_intrq(bool state) {
  intrq = state;
  if (state && (control & 0x01)) {
    cpu_6502_assert_irq();
  }
}

static void finish_command(void) {
  fdc_status &= (uint8_t)~(FDC_STATUS_BUSY | FDC_STATUS_DRQ);
  drq = false;
  transfer = FDC_TRANSFER_NONE;
  transfer_position = 0;
  transfer_length = 0;
  // Let the 6502 store the final data byte before the completion IRQ.
  completion_cycles = 32;
}

static int flush_sector(int unit, uint8_t* sector) {
  if (!valid_unit(unit) || !units[unit].file || units[unit].write_protected ||
      (sector < units[unit].data) ||
      (sector + TANDOS_SECTOR_SIZE > units[unit].data + units[unit].size)) {
    return RV_FILE_OPEN_ERROR;
  }

  long offset = (long)(sector - units[unit].data);
  if ((fseek(units[unit].file, offset, SEEK_SET) != 0) ||
      (fwrite(sector, 1, TANDOS_SECTOR_SIZE, units[unit].file) != TANDOS_SECTOR_SIZE) ||
      (fflush(units[unit].file) != 0)) {
    return RV_FILE_READ_ERROR;
  }
  return RV_OK;
}

static bool infer_geometry(size_t size, int* tracks, int* sectors_per_track) {
  const int sector_counts[] = {10, 9};

  for (size_t i = 0; i < sizeof(sector_counts) / sizeof(sector_counts[0]); i++) {
    size_t bytes_per_track = (size_t)sector_counts[i] * TANDOS_SECTOR_SIZE;
    if ((size % bytes_per_track) == 0) {
      size_t track_count = size / bytes_per_track;
      if ((track_count >= TANDOS_MIN_TRACKS) && (track_count <= TANDOS_MAX_TRACKS)) {
        *tracks = (int)track_count;
        *sectors_per_track = sector_counts[i];
        return true;
      }
    }
  }

  return false;
}

static uint8_t* sector_pointer(int unit, uint8_t track, uint8_t sector) {
  if (!valid_unit(unit) || !units[unit].data ||
      (track >= units[unit].tracks) || (sector == 0) ||
      (sector > units[unit].sectors_per_track)) {
    return NULL;
  }

  size_t index = ((size_t)track * units[unit].sectors_per_track) + (sector - 1);
  return units[unit].data + index * TANDOS_SECTOR_SIZE;
}

static void begin_read_sector(void) {
  int unit = selected_unit();
  head_loaded = true;
  uint8_t* source = sector_pointer(unit, fdc_track, fdc_sector);

  fdc_status = FDC_STATUS_BUSY;
  set_intrq(false);
  if (!units[unit].data) {
    fdc_status = FDC_STATUS_NOT_READY | FDC_STATUS_RECORD_NOT_FOUND;
    finish_command();
    return;
  }
  if (!source) {
    fdc_status = FDC_STATUS_RECORD_NOT_FOUND;
    finish_command();
    return;
  }

  memcpy(transfer_buffer, source, TANDOS_SECTOR_SIZE);
  transfer = FDC_TRANSFER_READ;
  transfer_position = 0;
  transfer_length = TANDOS_SECTOR_SIZE;
  drq = true;
  fdc_status |= FDC_STATUS_DRQ;
}

static void begin_write_sector(void) {
  int unit = selected_unit();
  head_loaded = true;

  fdc_status = FDC_STATUS_BUSY;
  set_intrq(false);
  if (!units[unit].data) {
    fdc_status = FDC_STATUS_NOT_READY | FDC_STATUS_RECORD_NOT_FOUND;
    finish_command();
    return;
  }
  if (units[unit].write_protected) {
    fdc_status = FDC_STATUS_WRITE_PROTECT;
    finish_command();
    return;
  }
  if (!sector_pointer(unit, fdc_track, fdc_sector)) {
    fdc_status = FDC_STATUS_RECORD_NOT_FOUND;
    finish_command();
    return;
  }

  transfer = FDC_TRANSFER_WRITE;
  transfer_position = 0;
  transfer_length = TANDOS_SECTOR_SIZE;
  drq = true;
  fdc_status |= FDC_STATUS_DRQ;
}

static void begin_read_address(void) {
  int unit = selected_unit();
  head_loaded = true;

  fdc_status = FDC_STATUS_BUSY;
  set_intrq(false);
  if (!units[unit].data) {
    fdc_status = FDC_STATUS_NOT_READY | FDC_STATUS_RECORD_NOT_FOUND;
    finish_command();
    return;
  }

  transfer_buffer[0] = fdc_track;
  transfer_buffer[1] = (uint8_t)(unit & 1);
  transfer_buffer[2] = fdc_sector ? fdc_sector : 1;
  transfer_buffer[3] = 1; // 256-byte sector size code.
  transfer_buffer[4] = 0;
  transfer_buffer[5] = 0;
  transfer = FDC_TRANSFER_READ_ADDRESS;
  transfer_position = 0;
  transfer_length = 6;
  drq = true;
  fdc_status |= FDC_STATUS_DRQ;
}

static void execute_type_one(uint8_t command) {
  int unit = selected_unit();
  bool ready = units[unit].data != NULL;

  fdc_status = 0;
  set_intrq(false);
  transfer = FDC_TRANSFER_NONE;
  drq = false;
  head_loaded = (command & 0x08) != 0;

  switch (command & 0xF0) {
    case 0x00: // Restore.
      fdc_track = 0;
      break;
    case 0x10: // Seek.
      fdc_track = fdc_data;
      break;
    case 0x40: // Step in.
    case 0x50:
      if (fdc_track < 0xFF) {
        fdc_track++;
      }
      break;
    case 0x60: // Step out.
    case 0x70:
      if (fdc_track > 0) {
        fdc_track--;
      }
      break;
    default: // Step retains the previous direction; TANDOS does not depend on it.
      break;
  }

  if (fdc_track == 0) {
    fdc_status |= FDC_STATUS_TRACK_ZERO;
  }
  if (!ready) {
    fdc_status |= FDC_STATUS_NOT_READY;
  }
  // A real seek/restore completes after the ROM has installed its IRQ link.
  completion_cycles = 512;
}

static void write_command(uint8_t command) {
  set_intrq(false);
  completion_cycles = 0;

  switch (command & 0xF0) {
    case 0x80:
    case 0x90:
      begin_read_sector();
      break;
    case 0xA0:
    case 0xB0:
      begin_write_sector();
      break;
    case 0xC0:
      begin_read_address();
      break;
    case 0xD0: // Force interrupt.
      fdc_status = 0;
      drq = false;
      transfer = FDC_TRANSFER_NONE;
      set_intrq((command & 0x0F) != 0);
      break;
    case 0xE0: // Read track is not used by normal TANDOS file operations.
    case 0xF0: // Write track/format support is deliberately reported unavailable.
      fdc_status = FDC_STATUS_RECORD_NOT_FOUND;
      finish_command();
      break;
    default:
      execute_type_one(command);
      break;
  }
}

static uint8_t read_data_register(void) {
  if ((transfer != FDC_TRANSFER_READ) &&
      (transfer != FDC_TRANSFER_READ_ADDRESS)) {
    return fdc_data;
  }

  fdc_data = transfer_buffer[transfer_position++];
  if (transfer_position >= transfer_length) {
    finish_command();
  }
  return fdc_data;
}

static void write_data_register(uint8_t value) {
  fdc_data = value;
  if (transfer != FDC_TRANSFER_WRITE) {
    return;
  }

  transfer_buffer[transfer_position++] = value;
  if (transfer_position >= transfer_length) {
    int unit = selected_unit();
    uint8_t* destination = sector_pointer(unit, fdc_track, fdc_sector);
    if (!destination) {
      fdc_status = FDC_STATUS_RECORD_NOT_FOUND;
    } else {
      memcpy(destination, transfer_buffer, TANDOS_SECTOR_SIZE);
      if (flush_sector(unit, destination) != RV_OK) {
        fdc_status = FDC_STATUS_WRITE_PROTECT;
      }
    }
    finish_command();
  }
}

static uint8_t memory_read(uint16_t address) {
  if (!enabled) {
    return *system_get_memory_pointer(address);
  }
  if (address < 0xB000) {
    // U17 does not see A12: DBASIC is in the physical upper half of the 2732.
    return rom[0x0800 + address - TANDOS_ROM_BASE];
  }
  if (address <= TANDOS_ROM_END) {
    return rom[address - 0xB000];
  }
  return ram[address - TANDOS_RAM_BASE];
}

static void memory_write(uint16_t address, uint8_t value) {
  if (enabled && (address >= TANDOS_RAM_BASE)) {
    ram[address - TANDOS_RAM_BASE] = value;
  }
}

static uint8_t io_read(uint16_t address) {
  if (!enabled) {
    return *system_get_memory_pointer(address);
  }

  switch (address - TANDOS_FDC_BASE) {
    case 0:
      set_intrq(false);
      return fdc_status;
    case 1:
      return fdc_track;
    case 2:
      return fdc_sector;
    case 3:
      return read_data_register();
    case 4:
      return (uint8_t)((control & 0x1C) |
                       (intrq ? TANDOS_STATUS_INTRQ : 0) |
                       (head_loaded ? TANDOS_STATUS_HLD : 0) |
                       (drq ? TANDOS_STATUS_DRQ : 0));
    default:
      return 0;
  }
}

static void io_write(uint16_t address, uint8_t value) {
  if (!enabled) {
    return;
  }

  switch (address - TANDOS_FDC_BASE) {
    case 0:
      write_command(value);
      break;
    case 1:
      fdc_track = value;
      break;
    case 2:
      fdc_sector = value;
      break;
    case 3:
      write_data_register(value);
      break;
    case 4:
      control = value;
      if (intrq && (control & 0x01)) {
        cpu_6502_assert_irq();
      }
      break;
  }
}

int tandos_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier) {
  (void)bank;
  (void)address;
  (void)param;
  (void)identifier;

  FILE* file = fopen(TANDOS_ROM_FILENAME, "rb");
  if (!file) {
    fprintf(stderr, "Warning: unable to open TANDOS ROM [%s]\n", TANDOS_ROM_FILENAME);
    memset(rom, 0xFF, sizeof(rom));
  } else {
    size_t bytes_read = fread(rom, 1, sizeof(rom), file);
    fclose(file);
    if (bytes_read != sizeof(rom)) {
      fprintf(stderr, "Warning: TANDOS ROM has the wrong size\n");
      memset(rom, 0xFF, sizeof(rom));
    }
  }

  int rv = system_register_memory_mapped_device(
    TANDOS_ROM_BASE, TANDOS_RAM_END, memory_read, memory_write, false);
  if (rv != RV_OK) {
    return rv;
  }
  return system_register_memory_mapped_device(
    TANDOS_FDC_BASE, TANDOS_FDC_END, io_read, io_write, false);
}

void tandos_reset(uint8_t bank, uint16_t address) {
  (void)bank;
  (void)address;
  memset(ram, 0, sizeof(ram));
  fdc_status = FDC_STATUS_TRACK_ZERO;
  fdc_track = 0;
  fdc_sector = 1;
  fdc_data = 0;
  control = 0x20;
  intrq = false;
  drq = false;
  head_loaded = false;
  transfer = FDC_TRANSFER_NONE;
  transfer_position = 0;
  transfer_length = 0;
  completion_cycles = 0;
}

void tandos_close(void) {
  tandos_eject_all();
}

void tandos_update(uint32_t elapsed_cycles) {
  if (completion_cycles == 0) {
    return;
  }

  if (elapsed_cycles >= completion_cycles) {
    completion_cycles = 0;
    set_intrq(true);
  } else {
    completion_cycles -= elapsed_cycles;
  }
}

void tandos_set_enabled(bool new_enabled) {
  enabled = new_enabled;
}

bool tandos_get_enabled(void) {
  return enabled;
}

int tandos_mount(int unit, const char* file_name, bool write_protected) {
  if (!valid_unit(unit) || !file_name || !*file_name) {
    return RV_INVALID_FILE;
  }

  FILE* file = fopen(file_name, write_protected ? "rb" : "r+b");
  if (!file) {
    return RV_FILE_OPEN_ERROR;
  }

  if ((fseek(file, 0, SEEK_END) != 0)) {
    fclose(file);
    return RV_FILE_READ_ERROR;
  }
  long file_size = ftell(file);
  if ((file_size <= 0) || (fseek(file, 0, SEEK_SET) != 0)) {
    fclose(file);
    return RV_INVALID_FILE;
  }

  int tracks;
  int sectors_per_track;
  if (!infer_geometry((size_t)file_size, &tracks, &sectors_per_track)) {
    fclose(file);
    return RV_INVALID_FILE;
  }

  uint8_t* data = malloc((size_t)file_size);
  if (!data) {
    fclose(file);
    return RV_MEMORY_ALLOCATION_FAILURE;
  }
  if (fread(data, 1, (size_t)file_size, file) != (size_t)file_size) {
    fclose(file);
    free(data);
    return RV_FILE_READ_ERROR;
  }

  tandos_eject(unit);
  units[unit].data = data;
  units[unit].file = file;
  units[unit].size = (size_t)file_size;
  units[unit].tracks = tracks;
  units[unit].sectors_per_track = sectors_per_track;
  units[unit].write_protected = write_protected;
  snprintf(units[unit].file_name, sizeof(units[unit].file_name), "%s", file_name);
  return RV_OK;
}

void tandos_eject(int unit) {
  if (!valid_unit(unit)) {
    return;
  }
  if (units[unit].file) {
    fclose(units[unit].file);
  }
  free(units[unit].data);
  memset(&units[unit], 0, sizeof(units[unit]));
}

void tandos_eject_all(void) {
  for (int unit = 0; unit < TANDOS_UNIT_COUNT; unit++) {
    tandos_eject(unit);
  }
}

bool tandos_unit_mounted(int unit) {
  return valid_unit(unit) && units[unit].data;
}

bool tandos_unit_write_protected(int unit) {
  return valid_unit(unit) && units[unit].write_protected;
}

const char* tandos_unit_file_name(int unit) {
  return valid_unit(unit) ? units[unit].file_name : "";
}

int tandos_unit_tracks(int unit) {
  return valid_unit(unit) ? units[unit].tracks : 0;
}

int tandos_unit_sectors_per_track(int unit) {
  return valid_unit(unit) ? units[unit].sectors_per_track : 0;
}

int tandos_create_image(const char* file_name, int tracks, int sectors_per_track) {
  if (!file_name || !*file_name ||
      (tracks < TANDOS_MIN_TRACKS) || (tracks > TANDOS_MAX_TRACKS) ||
      ((sectors_per_track != 9) && (sectors_per_track != 10))) {
    return RV_INVALID_FILE;
  }

  FILE* file = fopen(file_name, "wb");
  if (!file) {
    return RV_FILE_OPEN_ERROR;
  }

  uint8_t sector[TANDOS_SECTOR_SIZE];
  memset(sector, 0xF6, sizeof(sector));
  size_t sector_count = (size_t)tracks * sectors_per_track;
  bool ok = true;
  for (size_t i = 0; i < sector_count; i++) {
    if (fwrite(sector, 1, sizeof(sector), file) != sizeof(sector)) {
      ok = false;
      break;
    }
  }
  if (fclose(file) != 0) {
    ok = false;
  }
  return ok ? RV_OK : RV_FILE_READ_ERROR;
}

void tandos_save_settings(FILE* file) {
  if (!file) {
    return;
  }

  fprintf(file, "tandos_enabled=%d\n", enabled ? 1 : 0);
  for (int unit = 0; unit < TANDOS_UNIT_COUNT; unit++) {
    if (units[unit].data) {
      fprintf(file, "tandos_unit%d=%c:%s\n", unit,
              units[unit].write_protected ? 'r' : 'w',
              units[unit].file_name);
    }
  }
}

void tandos_load_settings(FILE* file) {
  char line[PATH_MAX + 64];

  if (!file) {
    return;
  }
  while (fgets(line, sizeof(line), file)) {
    size_t length = strlen(line);
    while (length && ((line[length - 1] == '\n') || (line[length - 1] == '\r'))) {
      line[--length] = '\0';
    }

    int enabled_value;
    if (sscanf(line, "tandos_enabled=%d", &enabled_value) == 1) {
      enabled = enabled_value != 0;
      continue;
    }

    int unit;
    char mode;
    int path_offset = 0;
    if ((sscanf(line, "tandos_unit%d=%c:%n", &unit, &mode, &path_offset) == 2) &&
        valid_unit(unit) && (path_offset > 0) && line[path_offset]) {
      (void)tandos_mount(unit, line + path_offset, mode == 'r');
    }
  }
}
