#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <map>
//#include <limits.h>
#include "common.h"
using namespace std;
#endif /* __PROGTEST__ */

void * wrapper(void *);

class Args {
public:
    uint32_t page;
    void * processArg;
    void (* process) (CCPU *, void *);
    uint32_t memLimit;
    Args(uint32_t tPage, void * tProcessArg, void (* tProcess) (CCPU *, void *), uint32_t tMemLimit = 0)
    : page(tPage), processArg(tProcessArg), process(tProcess), memLimit(tMemLimit) {
    };
};

uint32_t * memory = NULL;
uint8_t runningProcesses = 0;
uint32_t pagesTotal = 0;
uint32_t countOfFreePages = 0;
bool * freePages = NULL;
pthread_mutex_t mtxAllocator;
pthread_mutex_t mtxProcessCounter;
pthread_cond_t condProcessCounter;

class Pager {
private:

    void createTable(const int & position) {
        uint32_t max = position * 1024 + 1024;
        for (uint32_t i = position * 1024; i < max; i++) {
            memory[i] = 0;
        }
        freePages[position] = true;
    }

    uint32_t findFreePage() {
        uint32_t i;
        for (i = 0; i < pagesTotal; i++) { //find free page
            if (!freePages[i]) break;
        }
        if (i == pagesTotal) {
            cout << "***********CHYBA*************" << endl;
            for (i = 0; i < 8192; i++) { //find free page
                if (!freePages[i]) cout << "true" << endl;
            }
            throw 1; //no free pages has left
        }

        return i;
    }

public:

    uint32_t allocatePage(const bool & top, uint32_t owner = 0) {
        uint32_t freePage = 0;
        if (top) { //create top-level table
            freePage = findFreePage();
            createTable(freePage);
            countOfFreePages--;
        } else { //create data entry
            //top-level
            owner = owner >> 2;
            uint32_t topLevel;
            uint32_t max = owner + 1024;
            for (topLevel = owner; topLevel < max; topLevel++) { //find the last present entry in top-level table  
                if (!(memory[topLevel] & 0x0001)) break;
            }

            if (topLevel == owner) { //top-level table is completely clean 
                uint32_t entry = 0;
                freePage = findFreePage();
                countOfFreePages--;
                createTable(freePage);
                entry = freePage << 12; //12 for offset
                entry |= 0x0001; //present_bit
                entry |= 0x0002; //write
                entry |= 0x0004; //user_bit
                memory[topLevel] = entry; //update entry in top-level table
                topLevel += 1; //make correction for following operations
            }

            topLevel--;

            //second level
            uint32_t sub_table = ((memory[topLevel] & 0xFFFFF000) >> 2);
            uint32_t field;
            max = sub_table + 1024;

            for (field = sub_table; field < max; field++) { //find the post-last entry in second-level table  
                if (!(memory[field] & 0x0001)) break;
            }

            if (field == max) { //table is full, create new and update top-level table
                if (topLevel == owner + 1023) throw 2; // memory limit exceeded

                freePage = findFreePage();
                uint32_t entry = 0;
                createTable(freePage);
                countOfFreePages--;
                entry = freePage << 12;
                entry |= 0x0001; //present_bit
                entry |= 0x0002; //write
                entry |= 0x0004; //user
                memory[topLevel + 1] = entry;
                field = freePage * 1024;
            }


            //fill the free entry
            uint32_t entry = 0;
            freePage = findFreePage();
            countOfFreePages--;
            entry = freePage << 12;
            entry |= 0x0001; //present
            entry |= 0x0002; //write
            entry |= 0x0004; //user
            freePages[freePage] = true;
            memory[field] = entry;
        }
        return freePage << 12;
    }

