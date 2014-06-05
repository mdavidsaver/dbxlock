
#include <epicsUnitTest.h>
#include <testMain.h>
#include <epicsAtomic.h>

#include "dbxlock_priv.h"

static void testCreate(void)
{
    dbxLockRef A, *refs[] = {&A};
    dbxLocker *L;
    memset(&A, 0, sizeof(A));

    testDiag("Test allocation");

    testOk1(dbxLockRefInit(&A, 0)==0);
    /* 1 count for the dbxLockRef */
    testOk1(A.lock->refcnt==1);

    testOk1((L=dbxLockerAlloc(refs, 1, 0))!=NULL);
    testOk1(A.lock->refcnt==2);
    testOk1(dbxLockerFree(L)==0);

    testOk1(A.lock->refcnt==1);
    testOk1(dbxLockRefClean(&A)==0);
}

static void testLockerSort(void)
{
    dbxLockRef A, B,
            *refs1[] = {&A, &B},
            *refs2[] = {&B, &A};
    dbxLocker *L1, *L2;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    testDiag("Test sorting of lock refs in dbxLockerAlloc()");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(dbxLockRefInit(&B, 0)==0);

    testOk1((L1=dbxLockerAlloc(refs1, 2, 0))!=NULL);
    testOk1((L2=dbxLockerAlloc(refs2, 2, 0))!=NULL);

    testOk1(L1->refs[0].ref==L2->refs[0].ref);
    testOk1(L1->refs[1].ref==L2->refs[1].ref);

    testOk1(dbxLockerFree(L1)==0);
    testOk1(dbxLockerFree(L2)==0);
    testOk1(dbxLockRefClean(&A)==0);
    testOk1(dbxLockRefClean(&B)==0);
}

static void testLockOne(void)
{
    dbxLockRef A;
    dbxLock *K;
    memset(&A, 0, sizeof(A));

    testDiag("Test dbxLockOne");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(A.lock->refcnt==1);

    testOk1((K=dbxLockOne(&A, 0))!=NULL);
    /* 1 more count while the lock is held */
    testOk1(A.lock->refcnt==2);
    testOk1(dbxUnlockOne(K)==0);
    testOk1(A.lock->refcnt==1);

    testOk1((K=dbxLockOne(&A, 0))!=NULL);
    testOk1(A.lock->refcnt==2);
    testOk1(dbxUnlockOne(K)==0);
    testOk1(A.lock->refcnt==1);
    testOk1(dbxLockRefClean(&A)==0);
}

static void testLockMany(void)
{
    dbxLockRef A, B, *refs[] = {&A, &B};
    dbxLocker *L;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    testDiag("Test dbxLockMany");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(dbxLockRefInit(&B, 0)==0);
    testOk1(A.lock->refcnt==1);
    testOk1(B.lock->refcnt==1);
    testOk1((L=dbxLockerAlloc(refs, 2, 0))!=NULL);
    /* 1 more count for the dbxLocker::refs cache */
    testOk1(A.lock->refcnt==2);
    testOk1(B.lock->refcnt==2);
    testOk1(ellCount(&L->locked)==0);

    testOk1(dbxLockMany(L, 0)==0);
    testOk1(ellCount(&L->locked)==2);
    testOk1(A.lock->refcnt==3);
    testOk1(B.lock->refcnt==3);
    /* 1 more count for the dbxLocker::locked list */
    testOk1(dbxUnlockMany(L)==0);
    testOk1(ellCount(&L->locked)==0);

    testOk1(A.lock->refcnt==2);
    testOk1(B.lock->refcnt==2);

    testOk1(dbxLockMany(L, 0)==0);
    testOk1(A.lock->refcnt==3);
    testOk1(B.lock->refcnt==3);
    testOk1(dbxUnlockMany(L)==0);

    testOk1(A.lock->refcnt==2);
    testOk1(B.lock->refcnt==2);
    testOk1(dbxLockerFree(L)==0);
    testOk1(A.lock->refcnt==1);
    testOk1(B.lock->refcnt==1);
    testOk1(dbxLockRefClean(&A)==0);
    testOk1(dbxLockRefClean(&B)==0);
}

