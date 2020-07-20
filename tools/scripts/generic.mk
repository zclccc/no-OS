#------------------------------------------------------------------------------
#                              UTIL FUNCTIONS
#------------------------------------------------------------------------------
#TODO Add platform agnostic function here and make xilinx and altera makefiles
# platform agnostic

# recursive wildcard
rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

#------------------------------------------------------------------------------
#                             EXPORTED VARIABLES                               
#------------------------------------------------------------------------------
# TODO Remove common lines from specific makefiles
# Used by nested Makefils (mbedtls, fatfs, iio)
export CFLAGS
export CC
export AR

#------------------------------------------------------------------------------
#                           DEFAULT VARIABLES                              
#------------------------------------------------------------------------------

NO-OS			?= $(realpath ../..)
WORKSPACE		?= $(NO-OS)/projects
PROJECT			?= $(realpath .)

#------------------------------------------------------------------------------
#                          COMMON INITIALIZATION
#------------------------------------------------------------------------------

#Add default directories
SRC_DIRS += drivers/platform/$(PLATFORM)
SRC_DIRS += include
SRC_DIRS += util

# Add noos path prefix to al src and include files
SRCS     := $(addprefix $(NO-OS)/, $(SRCS)))
INCS     := $(addprefix $(NO-OS)/, $(INCS)))
SRC_DIRS := $(addprefix $(NO-OS)/, $(SRC_DIRS)))

# Get all .c and .h files from SRC_DIRS
SRCS     += $(foreach dir, $(SRC_DIRS), $(call rwildcard, $(dir),*.c))
INCS     += $(foreach dir, $(SRC_DIRS), $(call rwildcard, $(dir),*.h))

# Remove ignored files
SRCS     := $(filter-out $(IGNORE_FILES),$(SRCS))
INCS     := $(filter-out $(IGNORE_FILES),$(INCS))

#TODO add libraries initialization here

#------------------------------------------------------------------------------
#                          CALL SPECIFIC MAKEFILES
#------------------------------------------------------------------------------

ifeq (aducm3029,$(strip $(PLATFORM)))
#Aducm3029 makefile
include $(NO-OS)/tools/scripts/aducm.mk

else
#Xilnx and altera makefiles
ifeq ($(OS), Windows_NT)
include $(NO-OS)/tools/scripts/windows.mk
else
include $(NO-OS)/tools/scripts/linux.mk
endif

endif