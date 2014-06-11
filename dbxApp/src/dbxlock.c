

#include <errlog.h>
#include <epicsAtomic.h>
#include <epicsThread.h>
#include <epicsAssert.h>
#include <dbDefs.h>

#include "dbxlock_priv.h"

/* Counter which increments each time a dbxLockRef is changed globally.
 * Since this is assumed to be (relatively) rare this counter acts as an optimization
 * to avoid verifying dbxLockRef to dbxLock associations after each multi-lock
 * operation.
 */
static size_t recomputeCnt;

static epicsThreadOnceId dbxlockinit = EPICS_THREAD_ONCE_INIT;

static double tickquantum;

/************ internal functions ***********/

static void dbxlockonce(void *x)
{
    tickquantum = epicsThreadSleepQuantum()*2;
}

static
int dbxlockcomp(const void *rawA, const void *rawB)
{
    const dbx_locker_ref *refA=rawA, *refB=rawB;
    const dbxLock *A=refA->lock, *B=refB->lock;
    if(!A && !B)
        return 0; /* NULL == NULL */
    else if(!A)
        return 1; /* NULL > !NULL */
    else if(!B)
        return -1; /* !NULL < NULL */
    else if(A < B)
        return -1;
    else if(A > B)
        return 1;
    else
        return 0;
}

static
dbxLock * dbxlockalloc(void)
{
    dbxLock *L = calloc(1, sizeof(*L));
    if(L) {
        epicsThreadOnce(&dbxlockinit, &dbxlockonce, NULL);
        L->lock = epicsMutexCreate();
        if(!L->lock) {
            free(L);
            L = NULL;
        } else
            L->refcnt = 1;
    }
    return L;
}

static inline
void dbxlockref(dbxLock *ptr)
{
    int cnt = epicsAtomicIncrIntT(&ptr->refcnt);
    assert(cnt>1);
}

/* Lock must NOT be held! */
void dbxlockunref(dbxLock *ptr)
{
    int cnt;

    if(!ptr)
        return;

    cnt = epicsAtomicDecrIntT(&ptr->refcnt);
    assert(cnt>=0);

    if(cnt>0)
        return;

    epicsMutexMustLock(ptr->lock);
    assert(ellCount(&ptr->refsets)==0);
    assert(ptr->owner==NULL);
    epicsMutexUnlock(ptr->lock);

    epicsMutexDestroy(ptr->lock);
    free(ptr);
}

/* Call w/ update=1 before locking to update cached dbxLock entries.
 * Call w/ update=0 after locking to verify that dbxLockRefs weren't updated
 */
static
int dbxupdaterefs(dbxLocker *ptr, int update)
{
    int changed = 0;
    size_t i, nlock = ptr->maxrefs,
           recomp = epicsAtomicGetSizeT(&recomputeCnt);

    if(ptr->recomp!=recomp) {
        /* some dbxLockRefs changed (somewhere) */

        for(i=0; i<nlock; i++) {
            dbx_locker_ref *ref = &ptr->refs[i];
            if(!ref->ref) {
                ref->lock = NULL;
                continue;
            }

            slock(ref->ref);
            if(ref->lock!=ref->ref->lock) {
                changed = 1;
                if(update) {
                    dbxlockunref(ref->lock);
                    if(ref->ref->lock)
                        dbxlockref(ref->ref->lock);
                    ref->lock = ref->ref->lock;
                }
            }
            sunlock(ref->ref);
        }
        if(update)
            ptr->recomp = recomp;
    }

    if(changed && update)
        qsort(ptr->refs, ptr->maxrefs, sizeof(dbx_locker_ref), &dbxlockcomp);
#ifdef DBXLOCK_DEBUG
    for(i=1; i<ptr->maxrefs; i++) {
        if(!ptr->refs[i].lock)
            continue;
        assert(ptr->refs[i-1].lock <= ptr->refs[i].lock);
        assert(dbxlockcomp(&ptr->refs[i-1], &ptr->refs[i])<1);
    }
#endif
    return changed;
}

/************ public api ***********/

int dbxLockRefInit(dbxLockRef* pref, unsigned int flags)
{
    assert(pref->lock==NULL && pref->spin==0);

    memset(pref, 0, sizeof(*pref));
    pref->lock = dbxlockalloc();
    alloclock(pref);

    if(pref->lock) {
        ellAdd(&pref->lock->refsets, &pref->refsetsNode);
    }

    return pref->lock==NULL;
}

