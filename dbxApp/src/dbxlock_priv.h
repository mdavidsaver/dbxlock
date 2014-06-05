#ifndef DBXLOCK_PRIV_H
#define DBXLOCK_PRIV_H

#include <string.h>

#include <epicsMutex.h>

#include "dbx/lock.h"

#define ELL_FOREACH(LIST, A) \
    for(A=ellFirst(LIST); A; A=ellNext(A))

/* list iteration where A can be deleted */
#define ELL_FOREACH_SAFE(LIST, A, B) \
    for(A=ellFirst(LIST), B=A?ellNext(A):NULL; A; A=B, B=B?ellNext(B):NULL)

#define ELL_FOREACH_POP(LIST, A) \
    while( (A=ellGet(LIST))!=NULL )

struct dbxLock {
    ELLNODE lockedNode;
    epicsMutexId lock;
    int refcnt;
    ELLLIST refsets;
    dbxLocker *owner;
};

struct dbx_locker_ref {
    dbxLockRef *ref;
    /* the last lock found associated with the ref.
     * not stable unless lock is locked, or ref spin
     * is locked.
     */
    dbxLock *lock;
};
typedef struct dbx_locker_ref dbx_locker_ref;

struct dbxLocker {
    ELLLIST locked;
    size_t recomp; /* snapshot of recomputeCnt when refs[] cache updated */
    size_t maxrefs;
    dbx_locker_ref *refs;
};

struct dbxLockLink {
    ELLNODE linksANode, linksBNode;
    dbxLockRef *A, *B;
    int refcnt;
};

void dbxlockunref(dbxLock *ptr);

#endif // DBXLOCK_PRIV_H
