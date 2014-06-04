TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = dbx
# dbx.dbd will be created and installed
DBD += dbx.dbd

# dbx.dbd will be made up from these files:
dbx_DBD += base.dbd

# Include dbd files from all support applications:
#dbx_DBD += xxx.dbd

# Add all the support libraries needed by this IOC
#dbx_LIBS += xxx

# dbx_registerRecordDeviceDriver.cpp derives from dbx.dbd
dbx_SRCS += dbx_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
dbx_SRCS_DEFAULT += dbxMain.cpp
dbx_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#dbx_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
dbx_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

