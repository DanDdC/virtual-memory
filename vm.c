#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

enum Policy {
    POLICY_FIFO,
    POLICY_LRU
};

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

    (void)page_policy;
    (void)tlb_policy;

    return EXIT_SUCCESS;
}