int dbxLockRefClean(dbxLockRef *pref)
{
    ELLNODE *cur;
    dbxLock *lock;

    if(!pref)
        return 0;

    assert(pref->lock && epicsAtomicGetIntT(&pref->lock->refcnt)>0);

    /* we are taking an extra reference here */
    lock = dbxLockOne(pref, 0);
    assert(lock);
    /* give up the extra ref.  We still have the callers ref. */
    epicsAtomicDecrIntT(&lock->refcnt);

    ellDelete(&lock->refsets, &pref->refsetsNode);

    /* Clean all links involving this reference */
    ELL_FOREACH_POP(&pref->linksA, cur) {
        dbxLockLink *L = CONTAINER(cur, dbxLockLink, linksANode);

        assert(epicsAtomicGetIntT(&L->refcnt)>0);
        assert(L->A==pref);
        assert(L->B->lock==L->A->lock);

        ellDelete(&L->B->linksB, &L->linksBNode);
        L->A = L->B = NULL;
    }
    ELL_FOREACH_POP(&pref->linksB, cur) {
        dbxLockLink *L = CONTAINER(cur, dbxLockLink, linksBNode);

        assert(epicsAtomicGetIntT(&L->refcnt)>0);
        assert(L->B==pref);
        assert(L->B->lock==L->A->lock);

        ellDelete(&L->A->linksA, &L->linksANode);
        L->A = L->B = NULL;
    }

    freelock(pref);
    memset(pref, 0, sizeof(*pref));

    dbxUnlockOne(lock); /* release the caller's ref */
    return 0;
}

dbxLocker * dbxLockerAlloc(dbxLockRef** pref, size_t nlock, unsigned int flags)
{
    dbxLocker *ptr = calloc(1, sizeof(*ptr)+nlock*sizeof(*ptr->refs));
    if(ptr) {
        epicsThreadOnce(&dbxlockinit, &dbxlockonce, NULL);
        size_t i;

        ptr->refs = (dbx_locker_ref*)(ptr+1);
        ptr->maxrefs = nlock;
        /* intentionally spoil the recomp count to ensure that
         * dbxupdaterefs() will run this first time
         */
        ptr->recomp = epicsAtomicGetSizeT(&recomputeCnt)-1;

        for(i=0; i<nlock; i++) {
            ptr->refs[i].ref = pref[i];
        }
        dbxupdaterefs(ptr, 1);
    }
    return ptr;
}

int dbxLockerFree(dbxLocker *ptr)
{
    int i;
    assert(ellCount(&ptr->locked)==0);

    for(i=0; i<ptr->maxrefs; i++) {
        dbxlockunref(ptr->refs[i].lock);
    }
    free(ptr);
    return 0;
}

dbxLock* dbxLockOne(dbxLockRef *R, unsigned int flags)
{
    dbxLock *L, *L2;

retry:
    slock(R);
    L = R->lock;
    dbxlockref(L);
    sunlock(R);

    epicsMutexMustLock(L->lock);

    slock(R);
    L2 = R->lock;
    sunlock(R);

    if(L != L2) {
        /* oops, collided with recompute */
        epicsMutexUnlock(L->lock);
        dbxlockunref(L);
        goto retry;
    }

    return L;
}

int dbxUnlockOne(dbxLock* L)
{
    epicsMutexUnlock(L->lock);
    dbxlockunref(L);
    return 0;
}

int dbxLockMany(dbxLocker *ptr, unsigned int flags)
{
#ifdef DBXLOCK_DEBUG
    dbxLock *prevlock;
#endif
    size_t i, nlock = ptr->maxrefs;
    dbxLock *plock;
    assert(ellCount(&ptr->locked)==0);

retry:
#ifdef DBXLOCK_DEBUG
    prevlock = NULL;
#endif
    dbxupdaterefs(ptr, 1);

    for(i=0, plock=NULL; i<nlock; i++) {
        dbx_locker_ref *ref = &ptr->refs[i];

        /* skip NULLs and duplicates */
        if(!ref->lock || (i!=0 && ref->lock==plock))
            continue;
        plock = ref->lock;
#ifdef DBXLOCK_DEBUG
        assert(!prevlock || prevlock < plock);
        prevlock = plock;
#endif

        epicsMutexMustLock(plock->lock);
        assert(plock->owner==NULL);
        plock->owner = ptr;
        ellAdd(&ptr->locked, &ref->lock->lockedNode);
        // An extra ref for the locked list node
        dbxlockref(plock);
    }

    if(dbxupdaterefs(ptr,0)) {
        /* oops, collided with recompute */
        dbxUnlockMany(ptr);
        goto retry;
    }

    return 0;
}