    void deallocatePage(const bool & top, uint32_t owner) {
        if (top) { //delete top-level table
            owner = owner >> 2;
            freePages[owner >> 10] = false;
            memory[owner] = 0;
            countOfFreePages++;

        } else { //delete last data entry
            owner = owner >> 2;
            //top-level
            uint32_t topLevel;
            uint32_t max = owner + 1024;
            for (topLevel = owner; topLevel < max; topLevel++) { //find the post-last present entry in top-level table  
                if (!(memory[topLevel] & 0x0001)) break;
            }

            topLevel--; // move to last present entry

            //second level
            uint32_t sub_table = ((memory[topLevel] & 0xFFFFF000) >> 2);
            uint32_t field;
            uint32_t page;
            max = sub_table + 1024;

            for (field = sub_table; field < max; field++) { //find the post-last entry in second-level table  
                if (!(memory[field] & 0x0001)) break;
            }
            page = memory[field - 1] >> 12;

            freePages[page] = false;
            countOfFreePages++;

            memory[field - 1] = 0;
            if (field - 1 == sub_table) { //table is empty
                freePages[memory[topLevel] >> 12] = false;
                memory[topLevel] = 0;
                countOfFreePages++;
            }



        }
    }

    uint32_t copyMemory(uint32_t owner) {
        owner = owner >> 2;
        uint32_t main = allocatePage(true);
                 countOfFreePages--;
        uint32_t mainCounter = 0;
        uint32_t subCounter = 0;
        uint32_t sub = 0;
        uint32_t entry = 0;
        while (memory[owner + mainCounter] & 0x1) {
            sub = (memory[owner + mainCounter] & 0xFFFFF000) >> 2;
            uint32_t page = findFreePage();
            freePages[page] = true;
            countOfFreePages--;
            page = page << 10;

            entry = page << 2;
            entry |= 0x0001; //present
            entry |= 0x0002; //write
            entry |= 0x0004; //user
            memory[(main >> 2) + mainCounter] = entry;

            while (memory[sub + subCounter] & 0x1) {
                uint32_t dataPage = findFreePage();
                freePages[dataPage] = true;
                countOfFreePages--;
                memcpy(&memory[dataPage << 10], &memory[(memory[sub + subCounter] & 0xFFFFF000) >> 2], 1024 * sizeof (uint32_t));

                entry = dataPage << 12;
                entry |= 0x0001; //present
                entry |= 0x0002; //write
                entry |= 0x0004; //user
                memory[page + subCounter] = entry;

                subCounter++;
            }
            subCounter = 0;
            mainCounter++;
        }
        return main;
    }

    bool isEnoughSpace(const uint32_t & count, uint32_t owner) {
        bool free = false;
        uint32_t needed = 0;
        owner = owner >> 2;

        //top-level
        uint32_t topLevel;
        uint32_t max = owner + 1024;
        for (topLevel = owner; topLevel < max; topLevel++) { //find the last present entry in top-level table  
            if (!(memory[topLevel] & 0x0001)) break;
        }

        if (topLevel == owner) { //top-level table is completely clean 
            needed++;
            topLevel += 1; //make correction for following operations
        }

        topLevel--;

        //second level
        uint32_t sub_table = ((memory[topLevel] & 0xFFFFF000) >> 2);
        uint32_t field = sub_table;
        max = sub_table + 1024;

        for (uint32_t cc = 0; cc < count; cc++) {

            if (!free) {
                for (; field < max; field++) { //find the last present entry in top-level table  
                    if (!(memory[field] & 0x0001)) break;
                }
            }

            if (field == max) { //table is full, create new and update top-level table
                if (field == owner + 1022) return false; // memory limit exceeded || No, tady moc fungovat nebude
                needed++;
                field = 0;
                max = 1024;
            }

            needed++;
            field++;
        }
        if (needed > countOfFreePages) return false;
        return true;
    }
};

class MyCPU : public CCPU {
public:

    MyCPU(uint8_t * memStart, uint32_t pageTableRoot, uint32_t tMemLimit) //initialise memory and updates number of running processes
    : CCPU(memStart, pageTableRoot), memLimit(tMemLimit) {
        pthread_mutex_lock(&mtxProcessCounter);
        runningProcesses++;
        pthread_mutex_unlock(&mtxProcessCounter);
    };

    virtual ~MyCPU() {
        Pager p;
        pthread_mutex_lock(&mtxAllocator);
        for (uint32_t i = 0; i < memLimit; i++) { //clean up
            p.deallocatePage(false, m_PageTableRoot);
        }
        p.deallocatePage(true, m_PageTableRoot);

        /*int cnt = 0;
        for (uint32_t i = 0; i < 8192; i++) {
            if (!freePages[i]) cnt++;
        }
        cout << cnt << endl;
         */
        pthread_mutex_unlock(&mtxAllocator);

        pthread_mutex_lock(&mtxProcessCounter);
        runningProcesses--;
        if (runningProcesses == 0) pthread_cond_broadcast(&condProcessCounter);
        pthread_mutex_unlock(&mtxProcessCounter);
    }

