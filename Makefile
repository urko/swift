CPP=g++
CPPFLAGS=-I. -g -O0 #-I. -O2
CPPOPTS=$(CPP) $(CPPFLAGS)
COMPILE=$(CPPOPTS) -c

OBJS=sha1.o compat.o sendrecv.o send_control.o hashtree.o bin64.o bins.o channel.o datagram.o transfer.o httpgw.o 
OUT_DIR=bin
OUT_OBJS := $(addprefix $(OUT_DIR)/,$(OBJS))

$(OUT_DIR)/%.o: %.cpp
				$(COMPILE) -o $@ $<

all: swift

swift:	$(OUT_DIR)/swift.o $(OUT_OBJS)
		$(CPPOPTS) $(OUT_OBJS) $(OUT_DIR)/swift.o -o $(OUT_DIR)/swift
		
clean:	
		\rm $(OUT_DIR)/*


