ALL: ring_example

HEADERS=$(wildcard *.h)

CC=gcc -w -s

CFLAGS	+=  -g -W -Wall -lpthread -O2
CFLAGS	+= -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wold-style-definition
CFLAGS	+= -Wpointer-arith -Wcast-align -Wnested-externs -Wcast-qual -Wformat-nonliteral
CFLAGS	+= -Wformat-security -Wundef -Wwrite-strings -Wdeprecated 

%.o: %.c $(HEADERS)
	@$(CC) $(CFLAGS) -c $< -o $@

_obj=rte_ring.o main.o
	
ring_example : $(_obj)
	@$(CC) $(CFLAGS) $(_obj) -o $@
	
	@echo -e "\e[01;34mBuild was successful.\e[00m"
	
clean:
	@rm -f *.o 
	@rm -f ring_example 
	@echo -e "\e[01;33mObject files were removed.\e[00m"