static void testLockManyToOne(void)
{
    dbxLockRef A, B, *refs[] = {&A, &B};
    dbxLocker *L;
    dbxLock *K;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    testDiag("Test dbxLockMany then dbxLockOne");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(dbxLockRefInit(&B, 0)==0);
    testOk1((L=dbxLockerAlloc(refs, 2, 0))!=NULL);
    testOk1(dbxLockMany(L, 0)==0);

    testOk1(A.lock->refcnt==3);
    testOk1(B.lock->refcnt==3);

    testOk1((K=dbxLockOne(&A, 0))!=NULL);

    testOk1(A.lock->refcnt==4);
    testOk1(B.lock->refcnt==3);

    testOk1(dbxUnlockOne(K)==0);

    testOk1(A.lock->refcnt==3);
    testOk1(B.lock->refcnt==3);

    testOk1(dbxUnlockMany(L)==0);
    testOk1(A.lock->refcnt==2);
    testOk1(B.lock->refcnt==2);

    testOk1(dbxLockerFree(L)==0);
    testOk1(dbxLockRefClean(&A)==0);
    testOk1(dbxLockRefClean(&B)==0);
}

static void testLockJoin(void)
{
    dbxLockRef A, B, *refs[] = {&A, &B};
    dbxLocker *L;
    dbxLock *LA, *LB;
    dbxLockLink *link, *link2;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    testDiag("Test dbxLockMany then dbxLockRefJoin()");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(dbxLockRefInit(&B, 0)==0);
    testOk1(A.lock!=B.lock);

    testOk1((L=dbxLockerAlloc(refs, 2, 0))!=NULL);
    testOk1(dbxLockMany(L, 0)==0);

    /* take an extra ref to both locks so that we can
     * inspect them after the join
     */
    LA = A.lock;
    LB = B.lock;
    epicsAtomicIncrIntT(&LA->refcnt);
    epicsAtomicIncrIntT(&LB->refcnt);

    /* Each lock refcnt
     * 1. dbxLockRef
     * 2. dbxLocker::refs
     * 3. dbxLocker::locked
     * 4. this function
     */
    testOk1(LA->refcnt==4);
    testOk1(LB->refcnt==4);
    testOk1(ellCount(&L->locked)==2);

    testOk1((link=dbxLockRefJoin(L, &A, &B))!=NULL);
    testOk1(link->refcnt==1);

    testOk1((link2=dbxLockRefJoin(L, &A, &B))!=NULL);
    testOk1(link==link2);
    testOk1(link->refcnt==2);

    testOk1(dbxLockRefSplit(L, link2)==0);
    testOk1(link->refcnt==1);

    /* same as above with an additional dbxLockRef */
    testOk1(LA->refcnt==5);
    /* our count, dbxLocker::locked, and dbxLocker::refs */
    testOk1(LB->refcnt==3);
    testOk1(ellCount(&L->locked)==2);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==LA);
    testOk1(B.lock==LA);
    testOk1(A.lock->owner==L);

    testOk1(dbxUnlockMany(L)==0);
    /* dec. dbxLocker::locked */
    testOk1(LA->refcnt==4);
    testOk1(LB->refcnt==2);
    testOk1(dbxLockerFree(L)==0);
    /* dec. dbxLocker::refs (only for LA) */
    testOk1(LA->refcnt==3);
    testOk1(LB->refcnt==1);
    testOk1(dbxLockRefClean(&A)==0);
    testOk1(dbxLockRefClean(&B)==0);
    /* dec. dbxLockRefs (both for LA) */
    testOk1(LA->refcnt==1);
    testOk1(LB->refcnt==1);

    dbxlockunref(LA);
    dbxlockunref(LB);

    /* NULL is ok only after dbxLockRefClean() */
    testOk1(dbxLockRefSplit(NULL, link)==0);
}

