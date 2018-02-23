# This file is protected by Copyright. Please refer to the COPYRIGHT file
# distributed with this source distribution.
#
# This file is part of OpenCPI <http://www.opencpi.org>
#
# OpenCPI is free software: you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# OpenCPI is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with this program. If not, see <http://www.gnu.org/licenses/>.

ifndef OCPI_CDK_DIR
OCPI_CDK_DIR=$(realpath ../../exports)
endif

ifdef APP
all: $(PROG)
else
all:
endif

DIR=target-$(OCPI_TARGET_DIR)
$(DIR):
	mkdir -p $(DIR)

ifeq ($(filter clean%,$(MAKECMDGOALS)),)
  include $(OCPI_CDK_DIR)/include/ocpisetup.mk
endif
PROG=$(DIR)/$(APP)
OUT= > /dev/null

INCS = -I$(OCPI_INC_DIR) -I$(OCPI_PREREQUISITES_DIR)/ad9361/include/

ifdef APP
$(PROG): $(APP).cxx | $(DIR)
	$(AT)echo Building $@...
	$(AT)$(CXX) -g -std=c++11 -Wall $(OCPI_EXPORT_DYNAMIC) -o $@ $(INCS) $^ $(OCPI_LD_FLAGS)
endif

clean::
	$(AT)rm -r -f lib target-* *.*~
