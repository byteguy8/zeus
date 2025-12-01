PLATFORM            := LINUX
BUILD   		    := DEBUG

COMPILER.LINUX      := gcc
COMPILER.WINDOWS    := mingw64
COMPILER            := $(COMPILER.$(PLATFORM))

INCLUDE_DIR         := ./include
SRC_DIR             := ./src
OUT_DIR             := ./build

FLAGS.WNOS          := -Wno-unused-parameter -Wno-unused-function
FLAGS.COMMON        := -Wall -Wextra $(FLAGS.WNOS)
FLAGS.DEBUG         := -g2 -O0
FLAGS.DEBUG.LINUX   := -fsanitize=address,undefined,alignment
FLAGS.RELEASE       := -O3
FLAGS.LINUX         := --std=gnu99
FLAGS.WINDOWS       := --std=c99
FLAGS.DEFAULT       := $(FLAGS.COMMON) $(FLAGS.$(BUILD)) $(FLAGS.$(BUILD).$(PLATFORM)) $(FLAGS.$(PLATFORM))
FLAGS               := $(FLAGS.DEFAULT) -I$(INCLUDE_DIR)

FLAGS.ESSENTIALS    := $(FLAGS.DEFAULT) -I$(INCLUDE_DIR)/essentials
FLAGS.SCOPE_MANAGER := $(FLAGS.DEFAULT) -I$(INCLUDE_DIR)/scope_manager -I$(INCLUDE_DIR)
FLAGS.NATIVES       := $(FLAGS.DEFAULT) -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/native
FLAGS.VM            := $(FLAGS.DEFAULT) -I$(INCLUDE_DIR)/vm -I$(INCLUDE_DIR)

ESSENTIALS_OBJS     := lzbstr.o dynarr.o lzohtable.o lzarena.o lzpool.o lzflist.o memory.o
NATIVES_OBJS        := splitmix64.o xoshiro256.o
SCOPE_MANAGER_OBJS  := scope_manager.o native.o native_random.o native_nbarray.o native_file.o
VM_OBJS             := vm_factory.o obj.o vmu.o vm.o
OBJS                := $(ESSENTIALS_OBJS) \
					   $(NATIVES_OBJS) \
					   $(SCOPE_MANAGER_OBJS) \
                       $(VM_OBJS) \
					   utils.o lexer.o \
				       parser.o compiler.o \
					   dumpper.o

LINKS.COMMON        := -lm
LINKS.WINDOWS       := -lshlwapi
LINKS               := $(LINKS.COMMON) $(LINKS.$(PLATFORM))

zeus: $(OBJS)
	$(COMPILER) -o $(OUT_DIR)/zeus $(FLAGS) $(OUT_DIR)/*.o $(SRC_DIR)/zeus.c $(LINKS)

vm.o:
	$(COMPILER) -c -o $(OUT_DIR)/vm.o $(FLAGS.VM) $(SRC_DIR)/vm/vm.c
vmu.o:
	$(COMPILER) -c -o $(OUT_DIR)/vmu.o $(FLAGS.VM) $(SRC_DIR)/vm/vmu.c
obj.o:
	$(COMPILER) -c -o $(OUT_DIR)/obj.o $(FLAGS.VM) $(SRC_DIR)/vm/obj.c
vm_factory.o:
	$(COMPILER) -c -o $(OUT_DIR)/vm_factory.o $(FLAGS.VM) $(SRC_DIR)/vm/vm_factory.c

dumpper.o:
	$(COMPILER) -c -o $(OUT_DIR)/dumpper.o $(FLAGS) $(SRC_DIR)/dumpper.c
compiler.o:
	$(COMPILER) -c -o $(OUT_DIR)/compiler.o $(FLAGS) $(SRC_DIR)/compiler.c
parser.o:
	$(COMPILER) -c -o $(OUT_DIR)/parser.o $(FLAGS) $(SRC_DIR)/parser.c
lexer.o:
	$(COMPILER) -c -o $(OUT_DIR)/lexer.o $(FLAGS) $(SRC_DIR)/lexer.c

native_file.o:
	$(COMPILER) -c -o $(OUT_DIR)/native_file.o $(FLAGS.NATIVES) $(SRC_DIR)/native/native_file.c
native_nbarray.o:
	$(COMPILER) -c -o $(OUT_DIR)/native_nbarray.o $(FLAGS.NATIVES) $(SRC_DIR)/native/native_nbarray.c
native_random.o:
	$(COMPILER) -c -o $(OUT_DIR)/native_random.o $(FLAGS.NATIVES) $(SRC_DIR)/native/native_random.c
native.o:
	$(COMPILER) -c -o $(OUT_DIR)/native.o $(FLAGS.NATIVES) $(SRC_DIR)/native/native.c
xoshiro256.o:
	$(COMPILER) -c -o $(OUT_DIR)/xoshiro256.o $(FLAGS.NATIVES) $(SRC_DIR)/native/xoshiro256.c
splitmix64.o:
	$(COMPILER) -c -o $(OUT_DIR)/splitmix64.o $(FLAGS.NATIVES) $(SRC_DIR)/native/splitmix64.c

utils.o:
	$(COMPILER) -c -o $(OUT_DIR)/utils.o $(FLAGS) $(SRC_DIR)/utils.c

scope_manager.o:
	$(COMPILER) -c -o $(OUT_DIR)/scope_manager.o $(FLAGS.SCOPE_MANAGER) $(SRC_DIR)/scope_manager/scope_manager.c

memory.o:
	$(COMPILER) -c -o $(OUT_DIR)/memory.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/memory.c
lzflist.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzflist.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/lzflist.c
lzpool.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzpool.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/lzpool.c
lzarena.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzarena.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/lzarena.c
lzohtable.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzohtable.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/lzohtable.c
dynarr.o:
	$(COMPILER) -c -o $(OUT_DIR)/dynarr.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/dynarr.c
lzbstr.o:
	$(COMPILER) -c -o $(OUT_DIR)/lzbstr.o $(FLAGS.ESSENTIALS) $(SRC_DIR)/essentials/lzbstr.c
