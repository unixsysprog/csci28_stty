# ------------------------------------------------------------
# Makefile for sttyl
# ------------------------------------------------------------
# Compiles with messages about warnings and produces debugging
# information. Only file is sttyl.c.
#

GCC = gcc -Wall -g

sttyl: sttyl.o
	$(GCC) -o sttyl sttyl.o

sttyl.o: sttyl.c
	$(GCC) -c sttyl.c

clean:
	rm -f *.o sttyl
