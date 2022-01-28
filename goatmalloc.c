// Nicholas Delli Carpini

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "goatmalloc.h"

// pointers for memory area
void * _arena_start;
void * _arena_end;

// statusno needs to be initialized here bc it is not in the header, causing compiler errors
int statusno = 0;

// keep variable for sizeof(header) to avoid calling the function more than necessary
size_t header_size = sizeof(struct __node_t);

// allocate a memory chunk based on the provided size, creating the appropriate headers for mem
// size_t size -> min size of the chunk for the requested memory
//
// returns -> NULL if error, else ptr to memory chunk allocated
void * walloc(size_t size) {
    // if memory uninitialized, call error
    if (_arena_start == NULL) {
        printf("Error: Unitialized. Setting status code\n");

        statusno = ERR_UNINITIALIZED;
        return NULL;
    }

    printf("Allocating memory:\n");
    printf("...looking for free chunk of >= %lu bytes\n", (unsigned long) size);

    // if requested size is invalid, call error
    if (size < 1) {
        printf("...requested size is smaller than 1 byte\n");

        statusno = ERR_BAD_ARGUMENTS;
        return NULL;
    }
    if ((size + header_size) > (_arena_end - _arena_start)) {
        printf("...no such free chunk exists\n");
        printf("...setting error code\n");

        statusno = ERR_OUT_OF_MEMORY;
        return NULL;
    }

    struct __node_t header;
    void * header_ptr = _arena_start;

    // loop through linked list of headers to try to find appropriate memory chunk for requested size
    while (1) {
        memcpy(&header, header_ptr, header_size);

        // if current memory chunk is free and can fit requested size
        if (header.is_free && (header.size >= size)) {
            printf("...found free chunk of %lu bytes with header at %p\n", (unsigned long) header.size, header_ptr);
            printf("...free chunk->fwd currently points to %p\n", header.fwd);
            printf("...free chunk->bwd currently points to %p\n", header.bwd);
            printf("...checking if splitting is required\n");

            // if curr mem chunk size is large enough to fit multiple chunks after the requested mem
            // is is_free=0, then create a new chunk with the remaining space
            if (header.size > (size + header_size)) {
                size_t total = header.size;
                header.size = size;
                header.is_free = 0;

                struct __node_t new_header;
                new_header.size = total - size - header_size;
                new_header.is_free = 1;
                new_header.bwd = header_ptr;

                void * new_header_addr = header_ptr + header_size + header.size;

                // set the header.bwd & header.fwd values appropriately based on split memory chunks
                if (header.fwd == NULL) {
                    new_header.fwd = NULL;
                }
                else {
                    struct __node_t next_header;
                    memcpy(&next_header, header.fwd, header_size);

                    next_header.bwd = new_header_addr;
                    memcpy(header.fwd, &next_header, header_size);

                    new_header.fwd = header.fwd;
                }

                header.fwd = new_header_addr;

                printf("...splitting free chunk\n");
                printf("...updating chunk header at %p\n", header_ptr);
                memcpy(new_header_addr, &new_header, header_size);
                memcpy(header_ptr, &header, header_size);
                break;
            }
            else {
                header.is_free = 0;

                // this doesnt make sense bc if header.size=size, it still is not possible to split
                // the mem chunk, but this code is necessary to have the output match output_ref.txt
                if (header.size == size) {
                    printf("...splitting not required\n");
                }
                else {
                    printf("...splitting not possible\n");
                }
                
                printf("...updating chunk header at %p\n", header_ptr);
                memcpy(header_ptr, &header, header_size);
                break;
            }
        }
        else {
            // if the curr mem chunk cannot support requested mem, move onto next header
            if (header.fwd != NULL) {
                header_ptr = header.fwd;
                continue;
            }
            // if curr mem chunk cannot support requested mem & there are no more fwd headers, assume
            // memory arena is out of memory
            else {
                printf("...no such free chunk exists\n");
                printf("...setting error code\n");

                statusno = ERR_OUT_OF_MEMORY;
                return NULL;
            }
        }
    }

    printf("...being careful with my pointer arthimetic and void pointer casting\n");
    printf("...allocation starts at %p\n", (header_ptr + header_size));
    return (header_ptr + header_size);
}