int dbxUnlockMany(dbxLocker *ptr)
{
    ELLNODE *cur;

    ELL_FOREACH_POP(&ptr->locked, cur) {
        dbxLock *L = CONTAINER(cur, dbxLock, lockedNode);

        assert(L->owner==ptr);
        L->owner = NULL;
        epicsMutexUnlock(L->lock);
        // Release the extra ref taken in dbxLockMany()
        // and dbxLockRefSplit()
        dbxlockunref(L);
    }

    return 0;
}

/* assumes that lock(s) referenced by A and B are locked */
dbxLockLink* dbxLockRefJoin(dbxLocker *ptr, dbxLockRef *A, dbxLockRef *B)
{
    dbxLock *lockA = A->lock, *lockB = B->lock;

    assert(epicsAtomicGetIntT(&lockA->refcnt)>0);
    assert(epicsAtomicGetIntT(&lockB->refcnt)>0);

    if(lockA==lockB) { /* already share a lock */
        ELLNODE *cur;
        dbxLockLink *link;

        /* find existing (direct) link A <-> B */
        ELL_FOREACH(&A->linksA, cur) {
            dbxLockLink *linkA = CONTAINER(cur, dbxLockLink, linksANode);
            assert(linkA->A==A);
            if(linkA->B==B) {
                size_t newcnt = epicsAtomicIncrIntT(&linkA->refcnt);
                assert(newcnt>1);
                return linkA;
            }
        }
        ELL_FOREACH(&A->linksB, cur) {
            dbxLockLink *linkB = CONTAINER(cur, dbxLockLink, linksBNode);
            assert(linkB->B==A);
            if(linkB->A==B) {
                size_t newcnt = epicsAtomicIncrIntT(&linkB->refcnt);
                assert(newcnt>1);
                return linkB;
            }
        }

        /* no direct link exists, but refs are already joined
         * indirectly A <-> ... <-> B.
         * So we just create a direct link.
         */
        link = calloc(1, sizeof(*link));
        if(!link)
            return NULL;

        link->A = A;
        link->B = B;
        link->refcnt = 1;
        ellAdd(&A->linksA, &link->linksANode);
        ellAdd(&B->linksB, &link->linksBNode);
        return link;

    } else { /* create new link */
        ELLNODE *cur;
        dbxLockLink *link = calloc(1, sizeof(*link));
        if(!link)
            return NULL;

        link->A = A;
        link->B = B;
        link->refcnt = 1;
        ellAdd(&A->linksA, &link->linksANode);
        ellAdd(&B->linksB, &link->linksBNode);

        /* we will merge lockB into lockA */

        /* re-target lock-refs to A */
        ELL_FOREACH(&lockB->refsets, cur) {
            dbxLockRef *refX = CONTAINER(cur, dbxLockRef, refsetsNode);

            assert(refX->lock==lockB);
            slock(refX);
            refX->lock = lockA;
            epicsAtomicIncrSizeT(&recomputeCnt);
            sunlock(refX);
        }

        /* update ref counters */
        epicsAtomicAddIntT(&lockB->refcnt, -ellCount(&lockB->refsets));
        epicsAtomicAddIntT(&lockA->refcnt,  ellCount(&lockB->refsets));
        /* should have at least the caller's ref remaining */
        assert(epicsAtomicGetIntT(&lockB->refcnt)>0);

        /* merge refs */
        ellConcat(&lockA->refsets, &lockB->refsets);

        /* now empty lockB will be free'd when its refcnt reaches zero.
         * which may happen as soon as dbxUnlockMany()
         * or might take a long time if it lives
         * in some dbxLocker::refs cache.
         */
        return link;
    }


}

