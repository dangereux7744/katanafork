CC = gcc
CFLAGS = -ldwarf -lelf
OBJS = dwarf_instr.o growingBuffer.o leb.o types.o register.o symbol.o elfparse.o callFrameInfo.o elfutil.o fderead.o eh_pe.o elfwriter.o dwarftypes.o dwarfvm.o relocation.o list.o logging.o refcounted.o dictionary.o map.o hash.o util.o path.o stack.o target.o versioning.o hotpatch.o
PROG = dwarf_compiler

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

dwarf_instr.o: dwarf_instr.c dwarf_instr.h
	$(CC) $(CFLAGS) -c dwarf_instr.c

growingBuffer.o: growingBuffer.c growingBuffer.h
	$(CC) $(CFLAGS) -c growingBuffer.c

leb.o: leb.c leb.h
	$(CC) $(CFLAGS) -c leb.c

types.o: types.c types.h
	$(CC) $(CFLAGS) -c types.c

register.o: register.c register.h
	$(CC) $(CFLAGS) -c register.c 

symbol.o: symbol.c symbol.h
	$(CC) $(CFLAGS) -c symbol.c

elfutil.o: elfutil.c elfutil.h
	$(CC) $(CFLAGS) -c elfutil.c

elfparse.o: elfparse.c elfparse.h
	$(CC) $(CFLAGS) -c elfparse.c

fderead.o: fderead.c fderead.h
	$(CC) $(CFLAGS) -c fderead.c

callFrameInfo.o: callFrameInfo.c callFrameInfo.h
	$(CC) $(CFLAGS) -c callFrameInfo.c  	 

eh_pe.o: eh_pe.c eh_pe.h
	$(CC) $(CFLAGS) -c eh_pe.c 

elfwriter.o: elfwriter.c elfwriter.h
	$(CC) $(CFLAGS) -c elfwriter.c

dwarftypes.o: dwarftypes.c dwarftypes.h
	$(CC) $(CFLAGS) -c dwarftypes.c  

dwarfvm.o: dwarfvm.c dwarfvm.h
	$(CC) $(CFLAGS) -c dwarfvm.c

relocation.o: relocation.c relocation.h
	$(CC) $(CFLAGS) -c relocation.c	

list.o: util/list.c util/list.h
	$(CC) $(CFLAGS) -c util/list.c

logging.o: util/logging.c util/logging.h
	$(CC) $(CFLAGS) -c util/logging.c

refcounted.o: util/refcounted.c util/refcounted.h
	$(CC) $(CFLAGS) -c util/refcounted.c

dictionary.o: util/dictionary.c util/dictionary.h
	$(CC) $(CFLAGS) -c util/dictionary.c

map.o: util/map.c util/map.h
	$(CC) $(CFLAGS) -c util/map.c

list.o: util/list.c util/list.h
	$(CC) $(CFLAGS) -c util/list.c

hash.o: util/hash.c util/hash.h
	$(CC) $(CFLAGS) -c util/hash.c

util.o: util/util.c util/util.h
	$(CC) $(CFLAGS) -c util/util.c

path.o: util/path.c util/path.h
	$(CC) $(CFLAGS) -c util/path.c

stack.o: util/stack.c util/stack.h
	$(CC) $(CFLAGS) -c util/stack.c

cxxutil.o: util/cxxutil.cpp util/cxxutil.h
	$(CC) $(CFLAGS) -c util/cxxutil.cpp

target.o: patcher/target.c patcher/target.h
	$(CC) $(CFLAGS) -c patcher/target.c

versioning.o: patcher/versioning.c patcher/versioning.h
	$(CC) $(CFLAGS) -c patcher/versioning.c

hotpatch.o: patcher/hotpatch.c patcher/hotpatch.h
	$(CC) $(CFLAGS) -c patcher/hotpatch.c

clean:
	rm -f *~ *.o $(PROG) core a.out
