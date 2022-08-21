gimli: CFLAGS = -Wall -Werror -pthread
gimli-cli: CFLAGS = -Wall -Werror

all: gimli

gimli: gimli.c

clean:
	rm -f gimli

install:
	mkdir -p $(HOME)/bin && cp gimli $(HOME)/bin
