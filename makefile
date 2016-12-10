CC = gcc -O2 -nostartfiles -nostdlib -Wall --std=c99 -c
LIBS = -L$(LIBRARY_PATH) -lkernel32 -lshell32 -lole32 -lShlwapi
LINK = ld -s --subsystem windows --kill-at --no-seh
OBJDIR = obj
BINDIR = bin
COMMON_OBJ = $(OBJDIR)/FSEntriesContainer.o $(OBJDIR)/DirectoriesContainer.o $(OBJDIR)/WorkQueue.o


clear_all_objs:	
	@rm -f $(OBJDIR)/*.o > NUL

mem.o:
	@rm -f $(OBJDIR)/mem.o > NUL
	$(CC) src/mem.c -o obj/mem.o

FSEntriesContainer.o:
	@rm -f $(OBJDIR)/FSEntriesContainer.o > NUL
	$(CC) src/FSEntriesContainer.c -o obj/FSEntriesContainer.o

DirectoriesContainer.o: FSEntriesContainer.o
	@rm -f $(OBJDIR)/DirectoriesContainer.o > NUL
	$(CC) src/DirectoriesContainer.c -o obj/DirectoriesContainer.o

WorkQueue.o:
	@rm -f $(OBJDIR)/WorkQueue.o > NUL
	$(CC) src/WorkQueue.c -o obj/WorkQueue.o

test.o: WorkQueue.o DirectoriesContainer.o
	@rm -f $(OBJDIR)/test.o
	$(CC) src/test.c -o $(OBJDIR)/test.o

test: test.o mem.o
	$(LINK) $(COMMON_OBJ) $(OBJDIR)/mem.o $(OBJDIR)/test.o -e entry_point -o $(BINDIR)/test.exe $(LIBS)

installer.o:
	@rm -f $(OBJDIR)/installer.o > NUL
	$(CC) src/installer.c -o $(OBJDIR)/installer.o

installer: installer.o
	@rm -f $(BINDIR)/installer.exe
	$(LINK) $(OBJDIR)/installer.o -e entry_point -o $(BINDIR)/installer.exe $(LIBS) -luuid -luser32 -lNtosKrnl -ladvapi32