    virtual uint32_t GetMemLimit(void) const {
        return memLimit;
    }

    virtual bool SetMemLimit(uint32_t pages) {

        if (pages == memLimit) return true; //no need to change size

        Pager p;
        uint32_t needed;
        if (pages > memLimit) { //extend memory size
            needed = pages - memLimit;
            pthread_mutex_lock(&mtxAllocator);
            if (p.isEnoughSpace(needed, m_PageTableRoot)) { //check whether is there enough space for allocation
                for (uint32_t i = 0; i < needed; i++) {
                    try {
                        p.allocatePage(false, m_PageTableRoot);
                    } catch (...) {
                        pthread_mutex_unlock(&mtxAllocator);
                        return false;
                    }
                }
            } else {
                pthread_mutex_unlock(&mtxAllocator);
                return false;
            }
            pthread_mutex_unlock(&mtxAllocator);
        } else {
            needed = memLimit - pages;
            pthread_mutex_lock(&mtxAllocator);
            for (uint32_t i = 0; i < needed; i++) {
                p.deallocatePage(false, m_PageTableRoot);
            }
            pthread_mutex_unlock(&mtxAllocator);
        }
        memLimit = pages;
        return true;
    }

    virtual bool NewProcess(void * processArg, void (* entryPoint) (CCPU *, void *), bool copyMem) {
        Pager p;

        try {
            pthread_attr_t attr;
            pthread_t dummy;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            uint32_t mem = 0;
            uint32_t page;

            if (copyMem) {
                pthread_mutex_lock(&mtxAllocator);

                if (!p.isEnoughSpace(memLimit + memLimit / 1024 + 1, m_PageTableRoot)) {
                    pthread_mutex_unlock(&mtxAllocator);
                    return false;
                }
                page = p.copyMemory(m_PageTableRoot);
                mem = memLimit;
                pthread_mutex_unlock(&mtxAllocator);

            } else {
                pthread_mutex_lock(&mtxAllocator);

                page = p.allocatePage(true);

                pthread_mutex_unlock(&mtxAllocator);
            }

            Args * args = new Args(page, processArg, entryPoint, mem);

            pthread_create(&dummy, &attr, wrapper, args);

        } catch (...) {
            pthread_mutex_unlock(&mtxAllocator);
            return false;
        }
        return true;
    }
protected:
    uint32_t memLimit = 0;
    /*
     if copy-on-write is implemented:

     virtual bool             pageFaultHandler              ( uint32_t          address,
                                                              bool              write );
     */
};

void MemMgr(void * mem, uint32_t totalPages, void * processArg, void (* mainProcess) (CCPU *, void *)) {
    if (totalPages == 0) return;

    //initialize variables
    countOfFreePages = pagesTotal = totalPages;
    memory = (uint32_t *) mem;
    freePages = new bool [pagesTotal](); //v pripade problemu presunout to stranky
    pthread_mutex_init(&mtxProcessCounter, NULL);
    pthread_mutex_init(&mtxAllocator, NULL);
    pthread_cond_init(&condProcessCounter, NULL);

    //run init
    Pager p;
    uint32_t page;
    page = p.allocatePage(true);
    Args * args = new Args(page, processArg, mainProcess);



    wrapper(args);
    pthread_mutex_lock(&mtxProcessCounter);
    if (runningProcesses != 0) pthread_cond_wait(&condProcessCounter, &mtxProcessCounter); //wait for other processes to exit
    pthread_mutex_unlock(&mtxProcessCounter);

    if (freePages != NULL) delete [] freePages;

    pthread_mutex_destroy(&mtxProcessCounter);
    pthread_mutex_destroy(&mtxAllocator);
    pthread_cond_destroy(&condProcessCounter);
}

void * wrapper(void * tArgs) {
    Args * args = (Args *) tArgs;
    MyCPU CPU((uint8_t *) memory, args->page, args->memLimit);
    args->process(&CPU, args->processArg);
    delete args;
    return NULL;
}