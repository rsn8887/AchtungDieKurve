#---------------------------------------------------------------------------------
# Achtung, die Kurve!  -  Nintendo Switch build (devkitPro / libnx / SDL2)
#
#   Put this Makefile and achtung.cpp in the same folder, then run  make
#   Output: achtung.nro  (copy it to /switch/ on the SD card)
#
#   One-time setup inside the devkitPro environment:
#       dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf
#
#   Text is drawn with SDL2_ttf. romfs/font.ttf is bundled into the .nro; without it
#   the game uses the Switch system font.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code ("." = this folder)
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS (empty = none)
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to this Makefile. If it is
#   not set, achtung.jpg or icon.jpg next to this Makefile is used when present,
#   otherwise the default libnx icon.
#---------------------------------------------------------------------------------
TARGET		:=	achtung
BUILD		:=	build
SOURCES		:=	.
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	$(if $(wildcard romfs/font.ttf),romfs,)

APP_TITLE	:=	Achtung, die Kurve!
APP_AUTHOR	:=	rsn8887
APP_VERSION	:=	1.0.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# SDL2 from the devkitPro portlibs. sdl2-config knows the right flags and the
# libraries SDL2 needs (EGL, GLES, nouveau, ...); if it is missing, fall back
# to the usual list.
#---------------------------------------------------------------------------------
SWITCH_PORTLIBS	:=	$(DEVKITPRO)/portlibs/switch
SDL2_CONFIG	?=	$(SWITCH_PORTLIBS)/bin/sdl2-config

ifneq ($(wildcard $(SDL2_CONFIG)),)
	SDL_CFLAGS	:=	$(shell $(SDL2_CONFIG) --cflags)
	SDL_LIBS	:=	$(shell $(SDL2_CONFIG) --libs)
else
	SDL_CFLAGS	:=	-I$(SWITCH_PORTLIBS)/include/SDL2
	SDL_LIBS	:=	-lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau
endif

# SDL2_ttf (and the FreeType etc. it needs) - must come before SDL2 in the link order
PKG_CONFIG	?=	$(SWITCH_PORTLIBS)/bin/aarch64-none-elf-pkg-config
ifneq ($(wildcard $(PKG_CONFIG)),)
	TTF_LIBS	:=	$(shell $(PKG_CONFIG) --static --libs SDL2_ttf 2>/dev/null)
endif
ifeq ($(strip $(TTF_LIBS)),)
	TTF_LIBS	:=	-lSDL2_ttf -lfreetype -lbz2 -lpng16 -lz
endif

CFLAGS		+=	$(SDL_CFLAGS)
CXXFLAGS	+=	$(SDL_CFLAGS)

LIBS	:=	$(TTF_LIBS) $(SDL_LIBS) -lm -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifeq ($(ROMFS),)
$(warning romfs/font.ttf not found - the game will use the Switch system font)
endif
ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
