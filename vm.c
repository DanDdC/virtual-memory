#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PAGE_SIZE 256
#define PAGE_COUNT 256
#define BACKING_STORE_SIZE (PAGE_COUNT * PAGE_SIZE)
#define BACKING_STORE_PATH "BACKING_STORE.bin"

#ifndef FRAME_COUNT
#define FRAME_COUNT 128
#endif

#if FRAME_COUNT > PAGE_COUNT
#error "FRAME_COUNT cannot exceed PAGE_COUNT"
#endif

enum Policy {
    POLICY_FIFO,
    POLICY_LRU
};

typedef struct {
    int *values;
    size_t count;
    size_t capacity;
} AddressList;

typedef struct {
    int frame;
    bool valid;
} PageTableEntry;

typedef struct {
    int page;
    bool occupied;
    unsigned long last_used;
    signed char data[PAGE_SIZE];
} Frame;

typedef struct {
    unsigned char backing_store[BACKING_STORE_SIZE];
    PageTableEntry page_table[PAGE_COUNT];
    Frame frames[FRAME_COUNT];
    enum Policy page_policy;
    size_t next_free_frame;
    size_t fifo_next_frame;
    unsigned long tick;
    unsigned long page_faults;
} VirtualMemory;

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s <addresses_file> <page_algorithm> <tlb_algorithm>\n", program);
    fprintf(stderr, "Algorithms: fifo, lru\n");
}

static bool parse_policy(const char *text, enum Policy *policy)
{
    if (strcasecmp(text, "fifo") == 0) {
        *policy = POLICY_FIFO;
        return true;
    }

    if (strcasecmp(text, "lru") == 0) {
        *policy = POLICY_LRU;
        return true;
    }

    return false;
}

static void free_addresses(AddressList *addresses)
{
    free(addresses->values);
    addresses->values = NULL;
    addresses->count = 0;
    addresses->capacity = 0;
}

static bool append_address(AddressList *addresses, int value)
{
    if (addresses->count == addresses->capacity) {
        size_t new_capacity = addresses->capacity == 0 ? 1024 : addresses->capacity * 2;

        if (new_capacity < addresses->capacity || new_capacity > SIZE_MAX / sizeof *addresses->values) {
            fprintf(stderr, "error: too many logical addresses\n");
            return false;
        }

        int *new_values = realloc(addresses->values, new_capacity * sizeof *new_values);
        if (new_values == NULL) {
            fprintf(stderr, "error: out of memory while storing logical addresses\n");
            return false;
        }

        addresses->values = new_values;
        addresses->capacity = new_capacity;
    }

    addresses->values[addresses->count] = value;
    addresses->count++;
    return true;
}

static bool read_addresses(const char *path, AddressList *addresses)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "error: cannot open address file '%s': %s\n", path, strerror(errno));
        return false;
    }

    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_number = 0;
    bool success = true;

    while (getline(&line, &line_capacity, file) != -1) {
        line_number++;

        char *cursor = line;
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }

        char *end = NULL;
        errno = 0;
        long value = strtol(cursor, &end, 10);

        if (end == cursor || errno == ERANGE || value < 0 || value > 65535) {
            fprintf(stderr, "error: %s:%zu: invalid logical address\n", path, line_number);
            success = false;
            break;
        }

        while (isspace((unsigned char)*end)) {
            end++;
        }

        if (*end != '\0') {
            fprintf(stderr, "error: %s:%zu: invalid logical address\n", path, line_number);
            success = false;
            break;
        }

        if (!append_address(addresses, (int)value)) {
            success = false;
            break;
        }
    }

    if (success && !feof(file)) {
        fprintf(stderr, "error: failed to read address file '%s'\n", path);
        success = false;
    }

    if (success && addresses->count == 0) {
        fprintf(stderr, "error: address file '%s' contains no logical addresses\n", path);
        success = false;
    }

    free(line);

    if (fclose(file) != 0) {
        fprintf(stderr, "error: failed to close address file '%s': %s\n", path, strerror(errno));
        success = false;
    }

    if (!success) {
        free_addresses(addresses);
    }

    return success;
}

