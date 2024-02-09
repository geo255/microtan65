#include "eprom.h"
#include "function_return_codes.h"
#include "system.h"

int eprom_load(char *file_name, uint16_t address)
{
    uint8_t *memory_ptr = system_get_memory_pointer(address);
    FILE *eprom_file = fopen(file_name, "rb");

    if (!eprom_file)
    {
        printf("Error opening [%s]\r\n", file_name);
        return RV_FILE_OPEN_ERROR;
    }

    fseek(eprom_file, 0, SEEK_END);
    size_t eprom_file_size = ftell(eprom_file);
    fseek(eprom_file, 0, SEEK_SET);

    if (((uint32_t)address + (uint32_t)eprom_file_size - 1) > 0xFFFF)
    {
        fclose(eprom_file);
        printf("File [%s] too large\r\n", file_name);
        return RV_INVALID_FILE;
    }

    size_t bytes_read = fread(memory_ptr, 1, eprom_file_size, eprom_file);
    fclose(eprom_file);

    if (bytes_read != eprom_file_size)
    {
        printf("Error reading [%s] (read %lu, expect %lu)\r\n", file_name, bytes_read, eprom_file_size);
        return RV_FILE_READ_ERROR;
    }

    system_set_read_only(address, address + eprom_file_size - 1);
    return RV_OK;
}


int eprom_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier)
{
    int rv;
    rv = eprom_load("microtan.rom", 0xC000);

    if (rv != RV_OK)
    {
        return rv;
    }

    return RV_OK;
}