static void testLockSplit(void)
{
    dbxLockRef A, B, *refs[] = {&A, &B};
    dbxLockLink *link;
    dbxLocker *L;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    testDiag("Test dbxLockMany then dbxLockRefSplit()");

    link = calloc(1, sizeof(*link));
    if(!link) {
        testAbort("Alloc fails");
        return;
    }

    testOk1(dbxLockRefInit(&A, 0)==0);

    /* we setup B to share a lock with A */
    B.lock = A.lock;
    epicsAtomicIncrIntT(&A.lock->refcnt);
    ellAdd(&A.lock->refsets, &B.refsetsNode);

    /* prepare the link between them */
    link->A = &A;
    link->B = &B;
    link->refcnt = 1;
    ellAdd(&A.linksA, &link->linksANode);
    ellAdd(&B.linksB, &link->linksBNode);

    testOk1(A.lock->refcnt==2);

    testOk1((L=dbxLockerAlloc(refs, 2, 0))!=NULL);
    testOk1(A.lock->refcnt==4);

    testOk1(dbxLockMany(L, 0)==0);

    /* lock refcnt
     * 1. 2x dbxLockRef
     * 2. 2x dbxLocker::refs
     * 3. dbxLocker::locked
     */
    testOk1(A.lock->refcnt==5);
    testOk1(ellCount(&L->locked)==1);

    testOk1(dbxLockRefSplit(L, link)==0);

    testOk1(A.lock!=B.lock);

    /* lock A refcnt
     * 1. dbxLockRef
     * 2. 2x dbxLocker::refs
     * 3. dbxLocker::locked
     */
    testOk1(A.lock->refcnt==4);
    /* lock B refcnt
     * 1. dbxLockRef
     * 2. dbxLocker::locked
     */
    testOk1(B.lock->refcnt==2);

    testOk1(A.lock->owner==L);
    testOk1(B.lock->owner==L);
    testOk1(ellCount(&L->locked)==2);

    testOk1(dbxUnlockMany(L)==0);
    testOk1(A.lock->refcnt==3);
    testOk1(B.lock->refcnt==1);
    testOk1(dbxLockerFree(L)==0);
    testOk1(A.lock->refcnt==1);
    testOk1(B.lock->refcnt==1);
    testOk1(dbxLockRefClean(&A)==0);
    testOk1(dbxLockRefClean(&B)==0);
}

static void testLockLinks(void)
{
    dbxLockRef A, B, C, D,
            *refs[] = {&B, &D, &C, &A};
    dbxLockLink *linkAB, *linkBC, *linkCD, *linkDA;
    dbxLocker *L;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));
    memset(&C, 0, sizeof(C));
    memset(&D, 0, sizeof(D));

    testDiag("Test link join/split");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(dbxLockRefInit(&B, 0)==0);
    testOk1(dbxLockRefInit(&C, 0)==0);
    testOk1(dbxLockRefInit(&D, 0)==0);

    /* link for refs together as a box
     * A - B
     * |   |
     * D - C
     */

    testOk1((L=dbxLockerAlloc(refs, 4, 0))!=NULL);
    testOk1(dbxLockMany(L, 0)==0);

    testOk1((linkAB=dbxLockRefJoin(L, &A, &B))!=NULL);
    testOk1(A.lock==B.lock);

    testOk1((linkBC=dbxLockRefJoin(L, &C, &B))!=NULL);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);

    testOk1((linkCD=dbxLockRefJoin(L, &C, &D))!=NULL);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);
    testOk1(A.lock==D.lock);

    testOk1((linkDA=dbxLockRefJoin(L, &D, &A))!=NULL);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);
    testOk1(A.lock==D.lock);

    testOk1(A.lock->refcnt==6);

    testOk1(linkAB!=linkBC);
    testOk1(linkAB!=linkCD);
    testOk1(linkAB!=linkDA);
    testOk1(linkBC!=linkCD);
    testOk1(linkBC!=linkDA);
    testOk1(linkCD!=linkDA);

    testOk1(ellCount(&L->locked)==4);

    testDiag("remove each individual link and replace"
             " it in the opposite direction.");
    testDiag("All refs should still share the same lock");

    testOk1(dbxLockRefSplit(L, linkAB)==0);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);
    testOk1(A.lock==D.lock);
    testOk1(ellCount(&L->locked)==4);
    testOk1((linkAB=dbxLockRefJoin(L, &B, &A))!=NULL);

    testOk1(dbxLockRefSplit(L, linkBC)==0);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);
    testOk1(A.lock==D.lock);
    testOk1(ellCount(&L->locked)==4);
    testOk1((linkBC=dbxLockRefJoin(L, &B, &C))!=NULL);

    testOk1(dbxLockRefSplit(L, linkCD)==0);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);
    testOk1(A.lock==D.lock);
    testOk1(ellCount(&L->locked)==4);
    testOk1((linkCD=dbxLockRefJoin(L, &D, &C))!=NULL);

    testOk1(dbxLockRefSplit(L, linkDA)==0);
    testOk1(A.lock==B.lock);
    testOk1(A.lock==C.lock);
    testOk1(A.lock==D.lock);
    testOk1(ellCount(&L->locked)==4);
    testOk1((linkDA=dbxLockRefJoin(L, &A, &D))!=NULL);

    testOk1(A.lock->refcnt==6);

    testDiag("Remove A-B and C-D to split into two locks AD and BC.");

    testOk1(dbxLockRefSplit(L, linkAB)==0);
    testOk1(dbxLockRefSplit(L, linkCD)==0);
    testOk1(A.lock!=B.lock);
    testOk1(A.lock!=C.lock);
    testOk1(A.lock==D.lock);
    testOk1(B.lock==C.lock);
    testOk1(A.lock->refcnt==4); /* original */
    testOk1(B.lock->refcnt==3); /* new lock */
    testOk1(ellCount(&L->locked)==5);

    testDiag("replace A-B and break B-C.");
    testDiag("A, B, and D share a lock.  C has another.");

    testOk1((linkAB=dbxLockRefJoin(L, &A, &B))!=NULL);
    testOk1(dbxLockRefSplit(L, linkBC)==0);
    testOk1(A.lock==B.lock);
    testOk1(A.lock!=C.lock);
    testOk1(A.lock==D.lock);
    testOk1(A.lock->refcnt==5);
    testOk1(C.lock->refcnt==2); /* new lock */
    testOk1(ellCount(&L->locked)==6);

    testDiag("Cleanup.");

    testOk1(dbxLockRefSplit(L, linkAB)==0);
    testOk1(ellCount(&L->locked)==7);
    testOk1(dbxLockRefSplit(L, linkDA)==0);
    testOk1(ellCount(&L->locked)==8);

    testOk1(dbxUnlockMany(L)==0);
    testOk1(dbxLockerFree(L)==0);
    testOk1(A.lock->refcnt==1);
    testOk1(B.lock->refcnt==1);
    testOk1(C.lock->refcnt==1);
    testOk1(D.lock->refcnt==1);
    testOk1(dbxLockRefClean(&A)==0);
    testOk1(dbxLockRefClean(&B)==0);
    testOk1(dbxLockRefClean(&C)==0);
    testOk1(dbxLockRefClean(&D)==0);
}

