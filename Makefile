.PHONY: examples
.PHONY: clean

clean:
	rm -f simple_crud.out
examples: simple_crud.out

simple_crud.out: examples/simple_crud.c
	$(CC) -g -Wall -Wextra -pedantic -std=c11 -o simple_crud.out examples/simple_crud.c -lsqlite3

clean:
	rm -f simple_crud.out
