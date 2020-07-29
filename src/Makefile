BUILDTYPE ?= release
include yama.mk

LUA_CFLAGS0 ?= $(call yama.lazyShell,pkg-config --cflags lua5.1,LUA_CFLAGS0)
LUA_LIBS0 ?= $(call yama.lazyShell,pkg-config --libs lua5.1,LUA_LIBS0)

CFLAGS += $(LUA_CFLAGS0)
LDLIBS += $(LUA_LIBS0)

ifdef yama.IsWindows
    all: neoclip_w32
else
    all: neoclip_x11
endif

$(call yama.goalDll,neoclip_w32,neoclip_w32 splitjoin,../lua/neoclip_w32,build)
$(call yama.goalDll,neoclip_x11,neoclip_x11 splitjoin,../lua/neoclip_x11,build)
$(call yama.rules,build)
