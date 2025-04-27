GCC = gcc
CFLAGS = -g -Wall -Wshadow

# Make all objects and exe
all: oss user

# Make exe 'oss'
oss: oss.o
	$(GCC) $(CFLAGS) oss.o  -o oss

# Make exe 'user'
user: user.o
	$(GCC) $(CFLAGS) user.o -o user

# Make oss object
oss.o: oss.c
	$(GCC) $(CFLAGS) -c -o oss.o oss.c

# Make user object
user.o: user.c
	$(GCC) $(CFLAGS) -c -o user.o user.c

# Clean object files and exe.
clean:
	rm -f user.o oss.o oss user
