CFLAGS := $(CFLAGS) -Wall -Wextra -Werror -O2
DESTDIR := /usr

.PHONY: install

mv_data: main.c
	$(CC) $(CFLAGS) -o $@ $^

install: mv_data
	install -m 755 -t $(DESTDIR)/bin $^

README: mv_data
	./mv_data --help > README
