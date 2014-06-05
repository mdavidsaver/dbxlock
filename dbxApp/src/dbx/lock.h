#ifndef DBX_LOCK_H
#define DBX_LOCK_H

#include <ellLib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dbxLocker dbxLocker;
typedef struct dbxLock dbxLock;
typedef struct dbxLockLink dbxLockLink;

struct dbxLockRef {
    /* the fields of dbxLockRef are not considered a public API */
    /* Access to all fields except spin and lock is governed
     * by the currently associated lock.
     * The lock field may only be changed while the present
     * lock and the spinlock are locked.
     */
    ELLNODE refsetsNode;
    ELLLIST linksA, linksB;
    int visited; /* used by dbxLockRefSplit() */

    dbxLock *lock;
    int spin;
};
typedef struct dbxLockRef dbxLockRef;

int dbxLockRefInit(dbxLockRef* pref, unsigned int flags);
int dbxLockRefClean(dbxLockRef *pref);

dbxLocker * dbxLockerAlloc(dbxLockRef** pref, size_t nlock, unsigned int flags);
int dbxLockerFree(dbxLocker *ptr);

dbxLock* dbxLockOne(dbxLockRef *R, unsigned int flags);
int dbxUnlockOne(dbxLock* L);

int dbxLockMany(dbxLocker *ptr, unsigned int flags);
int dbxUnlockMany(dbxLocker *ptr);

dbxLockLink* dbxLockRefJoin(dbxLocker *ptr, dbxLockRef *A, dbxLockRef *B);
int dbxLockRefSplit(dbxLocker *ptr, dbxLockLink *R);

#ifdef __cplusplus
}
#endif

#endif /* DBX_LOCK_H */