static void testRelockJoin(void)
{
    dbxLockRef A, B,
            *refs[] = {&B, &A};
    dbxLockLink *linkAB;
    dbxLocker *L;
    memset(&A, 0, sizeof(A));
    memset(&B, 0, sizeof(B));

    testDiag("test dbxLockMany re-lock after join");

    testOk1(dbxLockRefInit(&A, 0)==0);
    testOk1(dbxLockRefInit(&B, 0)==0);

    testOk1((L=dbxLockerAlloc(refs, 2, 0))!=NULL);
    testOk1(dbxLockMany(L, 0)==0);

    /* cache is good */
    testOk1(L->refs[0].lock==L->refs[0].ref->lock);
    testOk1(L->refs[1].lock==L->refs[1].ref->lock);

    testOk1((linkAB=dbxLockRefJoin(L, &A, &B))!=NULL);

    /* one of the cached refs will no longer match.
     * which one depends on Arch/OS specific stack
     * address ordering.
     */
    testOk1((L->refs[0].lock!=L->refs[0].ref->lock) ^
            (L->refs[1].lock!=L->refs[1].ref->lock));

    testOk1(dbxUnlockMany(L)==0);

    testOk1(dbxLockMany(L, 0)==0);
    /* cache is good again */
    testOk1(L->refs[0].lock==L->refs[0].ref->lock);
    testOk1(L->refs[1].lock==L->refs[1].ref->lock);

    testOk1(L->refs[0].ref->lock==L->refs[1].ref->lock);

    testOk1(dbxUnlockMany(L)==0);
    testOk1(dbxLockerFree(L)==0);
    testOk1(A.lock->refcnt==2);
    testOk1(dbxLockRefClean(&B)==0);
    testOk1(dbxLockRefClean(&A)==0);

    testOk1(dbxLockRefSplit(NULL, linkAB)==0);
}

MAIN(testlock)
{
    testPlan(230);
    testCreate();
    testLockerSort();
    testLockOne();
    testLockMany();
    testLockManyToOne();
    testLockJoin();
    testLockSplit();
    testLockLinks();
    testRelockJoin();
    return testDone();
}