/* assumes that lock referenced by A and B must be locked */
int dbxLockRefSplit(dbxLocker *ptr, dbxLockLink *R)
{
    dbxLockRef *A = R->A, *B = R->B;
    dbxLock *L;
    ELLLIST visited, tovisit;
    int cnt;
    int found = 0;

    cnt = epicsAtomicDecrIntT(&R->refcnt);
    assert(cnt>=0);

    if(cnt>0)
        return 0;

    if(!A && !B) {
        /* cleanup link orphaned by dbxLockRefClean() */
        free(R);
        return 0;
    }
    assert(ptr);
    assert(A!=B);
    L = A->lock;
    assert(L == B->lock);

    ellDelete(&R->A->linksA, &R->linksANode);
    ellDelete(&R->B->linksB, &R->linksBNode);
    free(R);

    /* This was the last (direct) link between A and B.
     * Is there an indirect link?
     * we will do a breadth first search of links outward
     * from A until we find B, or run out of nodes.
     *
     * We will abuse dbxLockRef::refsetsNode.
     * Which will be one of three lists:
     * visited = 0 -> refsets  (initial state)
     *         = 1 -> tovisit
     *         = 2 -> visited
     */

    { /* reset visited flag */
        ELLNODE *curRef;
        ELL_FOREACH(&L->refsets, curRef) {
            dbxLockRef *ref = CONTAINER(curRef, dbxLockRef, refsetsNode);
            ref->visited = 0;
        }

    }

    ellInit(&visited);
    ellInit(&tovisit);
    ellDelete(&L->refsets, &A->refsetsNode);
    ellAdd(&tovisit, &A->refsetsNode);
    A->visited = 1;

    while(ellCount(&tovisit)>0) {
        ELLNODE *curLink;
        dbxLockRef *ref = CONTAINER(ellPop(&tovisit), dbxLockRef, refsetsNode);

        assert(ref->visited==1);

        ref->visited = 2;
        ellAdd(&visited, &ref->refsetsNode);

        ELL_FOREACH(&ref->linksA, curLink) {
            dbxLockLink *linkA = CONTAINER(curLink, dbxLockLink, linksANode);
            assert(linkA->A==ref);

            if(linkA->B==B) {
                /* found an indirect link. */
                found = 1;
                break;

            } else if(linkA->B->visited==0) {
                /* add it to the todo list */
                ellDelete(&L->refsets, &linkA->B->refsetsNode);
                ellAdd(&tovisit, &linkA->B->refsetsNode);
                linkA->B->visited = 1;
            }
        }
        if(found)
            break;
        ELL_FOREACH(&ref->linksB, curLink) {
            dbxLockLink *linkB = CONTAINER(curLink, dbxLockLink, linksBNode);
            assert(linkB->B==ref);

            if(linkB->A==B) {
                /* found an indirect link. */
                found = 1;
                break;

            } else if(linkB->A->visited==0) {
                /* add it to the todo list */
                ellDelete(&L->refsets, &linkB->A->refsetsNode);
                ellAdd(&tovisit, &linkB->A->refsetsNode);
                linkB->A->visited = 1;
            }
        }
        if(found)
            break;
    }

    if(found) {
        /* lock will not split.
         * put refsets back together
         */

        ellConcat(&L->refsets, &visited);
        ellConcat(&L->refsets, &tovisit);
        return 0;

    } else {
        dbxLock *lockB;
        ELLNODE *curRef;
        /* lock will split.
         * The new lock for B will contain all nodes not reachable from A,
         * Which are those remaining in L->refsets.
         *
         */

        assert(ellCount(&tovisit)==0);

        lockB = dbxlockalloc(); /* refcnt==1 */
        if(!lockB)
            return 1;
        epicsMutexMustLock(lockB->lock);
        lockB->owner = ptr;

        // use the initial ref for the locked node
        ellAdd(&ptr->locked, &lockB->lockedNode);

        ellConcat(&lockB->refsets, &L->refsets);
        ellConcat(&L->refsets, &visited);

        ELL_FOREACH(&lockB->refsets, curRef) {
            dbxLockRef *ref = CONTAINER(curRef, dbxLockRef, refsetsNode);

            slock(ref);
            ref->lock = lockB;
            epicsAtomicIncrSizeT(&recomputeCnt);
            sunlock(ref);
        }

        /* adjust ref counts */
        assert(epicsAtomicGetIntT(&L->refcnt) > ellCount(&lockB->refsets));
        epicsAtomicAddIntT(&lockB->refcnt, ellCount(&lockB->refsets));
        epicsAtomicAddIntT(&L->refcnt,    -ellCount(&lockB->refsets));
        /* should have at least the caller's ref remaining */
        assert(epicsAtomicGetIntT(&L->refcnt)>0);

        return 0;
    }
}
