# ---------------------------------
# start of user settings
# ---------------------------------

# the shader compiler
SC=glslc
# the c++ compiler
CC=g++
# the linker
LD=g++

# put your warnings here, -W preffix is assumed
WARNS := all extra shadow non-virtual-dtor old-style-cast cast-align
WARNS += unused overloaded-virtual pedantic conversion sign-conversion
WARNS += misleading-indentation null-dereference double-promotion
WARNS += format=2

# you put your generic flags here, - prefix is assumed
CFLAGS=Og ggdb std=c++2a

# put the libraries you want to link against here, -l prefix is assued
LIBS=pthread SDL2 vulkan

# here are the auxiliary library and header paths
PATH_INCS=
PATH_LIBS=
PATH_DLIB=

# path where shaders are stored
PATH_SHD= shaders

# path where compiled shaders are stored
PATH_SHO= build/shaders

# path where the build will occur
PATH_OBJ= build

# path where the sources are situated
PATH_SRC= src

# the binary name, it will be in the build directory
BIN_NAME= build

# ---------------------------------
# end of user settings
# ---------------------------------

SRCS=$(shell find $(PATH_SRC) -name "*.cpp")
OBJS=$(SRCS:$(PATH_SRC)/%.cpp=$(PATH_OBJ)/%.o)
DEPS=$(SRCS:$(PATH_SRC)/%.cpp=$(PATH_OBJ)/%.d)
SHDS=$(shell find $(PATH_SHD) -type f -not -name ".*")
SHOS=$(SHDS:$(PATH_SHD)/%=$(PATH_SHO)/%.spv)

COMPILE_SHD=$(SC)
COMPILE_CPP=$(CC) -MMD -c $(patsubst %, -%,$(CFLAGS)) $(patsubst %, -I%,$(PATH_INCS)) $(patsubst %, -W%,$(WARNS))
LINK_CPP=$(LD) -o $@ $^ $(patsubst %,-rpath %,$(PATH_DLIB)) $(patsubst %, -L%, $(PATH_LIBS)) $(patsubst %, -l%, $(LIBS)) $(patsubst %, -%, $(LFLAGS))

all:$(PATH_OBJ)/$(BIN_NAME) shaders
	@echo $(SHOS)

clean:
	rm -rf $(PATH_OBJ)

$(PATH_OBJ):
	@mkdir -p $@ 

$(PATH_OBJ)/$(BIN_NAME):$(OBJS)
	@echo "Building target" $@
	@echo "Invoking" $(LD) on $^
	@$(LINK_CPP)
	@echo "Building target" $@ "Complete"
	@chmod +x $@

$(PATH_OBJ)/%.o: $(PATH_SRC)/%.cpp ./makefile | $(PATH_OBJ)
	@echo "Building target" $@
	@echo "Invoking" $(CC) on $<
	@mkdir -p $(@D)
	@$(COMPILE_CPP) -o $@ $<
	@echo "Building target" $@ "Complete"

$(PATH_SHO)/%.spv: $(PATH_SHD)/% ./makefile
	@echo "Building target" $@
	@echo "Invoking" $(SC) on $<
	@mkdir -p $(@D)
	@$(COMPILE_SHD) -o $@ $<
	@echo "Building target" $@ "Complete"

shaders: $(SHOS)

.PHONY: all clean

$(DEPS):

-include $(DEPS)
