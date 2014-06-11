
#include <time.h>
#include <stdlib.h>

#include <epicsUnitTest.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsAtomic.h>
#include <testMain.h>

#include "dbxlock_priv.h"

typedef struct threaddata threaddata;

static
size_t numOne, numMany, numSplit, numJoin;

typedef struct {
    size_t nrefs;
    dbxLockRef *trefs;

    size_t nthreads;
    threaddata *tthreads;

    struct timespec stoptime;
} testdata;

struct threaddata {
    int id;
    epicsThreadId me;
    testdata *central;
    epicsEventId stop, sync;
    unsigned int seed;
    dbxLockLink *link;
};

static
void fetchtime(struct timespec *ts)
{
    int ret = clock_gettime(CLOCK_MONOTONIC_RAW, ts);
    assert(ret==0);
}

/* A - B */
static inline
int64_t deltatime(struct timespec *A, struct timespec *B)
{
    int64_t aa, bb;
    aa = A->tv_sec*1000000000 + A->tv_nsec;
    bb = B->tv_sec*1000000000 + B->tv_nsec;
    return aa - bb;
}

static
void lockone(threaddata *self, int r)
{
    dbxLock *lock;
    int i=r%self->central->nrefs;

    //testDiag("%d takes %d", self->id, i);
    lock = dbxLockOne(&self->central->trefs[i], 0);

    if(lock)
        dbxUnlockOne(lock);
    else
        testFail("dbxUnlockOne(%p) fails", &self->central->trefs[i]);
    epicsAtomicIncrSizeT(&numOne);
}

#define NLOCKMAX 20

static
void lockmany(threaddata *self, int r)
{
    size_t i, nlock = (size_t)(r%NLOCKMAX);
    dbxLockRef *refs[NLOCKMAX];
    dbxLocker *locker;

    epicsAtomicIncrSizeT(&numMany);

    if(self->link) {
        refs[0] = self->link->A;
        refs[1] = self->link->B;
        locker = dbxLockerAlloc(refs, 2, 0);
        if(locker) {
            if(dbxLockMany(locker, 0)) {
                testFail("dbxLockerAlloc fails");
                return;
            }
            dbxLockRefSplit(locker, self->link);
            dbxUnlockMany(locker);
            dbxLockerFree(locker);
            self->link = NULL;
            epicsAtomicIncrSizeT(&numSplit);
        }

    }

    if(nlock==0)
        return;

    //testDiag("%d locks %d", self->id, (int)nlock);
    for(i=0; i<nlock; i++) {
        int r2 = rand_r(&self->seed);

        refs[i] = &self->central->trefs[r2%self->central->nrefs];
    }

    locker = dbxLockerAlloc(refs, nlock, 0);
    if(locker) {
        if(dbxLockMany(locker, 0)) {
            testFail("dbxLockerAlloc fails");
            return;
        }
        if(nlock>=2 && refs[0] != refs[1] && rand_r(&self->seed)<RAND_MAX/32)
        {
            self->link = dbxLockRefJoin(locker, refs[0], refs[1]);
            epicsAtomicIncrSizeT(&numJoin);
        }
        dbxUnlockMany(locker);
        dbxLockerFree(locker);
    }
}

static
void testTask(void *raw)
{
    threaddata *self = raw;
    size_t N=0;

    epicsEventSignal(self->sync);
    testDiag("%d Ready", self->id);
    epicsEventMustWait(self->stop);
    testDiag("%d Running", self->id);

    while(1) {
        struct timespec end;
        int r = rand_r(&self->seed);

        if(r>RAND_MAX/32)
            lockone(self, r);
        else
            lockmany(self, r);

        if(N%100==0)
            fetchtime(&end);
        N++;

        if(deltatime(&end, &self->central->stoptime)>0)
            break;
    }
    testPass("%d Finished after %lu cycles", self->id, (unsigned long)N);
    epicsEventSignal(self->stop);
}

#define MAXREFS 150

static
void runStress(void)
{
    struct timespec seedts;
    size_t i;
    testdata data;
    int numrefs;
    int numlockers = 8;

    fetchtime(&seedts);
    srand(seedts.tv_nsec);

    numrefs = rand()%MAXREFS;
    if(numrefs<1)
        numrefs = 1;

    testDiag("Creating %d refs", numrefs);

    fetchtime(&data.stoptime);
    data.stoptime.tv_sec += 15;

    data.nrefs = (size_t)numrefs;
    data.nthreads = (size_t)numlockers;
    data.trefs = calloc(data.nrefs, sizeof(*data.trefs));
    data.tthreads = calloc(data.nthreads, sizeof(*data.tthreads));
    assert(data.trefs && data.tthreads);

    for(i=0; i<numrefs; i++)
        dbxLockRefInit(&data.trefs[i], 0);

    testDiag("Creating %d threads", numlockers);
    for(i=0; i<numlockers; i++) {
        threaddata *td = &data.tthreads[i];

        td->id = i;
        td->central = &data;
        td->seed = rand();
        td->stop = epicsEventMustCreate(epicsEventEmpty);
        td->sync = epicsEventMustCreate(epicsEventEmpty);

        testDiag("Start %lu", (unsigned long)i);
        td->me = epicsThreadMustCreate("testTask",
                                       epicsThreadPriorityMedium,
                                       epicsThreadGetStackSize(epicsThreadStackSmall),
                                       &testTask, td);
        epicsEventWait(td->sync);
    }

    testDiag("Starting");
    for(i=0; i<numlockers; i++) {
        threaddata *td = &data.tthreads[i];
        epicsEventSignal(td->stop);
    }

    testDiag("Waiting");
    for(i=0; i<numlockers; i++) {
        threaddata *td = &data.tthreads[i];

        epicsEventMustWait(td->stop);
        testDiag("Stopped %lu", (unsigned long)i);
        epicsEventDestroy(td->stop);
        epicsEventDestroy(td->sync);
    }

    testDiag("Cleanup");
    for(i=0; i<numrefs; i++)
        dbxLockRefClean(&data.trefs[i]);

    for(i=0; i<numlockers; i++) {
        threaddata *td = &data.tthreads[i];
        if(td->link)
            dbxLockRefSplit(NULL, td->link);
    }

    free(data.trefs);
    free(data.tthreads);

    testDiag("# of dbxLockOne() %lu", (unsigned long)numOne);
    testDiag("# of dbxLockMany() %lu", (unsigned long)numMany);
    testDiag("# of dbxLockRefJoin() %lu", (unsigned long)numJoin);
    testDiag("# of dbxLockRefSplit() %lu", (unsigned long)numSplit);
}

MAIN(stresslock)
{
    testPlan(0);
    runStress();
    return testDone();
}