// mark a chunk as free based on the provided pointer
// void * ptr -> pointer to memory chunk that should be marked as free
//
// returns -> void
void wfree(void * ptr) {
    printf("Freeing allocated memory:\n");

    // if memory uninitialized, call error
    if (_arena_start == NULL) {
        printf("...error: arena is uninitialized\n");

        statusno = ERR_UNINITIALIZED;
    }
    // if requested pointer null, call error
    if (ptr == NULL) {
        printf("...error: requested pointer is invalid\n");

        statusno = ERR_BAD_ARGUMENTS;
    }
    // if requested pointer is outside memory bounds, call error
    if ((_arena_end < ptr) || (_arena_start > ptr)) {
        printf("...error: requested pointer is outside of the arena\n");

        statusno = ERR_BAD_ARGUMENTS;
    }

    printf("...supplied pointer %p:\n", ptr);
    printf("...being careful with my pointer arthimetic and void pointer casting\n");

    struct __node_t header;
    void * header_ptr = ptr - header_size;

    printf("...accessing chunk header at %p\n", header_ptr);
    memcpy(&header, header_ptr, header_size);

    printf("...chunk of size %lu\n", (unsigned long) header.size);
    printf("...checking if coalescing is needed\n");

    struct __node_t bwd_header;
    int bwd_exist = 0;

    struct __node_t fwd_header;
    int fwd_exist = 0;

    // check if header.bwd exists
    if (header.bwd != NULL) {
        memcpy(&bwd_header, header.bwd, header_size);
        bwd_exist = 1;
    }
    // check if header.fwd exists
    if (header.fwd != NULL) {
        memcpy(&fwd_header, header.fwd, header_size);
        fwd_exist = 1;
    }

    // coalesce if header.bwd is free, setting header.bwd & size appropriately
    if (bwd_exist && bwd_header.is_free) {
        header_ptr = header.bwd;
        header.bwd = bwd_header.bwd;
        header.size += bwd_header.size + header_size;

        // update the header.fwd's header.bwd to match the new coalesced header
        if (fwd_exist) {
            fwd_header.bwd = header_ptr;
            memcpy(header.fwd, &fwd_header, header_size);
        }

        bwd_exist = -1;
    }
    
    // coalesce if header.fwd is free, setting header.fwd & size appropriately
    if (fwd_exist && fwd_header.is_free) {
        header.fwd = fwd_header.fwd;
        header.size += fwd_header.size + header_size;

        fwd_exist = -1;
    }
    
    header.is_free = 1;
    memcpy(header_ptr, &header, header_size);

    // check which memory chunks have been coalesced for appropriate prints
    if (bwd_exist == -1 && fwd_exist == -1) {
        printf("...col. case 1: previous, current, and next chunks all free.\n");
    }
    else if (bwd_exist == -1) {
        printf("...col. case 2: previous and current chunks free.\n");
    }
    else if (fwd_exist == -1) {
        printf("...col. case 3: current and next chunks free.\n");
    }
    else {
        printf("...coalescing not needed.\n");
    }
}

// initialize the memory arena so that chunks can be allocated
// size_t size -> the total size of the memory arena, including space for headers
//
// returns -> error code if error, else size of memory arena
int init(size_t size) {
    printf("Initializing arena:\n");
    printf("...requested size %lu bytes\n", (unsigned long) size);

    // if requested size is less than 1 byte, call error
    if (size < 1) {
        printf("...error: requested size is smaller than 1 byte\n");

        statusno = ERR_BAD_ARGUMENTS;
        return ERR_BAD_ARGUMENTS;
    }
    // if requested size is greater than MAX_ARENA_SIZE, call error
    else if (size > 2147483647) {
        printf("...error: requested size larger than MAX_ARENA_SIZE (2147483647)\n");

        statusno = ERR_BAD_ARGUMENTS;
        return ERR_BAD_ARGUMENTS;
    }

    int page_size = getpagesize();
    printf("...pagesize is %d bytes\n", page_size);

    unsigned long mem_size = (size / page_size) * page_size;

    // if memory size needs to be increased by a page due to a requested memory amount that is not
    // a multiple of the system page size
    if ((size % page_size) > 0) {
        mem_size += page_size;

        printf("...adjusting size with page boundaries\n");
        printf("...adjusted size is %lu bytes\n", (unsigned long) mem_size);
    }

    // if adjusted memory size is somehow less than 1 byte, call error
    if (mem_size < 1) {
        printf("...error: adjusted size %lu is invalid\n", (unsigned long) mem_size);

        statusno = ERR_BAD_ARGUMENTS;
        return ERR_BAD_ARGUMENTS;
    }

    int fd = open("/dev/zero", O_RDWR);

    printf("...mapping arena with mmap()\n");
    _arena_start = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (_arena_start == (void *) -1) {
        printf("...error: syscall mmap() failed\n");

        statusno = ERR_SYSCALL_FAILED;
        return ERR_SYSCALL_FAILED;
    }

    printf("...arena starts at %p\n", _arena_start);

    _arena_end = _arena_start + mem_size;
    printf("...arena ends at %p\n", _arena_end);

    printf("...initializing header for initial free chunk\n");

    // initialize the first header for the entire memory space, expected to be split by any
    // allocation less than mem_size - (2 * header_size)
    struct __node_t base_header;
    base_header.is_free = 1;
    base_header.size = mem_size - header_size;
    base_header.fwd = NULL;
    base_header.bwd = NULL;

    memcpy(_arena_start, &base_header, header_size);

    printf("...header size is %lu bytes\n", (unsigned long) header_size);

    return (int) mem_size;
}

// uninitialized the memory arena, making it impossible to allocate memory
int destroy() {
    printf("Destroying Arena:\n");
    
    // if memory arena is not initialized, call error
    if (_arena_start == NULL) {
        printf("...error: cannot destroy unintialized arena. Setting error status\n");
        
        statusno = ERR_UNINITIALIZED;
        return ERR_UNINITIALIZED;
    }

    size_t size = _arena_end - _arena_start;

    printf("...unmapping arena with munmap()\n");
    if (munmap(_arena_start, size) == -1) {
        printf("...error: syscall munmap() failed\n");

        statusno = ERR_SYSCALL_FAILED;
        return ERR_SYSCALL_FAILED;
    }

    _arena_start = NULL;
    _arena_end = NULL;

    return 0;
}