static bool load_backing_store(const char *path, unsigned char *store)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "error: cannot open backing store '%s': %s\n", path, strerror(errno));
        return false;
    }

    bool success = true;
    size_t bytes_read = fread(store, 1, BACKING_STORE_SIZE, file);

    if (bytes_read != BACKING_STORE_SIZE) {
        if (ferror(file)) {
            fprintf(stderr, "error: failed to read backing store '%s'\n", path);
        } else {
            fprintf(stderr, "error: backing store '%s' must be exactly %d bytes\n", path, BACKING_STORE_SIZE);
        }
        success = false;
    } else {
        int extra = fgetc(file);

        if (extra != EOF) {
            fprintf(stderr, "error: backing store '%s' must be exactly %d bytes\n", path, BACKING_STORE_SIZE);
            success = false;
        } else if (ferror(file)) {
            fprintf(stderr, "error: failed to read backing store '%s'\n", path);
            success = false;
        }
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "error: failed to close backing store '%s': %s\n", path, strerror(errno));
        success = false;
    }

    return success;
}

static void initialize_memory(VirtualMemory *memory, enum Policy page_policy)
{
    memory->page_policy = page_policy;
    memory->next_free_frame = 0;
    memory->fifo_next_frame = 0;
    memory->tick = 0;
    memory->page_faults = 0;

    for (size_t i = 0; i < PAGE_COUNT; i++) {
        memory->page_table[i].frame = 0;
        memory->page_table[i].valid = false;
    }

    for (size_t i = 0; i < FRAME_COUNT; i++) {
        memory->frames[i].page = 0;
        memory->frames[i].occupied = false;
        memory->frames[i].last_used = 0;
    }
}

static size_t select_victim_frame(VirtualMemory *memory)
{
    size_t victim = memory->fifo_next_frame;
    memory->fifo_next_frame = (memory->fifo_next_frame + 1) % FRAME_COUNT;
    return victim;
}

static void translate_address(VirtualMemory *memory, int virtual_address, size_t *physical_address, int *value)
{
    size_t page = (size_t)virtual_address / PAGE_SIZE;
    size_t offset = (size_t)virtual_address % PAGE_SIZE;
    size_t frame;

    memory->tick++;

    if (memory->page_table[page].valid) {
        frame = (size_t)memory->page_table[page].frame;
        memory->frames[frame].last_used = memory->tick;
    } else {
        memory->page_faults++;

        if (memory->next_free_frame < FRAME_COUNT) {
            frame = memory->next_free_frame;
            memory->next_free_frame++;
        } else {
            frame = select_victim_frame(memory);
        }

        memory->frames[frame].page = (int)page;
        memory->frames[frame].occupied = true;
        memory->frames[frame].last_used = memory->tick;
        memcpy(memory->frames[frame].data, memory->backing_store + page * PAGE_SIZE, PAGE_SIZE);

        memory->page_table[page].frame = (int)frame;
        memory->page_table[page].valid = true;
    }

    *physical_address = frame * PAGE_SIZE + offset;
    *value = memory->frames[frame].data[offset];
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    enum Policy page_policy;
    enum Policy tlb_policy;

    if (!parse_policy(argv[2], &page_policy)) {
        fprintf(stderr, "error: invalid page algorithm '%s' (expected fifo or lru)\n", argv[2]);
        return EXIT_FAILURE;
    }

    if (!parse_policy(argv[3], &tlb_policy)) {
        fprintf(stderr, "error: invalid TLB algorithm '%s' (expected fifo or lru)\n", argv[3]);
        return EXIT_FAILURE;
    }

    if (page_policy == POLICY_LRU) {
        fprintf(stderr, "error: LRU page replacement is not implemented yet\n");
        return EXIT_FAILURE;
    }

    AddressList addresses = {0};
    if (!read_addresses(argv[1], &addresses)) {
        return EXIT_FAILURE;
    }

    VirtualMemory *memory = calloc(1, sizeof *memory);
    if (memory == NULL) {
        fprintf(stderr, "error: out of memory while initializing virtual memory\n");
        free_addresses(&addresses);
        return EXIT_FAILURE;
    }

    initialize_memory(memory, page_policy);

    if (!load_backing_store(BACKING_STORE_PATH, memory->backing_store)) {
        free(memory);
        free_addresses(&addresses);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < addresses.count; i++) {
        size_t physical_address;
        int value;

        translate_address(memory, addresses.values[i], &physical_address, &value);
        (void)physical_address;
        (void)value;
    }

    (void)tlb_policy;

    free(memory);
    free_addresses(&addresses);
    return EXIT_SUCCESS;
}
