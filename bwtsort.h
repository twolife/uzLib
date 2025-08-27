#pragma once

extern "C" {

typedef struct {
    unsigned prefix, offset;
} KeyPrefix;

KeyPrefix* bwtsort (unsigned char *buff, unsigned size);

}