################################################################################
#									       #
#     Makefile model:							       #							       #
#									       #
################################################################################

# Define NO-OS directory will be set to ../../ assuming this
# makefile is called from no-os/projects/some_project
# Uncomment to set an other noos directory
#NO-OS = $(realpath ../..)

# Default Workspace on aducm will be at $(NO-OS)/projects and on the other 
# platform at no-os/projects/some_project/build.
# Uncomment to set a custom workspace
#WORKSPACE = $(NO-OS)/projects

# Default project path will be .
# Uncomment to set a custom project path
#PROJECT  = $(realpath .)

# Choose platform for build
#PLATFORM = aducm3029
#PLATFORM = xilinx
#PLATFORM = altera

# To use libraries add them to the LIBRARIES variable. 
#LIBRARIES += mqtt
#LIBRARIES += fatfs
#LIBRARIES += iio
#LIBRARIES += mbedtls

#Notes: 
# - Path of folders and files is relative to No-OS folder.
# - Do not clone No-OS repo to in a path with spaces.
# - Git should be installed and availiable from command line 
#   for some functionalites

# By default .h files from includes, all files from platform drivers and utils
# will be added to the build. Use ignore files to ignore them.
# For example:
#IGNORE_FILES += utils/xml.c

# Add directories as source folders. All .c and .h files from them will be added
# to the build recursively
# For example:
#SRC_DIRS += network
#SRC_DIRS += drivers/accel/adxl362
#SRC_DIRS += projects/iio_demo/src

# Add only specific files to build

# SRCS += drivers/axi_core/jesd204/xilinx_transceiver.c
# SRCS += drivers/axi_core/jesd204/altera_adxcvr.c

# INCS += drivers/axi_core/jesd204/xilinx_transceiver.h
# INCS += drivers/axi_core/jesd204/altera_adxcvr.h

include ../../tools/scripts/generic.mk