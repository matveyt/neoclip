BUILDTYPE ?= release
include yama.mk

LUA_CFLAGS0 ?= $(call yama.lazyShell,pkg-config --cflags luajit,LUA_CFLAGS0)
LUA_LIBS0 ?= $(call yama.lazyShell,pkg-config --libs luajit,LUA_LIBS0)
CFLAGS += $(LUA_CFLAGS0)
LDLIBS += $(LUA_LIBS0)

ifdef yama.IsWindows
    all: neoclip_w32
else ifdef yama.IsPOSIX
    CFLAGS += -pthread -fPIC -fvisibility=internal
    LDFLAGS += -pthread
    LDLIBS += -lX11
    all: neoclip_x11
endif

$(call yama.goalDll,../lua/neoclip_w32,neoclip_w32 splitjoin,,build)
$(call yama.goalDll,../lua/neoclip_x11,neoclip_x11 splitjoin neo_x,,build)
$(call yama.rules,build)
