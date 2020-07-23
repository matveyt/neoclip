BUILDTYPE ?= release
include yama.mk

LUA_CFLAGS0 ?= $(call yama.lazyShell,pkg-config --cflags lua5.1,LUA_CFLAGS0)
LUA_LIBS0 ?= $(call yama.lazyShell,pkg-config --libs lua5.1,LUA_LIBS0)

CFLAGS += $(LUA_CFLAGS0)
LDLIBS += $(LUA_LIBS0)

all: neoclip

$(call yama.goalDll,neoclip,neoclip_w32 splitjoin,../lua/neoclip_w32,build)
$(call yama.rules,build)
