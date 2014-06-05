TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

LIBRARY_IOC = dbx

LIB_SRCS += dbxlock.c

dbx_LIBS += Com

TESTPROD_IOC = testlock
testlock_SRCS += testlock.c
testlock_LIBS += dbx Com

## Enable GCC coverage stats
#dbxlock_CFLAGS += -fprofile-arcs -ftest-coverage
#USR_LDFLAGS += -lgcov -coverage

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

